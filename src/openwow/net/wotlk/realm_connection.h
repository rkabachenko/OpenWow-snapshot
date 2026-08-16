
#pragma once

#include "openwow/net/wotlk/wow_client_connection.h"
#include "openwow/net/wotlk/protocol/realm_connection_packets.h"

#include <array>
#include <cstdint>
#include <functional>
#include <cstddef>
#include <string>
#include <vector>

namespace openwow::net::wotlk {

namespace realm_opcodes {
constexpr std::uint16_t SMSG_CHAR_CREATE            = 58;
constexpr std::uint16_t SMSG_CHAR_ENUM              = 59;
constexpr std::uint16_t SMSG_CHAR_DELETE             = 60;
constexpr std::uint16_t SMSG_CHAR_LOGIN_FAILED       = 65;
constexpr std::uint16_t SMSG_LOGOUT_RESPONSE         = 76;
constexpr std::uint16_t SMSG_LOGOUT_COMPLETE         = 77;
constexpr std::uint16_t SMSG_LOGOUT_CANCEL_ACK       = 79;
constexpr std::uint16_t SMSG_AUTH_CHALLENGE          = 492;
constexpr std::uint16_t SMSG_AUTH_RESPONSE           = 494;
constexpr std::uint16_t SMSG_ADDON_INFO              = 751;
constexpr std::uint16_t SMSG_CLIENTCACHE_VERSION     = 1195;
constexpr std::uint16_t CMSG_CHAR_ENUM               = 55;
constexpr std::uint16_t CMSG_PLAYER_LOGIN            = 61;
constexpr std::uint16_t CMSG_AUTH_SESSION             = 493;
}

struct RealmConnectionCallbacks {

  std::function<void(int result_code)> on_auth_response;

  std::function<void()> on_addon_info;

  std::function<void(bool success)> on_char_enum;

  std::function<void()> on_logout_complete;

  std::function<void()> on_logout_cancel_ack;

  std::function<void()> on_connected;

  std::function<void()> on_disconnected;

  std::function<void()> on_cant_connect;

  std::function<void(std::uint8_t code)> on_char_create;

  std::function<void(std::uint8_t code)> on_char_delete;

  std::function<void(std::uint8_t reason)> on_char_login_failed;

  std::function<void(std::uint32_t result, std::uint8_t instant_flag)>
      on_logout_response;
};

class RealmConnection {
 public:
  RealmConnection();
  ~RealmConnection();

  void SetAuthSessionSeedWords(std::uint32_t seed0,
                               std::uint32_t seed1,
                               std::uint32_t seed2);

  void OnConnected();

  void OnDisconnected();

  void OnCantConnect();

  int MessageHandler(std::uint16_t opcode,
                     const std::uint8_t* payload,
                     std::size_t size);

  void SendCharEnum();

  void SendPlayerLogin(std::uint64_t guid);

  void SetCallbacks(RealmConnectionCallbacks cbs);
  void SetOnCharEnumCallback(std::function<void(bool success)> callback);

  static void DrainActiveQueuedEvents();

  [[nodiscard]] static std::size_t ActiveInstanceCount();
  [[nodiscard]] static const RealmConnection *GetActiveInstance();
  [[nodiscard]] static bool IsActiveInstance(const RealmConnection* instance);

  void SetQueuedEventDrainHandler(std::function<void()> handler);
  void SetClientCacheVersionCallback(
      std::function<void(std::uint32_t)> callback) {
    client_cache_version_callback_ = std::move(callback);
  }
  void DrainQueuedEvents();

  [[nodiscard]] bool IsAuthenticated() const { return authenticated_; }
  [[nodiscard]] openwow::net::WowClientState client_state() const {
    return client_state_;
  }
  [[nodiscard]] std::uint32_t queue_position() const { return queue_position_; }
  void SetQueuePositionForTesting(std::uint32_t pos) { queue_position_ = pos; }
  [[nodiscard]] std::uint32_t free_char_migration() const { return free_char_migration_; }
  [[nodiscard]] std::uint32_t billing_time() const { return billing_time_; }
  [[nodiscard]] std::uint32_t billing_rested() const { return billing_rested_; }
  [[nodiscard]] std::uint8_t billing_flags() const { return billing_flags_; }
  [[nodiscard]] std::uint8_t expansion_level() const { return expansion_level_; }
  [[nodiscard]] const std::array<std::uint32_t, 3>& seed_words() const {
    return seed_words_;
  }
  [[nodiscard]] const std::vector<RealmConnectionCharEnumEntry>& characters() const {
    return characters_;
  }
  [[nodiscard]] const std::array<std::uint32_t, 10>& trailing_u32s() const {
    return trailing_u32s_;
  }

  static constexpr std::array<std::uint16_t, 11> kRegisteredOpcodes = {
      realm_opcodes::SMSG_AUTH_CHALLENGE,
      realm_opcodes::SMSG_AUTH_RESPONSE,
      realm_opcodes::SMSG_ADDON_INFO,
      realm_opcodes::SMSG_CHAR_ENUM,
      realm_opcodes::SMSG_CHAR_CREATE,
      realm_opcodes::SMSG_CHAR_LOGIN_FAILED,
      realm_opcodes::SMSG_LOGOUT_COMPLETE,
      realm_opcodes::SMSG_LOGOUT_CANCEL_ACK,
      realm_opcodes::SMSG_LOGOUT_RESPONSE,
      realm_opcodes::SMSG_CHAR_DELETE,
      realm_opcodes::SMSG_CLIENTCACHE_VERSION
  };

 private:
  struct MessageDispatchResult {
    int result = 0;
    std::size_t consumed = 0;
  };

  MessageDispatchResult HandleAuthResponse(const std::uint8_t* payload,
                                           std::size_t size);

  MessageDispatchResult HandleAddonInfo(const std::uint8_t* payload,
                                        std::size_t size);

  MessageDispatchResult HandleClientCacheVersion(const std::uint8_t* payload,
                                                 std::size_t size);

  MessageDispatchResult HandleCharCreate(const std::uint8_t* payload,
                                         std::size_t size);

  MessageDispatchResult HandleCharDelete(const std::uint8_t* payload,
                                         std::size_t size);

  MessageDispatchResult HandleCharLoginFailed(const std::uint8_t* payload,
                                              std::size_t size);

  MessageDispatchResult HandleLogoutResponse(const std::uint8_t* payload,
                                             std::size_t size);

  MessageDispatchResult HandleCharEnum(const std::uint8_t* payload,
                                       std::size_t size);

  void ClearCharacterList();
  void ResetState();

  RealmConnectionCallbacks callbacks_;
  openwow::net::WowClientState client_state_{openwow::net::kStateUninitialized};
  bool authenticated_{false};
  std::uint32_t queue_position_{0};
  std::uint32_t free_char_migration_{0};
  std::uint32_t billing_time_{0};
  std::uint32_t billing_rested_{0};
  std::uint8_t billing_flags_{0};
  std::uint8_t expansion_level_{0};
  std::array<std::uint32_t, 3> seed_words_{};
  std::vector<RealmConnectionCharEnumEntry> characters_;
  std::array<std::uint32_t, 10> trailing_u32s_{};

  std::function<void()> queued_event_drain_handler_;
  std::function<void(std::uint32_t)> client_cache_version_callback_;
};

}
