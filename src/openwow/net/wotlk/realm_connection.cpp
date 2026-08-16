
#include "openwow/net/wotlk/realm_connection.h"

#include "openwow/data/db_cache_instances.h"
#include "openwow/net/wotlk/addon_handshake.h"
#include "openwow/network/protocol/wotlk/opcodes.h"
#include "openwow/net/wotlk/protocol/realm_connection_packets.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <list>
#include <mutex>
#include <utility>

namespace openwow::net::wotlk {

namespace {

class RealmConnectionRegistry {
 public:
  static RealmConnectionRegistry& Instance() {
    static auto* registry = new RealmConnectionRegistry();
    return *registry;
  }

  void Register(RealmConnection* instance) {
    std::scoped_lock lock(mutex_);
    active_instances_.push_front(instance);
  }

  void Unregister(const RealmConnection* instance) {
    std::scoped_lock lock(mutex_);
    const auto it =
        std::find(active_instances_.begin(), active_instances_.end(), instance);
    if (it != active_instances_.end()) {
      active_instances_.erase(it);
    }
  }

  [[nodiscard]] std::size_t Count() const {
    std::scoped_lock lock(mutex_);
    return active_instances_.size();
  }

  [[nodiscard]] const RealmConnection *ActiveInstance() const {
    std::scoped_lock lock(mutex_);
    return active_instances_.empty() ? nullptr : active_instances_.front();
  }

  [[nodiscard]] bool Contains(const RealmConnection* instance) const {
    std::scoped_lock lock(mutex_);
    return std::find(active_instances_.begin(), active_instances_.end(),
                     instance) != active_instances_.end();
  }

  void DrainQueuedEvents() {
    std::scoped_lock lock(mutex_);
    for (auto it = active_instances_.begin(); it != active_instances_.end();) {
      RealmConnection* instance = *it;
      ++it;
      if (instance != nullptr) {
        instance->DrainQueuedEvents();
      }
    }
  }

 private:
  mutable std::recursive_mutex mutex_;
  std::list<RealmConnection*> active_instances_;
};

std::uint8_t ReadSingleByteForwardValue(const std::uint8_t* payload,
                                        const std::size_t size) {

  std::uint8_t forwarded =
      static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(payload));
  if (size >= 1) {
    forwarded = payload[0];
  }
  return forwarded;
}

std::size_t SingleByteReadConsumedSize(const std::size_t size) {
  return size >= sizeof(std::uint8_t) ? sizeof(std::uint8_t) : 0;
}

void LogRealmConnectionMessageUnderRead(const std::uint16_t opcode,
                                        const std::size_t consumed,
                                        const std::size_t size) {
  openwow::diagnostics::Log(
      openwow::diagnostics::LogLevel::kWarn,
      "RealmConnection::MessageHandler Message Under Read! ID:" +
          std::to_string(opcode) + " Read:" + std::to_string(consumed) +
          " Size:" + std::to_string(size));
}

}

RealmConnection::RealmConnection() {
  RealmConnectionRegistry::Instance().Register(this);
}

RealmConnection::~RealmConnection() {
  RealmConnectionRegistry::Instance().Unregister(this);
}

std::size_t RealmConnection::ActiveInstanceCount() {
  return RealmConnectionRegistry::Instance().Count();
}

const RealmConnection *RealmConnection::GetActiveInstance() {
  return RealmConnectionRegistry::Instance().ActiveInstance();
}

bool RealmConnection::IsActiveInstance(const RealmConnection* instance) {
  return RealmConnectionRegistry::Instance().Contains(instance);
}

void RealmConnection::DrainActiveQueuedEvents() {
  RealmConnectionRegistry::Instance().DrainQueuedEvents();
}

void RealmConnection::SetAuthSessionSeedWords(const std::uint32_t seed0,
                                              const std::uint32_t seed1,
                                              const std::uint32_t seed2) {
  seed_words_[0] = seed0;
  seed_words_[1] = seed1;
  seed_words_[2] = seed2;
}

void RealmConnection::OnConnected() {
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo, "RealmConnection: Connected");
  authenticated_ = false;
  if (callbacks_.on_connected) {
    callbacks_.on_connected();
  }
  client_state_ = openwow::net::kStateConnected;
}

void RealmConnection::OnDisconnected() {
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "RealmConnection: Disconnected");
  if (callbacks_.on_disconnected) {
    callbacks_.on_disconnected();
  }
  client_state_ = openwow::net::kStateInitialized;
}

void RealmConnection::OnCantConnect() {
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                     "RealmConnection: Can't Connect");
  if (callbacks_.on_cant_connect) {
    callbacks_.on_cant_connect();
  }
  client_state_ = openwow::net::kStateInitialized;
}

int RealmConnection::MessageHandler(const std::uint16_t opcode,
                                    const std::uint8_t* payload,
                                    const std::size_t size) {
  MessageDispatchResult dispatch{};

  if (opcode > 77) {
    if (opcode > 751) {
      if (opcode == 1195) {
        dispatch = HandleClientCacheVersion(payload, size);
      }

    } else {
      switch (opcode) {
        case 751:
          dispatch = HandleAddonInfo(payload, size);
          break;
        case 79:
          if (callbacks_.on_logout_cancel_ack) {
            callbacks_.on_logout_cancel_ack();
          }
          dispatch = {.result = 1, .consumed = 0};
          break;
        case 494:
          dispatch = HandleAuthResponse(payload, size);
          break;
        default:
          break;
      }
    }
  } else if (opcode == 77) {
    if (callbacks_.on_logout_complete) {
      callbacks_.on_logout_complete();
    }
    dispatch = {.result = 1, .consumed = 0};
  } else {
    switch (opcode) {
      case 58:
        dispatch = HandleCharCreate(payload, size);
        break;
      case 59:
        dispatch = HandleCharEnum(payload, size);
        break;
      case 60:
        dispatch = HandleCharDelete(payload, size);
        break;
      case 65:
        dispatch = HandleCharLoginFailed(payload, size);
        break;
      case 76:
        dispatch = HandleLogoutResponse(payload, size);
        break;
      default:
        break;
    }
  }

  if (dispatch.consumed != size) {
    LogRealmConnectionMessageUnderRead(opcode, dispatch.consumed, size);
  }

  return dispatch.result;
}

RealmConnection::MessageDispatchResult RealmConnection::HandleAuthResponse(
    const std::uint8_t* payload, const std::size_t size) {
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "RealmConnection: SMSG_AUTH_RESPONSE");

  RealmConnectionAuthResponsePayload parsed;
  std::size_t consumed = 0;
  const auto fallback_result_code =
      static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(this));
  ParseRealmConnectionAuthResponseWithFallback(
      payload, size, fallback_result_code, parsed, &consumed);

  if (parsed.result_code == AUTH_OK) {
    authenticated_ = true;
  }

  if (parsed.has_account_info) {
    billing_time_ = parsed.billing_time;
    billing_flags_ = parsed.billing_flags;
    billing_rested_ = parsed.billing_rested;
    expansion_level_ = parsed.expansion_level;
  }

  if (parsed.has_queue_position) {
    queue_position_ = parsed.queue_position;
    free_char_migration_ = parsed.free_character_migration;
  }

  if (callbacks_.on_auth_response) {
    callbacks_.on_auth_response(static_cast<int>(parsed.result_code));
  }

  return {.result = 1, .consumed = consumed};
}

RealmConnection::MessageDispatchResult RealmConnection::HandleAddonInfo(
    const std::uint8_t* payload, const std::size_t size) {
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "RealmConnection: SMSG_ADDON_INFO");

  std::size_t consumed = 0;
  (void)RealmAddonHandshakeState::Instance().ProcessServerInfo(payload, size,
                                                               &consumed);

  if (callbacks_.on_addon_info) {
    callbacks_.on_addon_info();
  }
  return {.result = 1, .consumed = consumed};
}

RealmConnection::MessageDispatchResult
RealmConnection::HandleClientCacheVersion(const std::uint8_t* payload,
                                          const std::size_t size) {
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "RealmConnection: SMSG_CLIENTCACHE_VERSION");

  std::uint32_t version =
      static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(this));
  if (size >= sizeof(version)) {
    std::memcpy(&version, payload, sizeof(version));
  }

  if (client_cache_version_callback_) {
    client_cache_version_callback_(version);
  }
  return {.result = 1,
          .consumed = size >= sizeof(version) ? sizeof(version) : 0};
}

RealmConnection::MessageDispatchResult RealmConnection::HandleCharCreate(
    const std::uint8_t* payload, const std::size_t size) {
  const std::uint8_t result_code = ReadSingleByteForwardValue(payload, size);
  if (callbacks_.on_char_create) {
    callbacks_.on_char_create(result_code);
  }
  return {.result = 1, .consumed = SingleByteReadConsumedSize(size)};
}

RealmConnection::MessageDispatchResult RealmConnection::HandleCharDelete(
    const std::uint8_t* payload, const std::size_t size) {
  const std::uint8_t result_code = ReadSingleByteForwardValue(payload, size);
  if (callbacks_.on_char_delete) {
    callbacks_.on_char_delete(result_code);
  }
  return {.result = 1, .consumed = SingleByteReadConsumedSize(size)};
}

RealmConnection::MessageDispatchResult RealmConnection::HandleCharLoginFailed(
    const std::uint8_t* payload, const std::size_t size) {
  const std::uint8_t reason = ReadSingleByteForwardValue(payload, size);
  if (callbacks_.on_char_login_failed) {
    callbacks_.on_char_login_failed(reason);
  }
  return {.result = 1, .consumed = SingleByteReadConsumedSize(size)};
}

RealmConnection::MessageDispatchResult RealmConnection::HandleLogoutResponse(
    const std::uint8_t* payload, const std::size_t size) {
  RealmConnectionLogoutResponsePayload parsed;
  std::size_t consumed = 0;
  ParseRealmConnectionLogoutResponse(payload, size, parsed, &consumed);

  if (callbacks_.on_logout_response) {
    callbacks_.on_logout_response(parsed.result, parsed.instant_flag);
  }
  return {.result = 1, .consumed = consumed};
}

void RealmConnection::ClearCharacterList() {
  std::vector<RealmConnectionCharEnumEntry>().swap(characters_);
}

RealmConnection::MessageDispatchResult RealmConnection::HandleCharEnum(
    const std::uint8_t* payload, const std::size_t size) {
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "RealmConnection: SMSG_CHAR_ENUM");

  RealmConnectionCharEnumPayload parsed;
  std::size_t consumed = 0;
  const bool ok = ParseRealmConnectionCharEnum(payload, size, parsed,
                                               &consumed);

  if (ok) {
    characters_ = std::move(parsed.characters);
    if (parsed.has_trailing_u32s) {
      trailing_u32s_ = parsed.trailing_u32s;
    }
  } else {
    ClearCharacterList();
  }

  if (callbacks_.on_char_enum) {
    callbacks_.on_char_enum(ok);
  }

  return {.result = 1, .consumed = consumed};
}

void RealmConnection::SendCharEnum() {
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kDebug,
                     "RealmConnection: Sending CMSG_CHAR_ENUM (opcode 55)");

}

void RealmConnection::SendPlayerLogin(const std::uint64_t guid) {
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kDebug,
                     "RealmConnection: Sending CMSG_PLAYER_LOGIN (opcode 61) "
                     "guid=" + std::to_string(guid));

}

void RealmConnection::SetCallbacks(RealmConnectionCallbacks cbs) {
  callbacks_ = std::move(cbs);
}

void RealmConnection::SetOnCharEnumCallback(std::function<void(bool success)> callback) {
  callbacks_.on_char_enum = std::move(callback);
}

void RealmConnection::SetQueuedEventDrainHandler(std::function<void()> handler) {
  queued_event_drain_handler_ = std::move(handler);
}

void RealmConnection::DrainQueuedEvents() {
  if (queued_event_drain_handler_) {
    queued_event_drain_handler_();
  }
}

}
