
#include "openwow/net/wotlk/protocol/world_protocol.h"

#include "openwow/runtime/time/game_clock.h"
#include "openwow/data/db_cache_instances.h"
#include "openwow/network/protocol/wotlk/opcodes.h"
#include "openwow/net/wotlk/glue_startup_handlers.h"
#include "openwow/net/wotlk/protocol/realm_connection_packets.h"
#include "openwow/net/wotlk/protocol/packet_sender.h"
#include "openwow/net/wotlk/protocol/world_header_crypto.h"
#include "openwow/net/wotlk/proof_of_work.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/foundation/hashing/retail_adler_seed.h"

#include <openssl/sha.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <limits>
#include <span>
#include <sstream>
#include <utility>

namespace openwow::net::wotlk {

std::uint32_t ComputeRealmAuthClientSeed(const std::uint32_t tick_count) {
  auto state =
      openwow::foundation::hashing::MakeAdlerSeedState(tick_count);
  return openwow::foundation::hashing::AdvanceAdlerSeed(state);
}

std::array<std::uint8_t, 20> ComputeRealmAuthSessionProof(
    const std::string& account_name,
    const std::uint32_t client_seed,
    const std::uint32_t auth_seed,
    const std::array<std::uint8_t, 40>& session_key) {
  const auto update_u32_le = [](SHA_CTX& sha, const std::uint32_t value) {
    const std::array<std::uint8_t, 4> bytes = {
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8U),
        static_cast<std::uint8_t>(value >> 16U),
        static_cast<std::uint8_t>(value >> 24U),
    };
    SHA1_Update(&sha, bytes.data(), bytes.size());
  };

  SHA_CTX sha;
  SHA1_Init(&sha);
  SHA1_Update(&sha, account_name.data(), account_name.size());
  update_u32_le(sha, 0);
  update_u32_le(sha, client_seed);
  update_u32_le(sha, auth_seed);
  SHA1_Update(&sha, session_key.data(), session_key.size());

  std::array<std::uint8_t, 20> proof{};
  SHA1_Final(proof.data(), &sha);
  return proof;
}

namespace {

constexpr std::size_t kRedirectAddressBytes = 4;
constexpr std::size_t kRedirectPortBytes = 2;
constexpr std::size_t kRedirectCookieBytes = 4;
constexpr std::size_t kRedirectPrefixBytes =
    kRedirectAddressBytes + kRedirectPortBytes + kRedirectCookieBytes;
constexpr std::size_t kRedirectDigestBytes = 20;
constexpr std::size_t kRedirectPayloadBytes =
    kRedirectPrefixBytes + kRedirectDigestBytes;
constexpr std::size_t kChallengeWordsOffset = 8;

constexpr std::uint32_t kSecondaryConnectTimeoutMs =
    std::numeric_limits<std::uint32_t>::max();

constexpr std::size_t kCharacterServiceResultBytes = sizeof(std::uint8_t);

constexpr std::size_t kCharacterServiceNameBufferBytes = 0x30;

std::string FormatOpcodeHex(const Opcode opcode) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%04X", OpcodeValue(opcode));
  return std::string(buf);
}

void LogRealmMessageUnderRead(const char* scope,
                              const Opcode opcode,
                              const std::size_t read,
                              const std::size_t size) {
  openwow::diagnostics::Log(
      openwow::diagnostics::LogLevel::kWarn,
      std::string(scope) + ": RealmConnection::MessageHandler Message Under Read! ID:" +
          std::to_string(OpcodeValue(opcode)) + " Read:" + std::to_string(read) +
          " Size:" + std::to_string(size));
}

bool ParseRealmAddress(const std::string& address, std::string* host, std::uint16_t* port) {
  const auto sep = address.rfind(':');
  if (sep == std::string::npos || sep == 0 || sep + 1 >= address.size()) return false;
  const std::string port_text = address.substr(sep + 1);
  int parsed_port = 0;
  const auto [ptr, ec] = std::from_chars(port_text.data(), port_text.data() + port_text.size(), parsed_port);
  if (ec != std::errc() || ptr != (port_text.data() + port_text.size()) || parsed_port <= 0 || parsed_port > 65535) return false;
  *host = address.substr(0, sep);
  *port = static_cast<std::uint16_t>(parsed_port);
  return true;
}

std::uint16_t ReadU16Le(const std::span<const std::uint8_t> bytes,
                        const std::size_t offset) {
  return static_cast<std::uint16_t>(bytes[offset]) |
         static_cast<std::uint16_t>(bytes[offset + 1] << 8U);
}

std::uint32_t ReadU32Le(const std::span<const std::uint8_t> bytes,
                        const std::size_t offset) {
  return static_cast<std::uint32_t>(bytes[offset]) |
         (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U) |
         (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U) |
         (static_cast<std::uint32_t>(bytes[offset + 3]) << 24U);
}

std::string FormatRedirectAddress(const std::uint32_t address_v4) {
  return std::to_string(address_v4 & 0xFFU) + "." +
         std::to_string((address_v4 >> 8U) & 0xFFU) + "." +
         std::to_string((address_v4 >> 16U) & 0xFFU) + "." +
         std::to_string((address_v4 >> 24U) & 0xFFU);
}

std::string GetEnvOrDefault(const char* key, const std::string& fallback) {
  const char* value = std::getenv(key);
  return (value && value[0] != '\0') ? std::string(value) : fallback;
}

bool HexToBytes(const std::string& hex, std::uint8_t* out, std::size_t out_len) {
  if (hex.size() != out_len * 2) return false;
  for (std::size_t i = 0; i < out_len; ++i) {
    char h = hex[2 * i], l = hex[2 * i + 1];
    auto nibble = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return 10 + c - 'a';
      if (c >= 'A' && c <= 'F') return 10 + c - 'A';
      return -1;
    };
    int hi = nibble(h), lo = nibble(l);
    if (hi < 0 || lo < 0) return false;
    out[i] = static_cast<std::uint8_t>((hi << 4) | lo);
  }
  return true;
}

struct ParsedCharacterServicePayload {
  std::uint64_t guid{0};
  std::string name;
  std::uint8_t gender{0};
  std::uint8_t skin{0};
  std::uint8_t hair_style{0};
  std::uint8_t hair_color{0};
  std::uint8_t facial_hair{0};
  std::uint8_t face{0};
  std::uint8_t race{0};
};

bool ReadCharacterServiceCString(const std::vector<std::uint8_t>& payload,
                                 std::size_t& offset,
                                 const std::size_t max_bytes_including_nul,
                                 std::string& out) {
  out.clear();
  std::size_t count = 0;
  while (offset < payload.size() && count < max_bytes_including_nul) {
    const char c = static_cast<char>(payload[offset++]);
    ++count;
    if (c == '\0') {
      return true;
    }
    out.push_back(c);
  }

  out.clear();
  offset = payload.size();
  return false;
}

std::uint8_t ReadOptionalU8(const std::vector<std::uint8_t>& payload,
                            std::size_t& offset) {
  if (offset >= payload.size()) {
    offset = payload.size();
    return 0;
  }
  return payload[offset++];
}

std::uint64_t ReadOptionalU64(const std::vector<std::uint8_t>& payload,
                              std::size_t& offset) {
  if (offset + sizeof(std::uint64_t) > payload.size()) {
    offset = payload.size();
    return 0;
  }

  std::uint64_t value = 0;
  for (int i = 0; i < 8; ++i) {
    value |= static_cast<std::uint64_t>(payload[offset + static_cast<std::size_t>(i)]) << (8 * i);
  }
  offset += sizeof(std::uint64_t);
  return value;
}

ParsedCharacterServicePayload ParseCharacterServicePayload(
    const std::vector<std::uint8_t>& payload,
    const bool include_race) {
  ParsedCharacterServicePayload parsed;
  std::size_t offset = kCharacterServiceResultBytes;

  parsed.guid = ReadOptionalU64(payload, offset);
  (void)ReadCharacterServiceCString(
      payload, offset, kCharacterServiceNameBufferBytes, parsed.name);

  parsed.gender = ReadOptionalU8(payload, offset);
  parsed.skin = ReadOptionalU8(payload, offset);
  parsed.hair_style = ReadOptionalU8(payload, offset);
  parsed.hair_color = ReadOptionalU8(payload, offset);
  parsed.facial_hair = ReadOptionalU8(payload, offset);
  parsed.face = ReadOptionalU8(payload, offset);
  if (include_race) {
    parsed.race = ReadOptionalU8(payload, offset);
  }
  return parsed;
}

}

RealmSession::RealmSession()
    : primary_stream_(std::make_shared<RealmStream>()) {}

RealmSession::~RealmSession() {
  secondary_stop_requested_.store(true, std::memory_order_release);
  const auto primary = PrimaryStream();
  const auto secondary = SecondaryStream();
  if (primary) {
    primary->client.Disconnect();
  }
  if (secondary) {
    secondary->client.Disconnect();
  }
  JoinSecondaryReceiveThread();
}

void RealmSession::SetSessionKey(const std::uint8_t key[40]) {
  std::memcpy(session_key_.data(), key, 40);
  has_session_key_ = true;
}

void RealmSession::SetSessionKey(const std::array<std::uint8_t, 40>& key) {
  session_key_ = key;
  has_session_key_ = true;
}

void RealmSession::SetAccountName(const std::string& account_name) {
  account_name_ = account_name;

  for (auto& c : account_name_) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
}

void RealmSession::SetSessionToken(const std::string& session_token) {
  session_token_ = session_token;

  if (session_token.size() == 80) {
    std::uint8_t key[40]{};
    if (HexToBytes(session_token, key, 40)) {
      SetSessionKey(key);
    }
  }
}

const std::string& RealmSession::session_token() const {
  return session_token_;
}

void RealmSession::SetClientSeedProvider(
    std::function<std::uint32_t()> provider) {
  client_seed_provider_ = std::move(provider);
}

void RealmSession::SetRealmRoutingIds(const RealmRoutingIds routing_ids) {
  realm_routing_ids_ = routing_ids;
}

void RealmSession::SetAuthSessionSeedWords(const std::uint32_t seed0,
                                           const std::uint32_t seed1,
                                           const std::uint32_t seed2) {
  SetRealmRoutingIds({seed0, seed1, seed2});
}

void RealmSession::SetAddonInfoProcessedCallback(std::function<void()> callback) {
  addon_info_processed_callback_ = std::move(callback);
}

bool RealmSession::Connect(const RealmInfo& realm, std::uint32_t timeout_ms) {
  Disconnect();
  std::string host;
  std::uint16_t port = 0;
  if (!ParseRealmAddress(realm.address, &host, &port)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kError, "RealmSession: invalid address: " + realm.address);
    return false;
  }
  const auto primary = PrimaryStream();
  if (!primary || !primary->client.Connect(host, port, timeout_ms)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kError, "RealmSession: TCP connect failed to " + realm.address);
    return false;
  }
  realm_ = realm;
  connected_.store(true, std::memory_order_release);
  authenticated_.store(false, std::memory_order_release);
  phase_.store(RealmSessionPhase::kConnected, std::memory_order_release);
  addon_info_received_ = false;
  client_cache_version_ = 0;
  char_enum_trailing_u32s_.fill(0);
  ResetReceiveState();

  ResetHeartbeat(openwow::core::GameClock::GetTickCount32());
  return true;
}

bool RealmSession::connected() const {
  const auto primary = PrimaryStream();
  return connected_.load(std::memory_order_acquire) && primary &&
         primary->client.IsConnected();
}
const RealmInfo& RealmSession::realm() const { return realm_; }

void RealmSession::CancelPendingIo() {
  secondary_stop_requested_.store(true, std::memory_order_release);
  const bool was_connected =
      connected_.exchange(false, std::memory_order_acq_rel);
  const auto primary = PrimaryStream();
  const auto secondary = SecondaryStream();
  if (primary) {
    primary->client.Disconnect();
  }
  if (secondary) {
    secondary->client.Disconnect();
  }
  JoinSecondaryReceiveThread();
  authenticated_.store(false, std::memory_order_release);
  phase_.store(RealmSessionPhase::kDisconnected, std::memory_order_release);
  if (was_connected && disconnected_callback_) {
    disconnected_callback_();
  }
}

void RealmSession::Disconnect() {
  CancelPendingIo();
  addon_info_received_ = false;
  client_cache_version_ = 0;
  pending_character_guid_ = 0;
  char_enum_trailing_u32s_.fill(0);
  {
    std::scoped_lock send_lock(send_mutex_);
    const auto primary = PrimaryStream();
    if (primary) {
      primary->crypt = {};
    }
    transfer_pending_ = false;
    held_outbound_packets_.clear();
  }
  {
    std::scoped_lock transfer_lock(transfer_mutex_);
    secondary_control_packets_.clear();
    secondary_disconnected_ = false;
    transfer_cookie_ = 0;
  }
  secondary_event_pending_.store(false, std::memory_order_release);
  secondary_stop_requested_.store(false, std::memory_order_release);
  {
    std::scoped_lock stream_lock(stream_mutex_);
    secondary_stream_.reset();
  }
  ResetReceiveState();
  ResetHeartbeat();
}

void RealmSession::ResetHeartbeat(const std::uint32_t baseline_tick_ms) {
  std::scoped_lock lock(heartbeat_mutex_);
  heartbeat_last_send_tick_ms_ = baseline_tick_ms;
  heartbeat_sequence_ = 0;
  heartbeat_acknowledged_sequence_ = 0;
  heartbeat_samples_.fill(0);
  heartbeat_sample_begin_ = 0;
  heartbeat_sample_write_ = 0;
  heartbeat_sample_count_ = 0;
  ping_sent_callback_ = {};
}

RealmHeartbeatSnapshot RealmSession::heartbeat_snapshot() const {
  std::scoped_lock lock(heartbeat_mutex_);
  std::uint64_t latency_sum = 0;
  for (std::size_t i = 0; i < heartbeat_sample_count_; ++i) {
    latency_sum += heartbeat_samples_[
        (heartbeat_sample_begin_ + i) % heartbeat_samples_.size()];
  }
  return {
      .sequence = heartbeat_sequence_,
      .last_send_tick_ms = heartbeat_last_send_tick_ms_,
      .average_latency_ms = heartbeat_sample_count_ == 0
          ? 0u
          : static_cast<std::uint32_t>(latency_sum / heartbeat_sample_count_),
      .sample_count = static_cast<std::uint32_t>(heartbeat_sample_count_),
  };
}

void RealmSession::SetPingSentCallback(
    std::function<void(std::uint32_t, std::uint32_t)> callback) {
  std::scoped_lock lock(heartbeat_mutex_);
  ping_sent_callback_ = std::move(callback);
}

bool RealmSession::ServiceHeartbeat(const std::uint32_t now_tick_ms) {
  const auto current_phase = phase();
  if (!connected() || current_phase == RealmSessionPhase::kDisconnected) {
    return false;
  }

  std::uint32_t sequence = 0;
  std::uint32_t reported_latency_ms = 0;
  std::function<void(std::uint32_t, std::uint32_t)> observer;
  bool response_overdue = false;
  {
    std::scoped_lock lock(heartbeat_mutex_);
    if (static_cast<std::int32_t>(
            now_tick_ms - heartbeat_last_send_tick_ms_ -
            kHeartbeatIntervalMs) < 0) {
      return false;
    }

    response_overdue =
        current_phase == RealmSessionPhase::kWorld &&
        heartbeat_sequence_ != heartbeat_acknowledged_sequence_;
    if (!response_overdue) {

      reported_latency_ms = heartbeat_sample_write_ == 0
          ? 0u
          : heartbeat_samples_[heartbeat_sample_write_ - 1];

      sequence = ++heartbeat_sequence_;
      heartbeat_last_send_tick_ms_ = now_tick_ms;
      observer = ping_sent_callback_;
    }
  }

  if (response_overdue) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "RealmSession: world heartbeat response overdue; closing dead stream");
    CancelPendingIo();
    return false;
  }

  const bool sent =
      SendPacket(PacketSender::BuildPing(sequence, reported_latency_ms));
  if (sent && observer) {
    observer(sequence, now_tick_ms);
  }
  return sent;
}

bool RealmSession::ObserveConnectionPacket(const WorldPacket& packet,
                                           const std::uint32_t now_tick_ms) {
  if (!packet.IsOpcode(Opcode::SMSG_PONG)) {
    return false;
  }
  if (packet.payload.size() < sizeof(std::uint32_t)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "RealmSession: malformed SMSG_PONG");
    return true;
  }

  std::uint32_t sequence = 0;
  std::memcpy(&sequence, packet.payload.data(), sizeof(sequence));
  std::scoped_lock lock(heartbeat_mutex_);
  if (sequence != heartbeat_sequence_ || heartbeat_last_send_tick_ms_ == 0) {
    return true;
  }

  heartbeat_acknowledged_sequence_ = sequence;

  const std::uint32_t round_trip_ms =
      now_tick_ms - heartbeat_last_send_tick_ms_;
  heartbeat_samples_[heartbeat_sample_write_] = round_trip_ms;
  const auto next_write =
      (heartbeat_sample_write_ + 1) % heartbeat_samples_.size();
  if (next_write == heartbeat_sample_begin_) {

    heartbeat_sample_begin_ =
        (heartbeat_sample_begin_ + 1) % heartbeat_samples_.size();
  } else {
    ++heartbeat_sample_count_;
  }
  heartbeat_sample_write_ = next_write;
  return true;
}

bool RealmSession::ReturnToCharacterSelect() {
  if (!connected() || !authenticated()) {
    return false;
  }
  if (phase() == RealmSessionPhase::kCharacterSelect) {
    return true;
  }
  auto expected = RealmSessionPhase::kWorld;
  return phase_.compare_exchange_strong(
      expected, RealmSessionPhase::kCharacterSelect,
      std::memory_order_acq_rel, std::memory_order_acquire);
}

bool RealmSession::CanPerformCharacterOperation() const {
  return connected() && authenticated() &&
         phase() == RealmSessionPhase::kCharacterSelect;
}

void RealmSession::RestoreCharacterSelectAfterFailedWorldEntry() {
  phase_.store(connected() && authenticated()
                   ? RealmSessionPhase::kCharacterSelect
                   : RealmSessionPhase::kDisconnected,
               std::memory_order_release);
}

WorldAuthResult RealmSession::Authenticate(
    const std::uint32_t timeout_ms,
    WorldAuthProgressCallback progress_callback,
    const std::function<bool()>& should_cancel) {
  std::unique_lock operation_lock(operation_mutex_);
  if (!connected() || phase() != RealmSessionPhase::kConnected) {
    return {.status = WorldAuthStatus::kNetworkError, .message = "Not connected."};
  }
  if (!has_session_key_) {
    return {.status = WorldAuthStatus::kAuthFailed, .message = "No session key set."};
  }

  phase_.store(RealmSessionPhase::kAuthenticating,
               std::memory_order_release);
  WorldAuthResult retained_auth_fields{};
  const auto with_retained_account_fields =
      [&retained_auth_fields](WorldAuthResult result) {
        if (!result.has_account_info && retained_auth_fields.has_account_info) {
          result.has_account_info = true;
          result.billing_time = retained_auth_fields.billing_time;
          result.billing_flags = retained_auth_fields.billing_flags;
          result.billing_rested = retained_auth_fields.billing_rested;
          result.expansion_level = retained_auth_fields.expansion_level;
        }
        return result;
      };
  const auto fail = [this](WorldAuthResult result) {
    authenticated_.store(false, std::memory_order_release);
    phase_.store(connected() ? RealmSessionPhase::kConnected
                             : RealmSessionPhase::kDisconnected,
                 std::memory_order_release);
    return result;
  };
  const auto cancelled =
      [this, &fail, &with_retained_account_fields](const char* const message) {

    CancelPendingIo();
    return fail(with_retained_account_fields(
        {.status = WorldAuthStatus::kCancelled, .message = message}));
  };
  const auto connection_failure =
      [this, &fail, &with_retained_account_fields](WorldAuthResult result) {

    CancelPendingIo();
    return fail(with_retained_account_fields(std::move(result)));
  };

  WorldPacket challenge_pkt;
  if (!ReceiveExpectedPacket({Opcode::SMSG_AUTH_CHALLENGE}, challenge_pkt,
                             timeout_ms, "RealmSession::Authenticate",
                             should_cancel)) {
    if (should_cancel && should_cancel()) {
      return cancelled("World authentication cancelled.");
    }
    return connection_failure(
        {.status = WorldAuthStatus::kNetworkError,
         .message = "No SMSG_AUTH_CHALLENGE received."});
  }
  const auto challenge = ParseRealmAuthChallenge(challenge_pkt.payload);
  if (!challenge.has_value()) {
    return connection_failure(
        {.status = WorldAuthStatus::kAuthFailed,
         .message = "Malformed SMSG_AUTH_CHALLENGE."});
  }
  const auto proof_of_work_nonce =
      SolveRealmProofOfWork(account_name_, *challenge);
  if (!proof_of_work_nonce.has_value()) {
    return connection_failure(
        {.status = WorldAuthStatus::kAuthFailed,
         .message = "SMSG_AUTH_CHALLENGE proof of work failed."});
  }

  const std::uint32_t client_seed = client_seed_provider_
      ? client_seed_provider_()
      : ComputeRealmAuthClientSeed(
            openwow::core::GameClock::GetTickCount32());

  const auto client_proof = ComputeRealmAuthSessionProof(
      account_name_, client_seed, challenge->auth_seed, session_key_);

  AuthSessionPayload auth_payload{};
  auth_payload.build = 12340;
  auth_payload.account_name = account_name_;
  auth_payload.client_seed = client_seed;
  auth_payload.region_id = realm_routing_ids_.region_id;
  auth_payload.battlegroup_id = realm_routing_ids_.battlegroup_id;
  auth_payload.realm_id = realm_routing_ids_.realm_id;
  auth_payload.proof_of_work_nonce = *proof_of_work_nonce;
  const auto addon_data =
      RealmAddonHandshakeState::Instance().BuildSerializedClientInfo();
  const bool account_is_canonical =
      std::none_of(account_name_.begin(), account_name_.end(), [](const unsigned char ch) {
        return std::islower(ch) != 0;
      });
  const auto auth_session = PacketSender::BuildAuthSession(
      auth_payload, client_proof.data(), addon_data);
  const auto raw = auth_session.SerializeClientFrame();
  const std::uint32_t addon_uncompressed_bytes = addon_data.size() >= 4
      ? static_cast<std::uint32_t>(addon_data[0]) |
            (static_cast<std::uint32_t>(addon_data[1]) << 8U) |
            (static_cast<std::uint32_t>(addon_data[2]) << 16U) |
            (static_cast<std::uint32_t>(addon_data[3]) << 24U)
      : 0U;
  const bool addon_has_zlib_header =
      addon_data.size() >= 6 && (addon_data[4] & 0x0FU) == 8U;
  openwow::diagnostics::Log(
      openwow::diagnostics::LogLevel::kInfo,
      "RealmSession: CMSG_AUTH_SESSION metadata build=12340"
      " login_server_id=0 login_server_type=0 routing=" +
          std::to_string(auth_payload.region_id) + "/" +
          std::to_string(auth_payload.battlegroup_id) + "/" +
          std::to_string(auth_payload.realm_id) +
          " account_canonical=" + (account_is_canonical ? "true" : "false") +
          " pow_difficulty=" +
          std::to_string(challenge->proof_of_work_difficulty) +
          " addon_bytes=" + std::to_string(addon_data.size()) +
          " addon_uncompressed_bytes=" +
          std::to_string(addon_uncompressed_bytes) +
          " addon_zlib_header=" +
          (addon_has_zlib_header ? "true" : "false") +
          " payload_bytes=" + std::to_string(auth_session.payload.size()) +
          " wire_bytes=" + std::to_string(raw.size()));
  bool auth_session_written = false;
  {
    std::scoped_lock send_lock(send_mutex_);
    const auto primary = PrimaryStream();
    if (primary) {
      auth_session_written =
          primary->client.Write(raw, timeout_ms, should_cancel);
    }
    if (auth_session_written) {

      primary->crypt.Init(session_key_.data());
    }
  }
  if (!auth_session_written) {
    if (should_cancel && should_cancel()) {
      return cancelled("World authentication cancelled.");
    }
    return connection_failure(
        {.status = WorldAuthStatus::kNetworkError,
         .message = "Failed to send CMSG_AUTH_SESSION."});
  }
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "RealmSession: CMSG_AUTH_SESSION write completed");

  WorldPacket auth_response;
  if (!ReceiveExpectedPacket({Opcode::SMSG_AUTH_RESPONSE}, auth_response,
                             timeout_ms, "RealmSession::Authenticate",
                             should_cancel)) {
    if (should_cancel && should_cancel()) {
      return cancelled("World authentication cancelled.");
    }
    return connection_failure(
        {.status = WorldAuthStatus::kNetworkError,
         .message = "No SMSG_AUTH_RESPONSE received."});
  }
  openwow::diagnostics::Log(
      openwow::diagnostics::LogLevel::kInfo,
      "RealmSession: SMSG_AUTH_RESPONSE received payload_bytes=" +
          std::to_string(auth_response.payload.size()));

  for (;;) {
    auto result =
        ParseAuthResponse(auth_response.payload.data(), auth_response.payload.size());
    if (result.has_account_info) {
      retained_auth_fields.has_account_info = true;
      retained_auth_fields.billing_time = result.billing_time;
      retained_auth_fields.billing_flags = result.billing_flags;
      retained_auth_fields.billing_rested = result.billing_rested;
      retained_auth_fields.expansion_level = result.expansion_level;
    } else {
      result = with_retained_account_fields(std::move(result));
    }
    if (result.has_queue_position) {
      retained_auth_fields.queue_position = result.queue_position;
      retained_auth_fields.free_character_migration =
          result.free_character_migration;
    } else if (result.status == WorldAuthStatus::kQueuePosition) {
      result.queue_position = retained_auth_fields.queue_position;
      result.free_character_migration =
          retained_auth_fields.free_character_migration;
    }
    if (result.status == WorldAuthStatus::kSuccess) {
      authenticated_.store(true, std::memory_order_release);
      phase_.store(RealmSessionPhase::kCharacterSelect,
                   std::memory_order_release);
      return result;
    }

    if (result.status != WorldAuthStatus::kQueuePosition) {
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kWarn,
          "RealmSession: SMSG_AUTH_RESPONSE rejected code=" +
              std::to_string(result.result_code) + " routing=" +
              std::to_string(realm_routing_ids_.region_id) + "/" +
              std::to_string(realm_routing_ids_.battlegroup_id) + "/" +
              std::to_string(realm_routing_ids_.realm_id));
      return fail(std::move(result));
    }

    phase_.store(RealmSessionPhase::kQueued, std::memory_order_release);

    if (progress_callback) {
      progress_callback(result);
    }

    if (!ReceiveExpectedPacket({Opcode::SMSG_AUTH_RESPONSE}, auth_response,
                               0,
                               "RealmSession::AuthenticateQueue",
                               should_cancel)) {
      if (should_cancel && should_cancel()) {
        return cancelled("Realm queue wait cancelled.");
      }
      return fail(with_retained_account_fields(
          {.status = WorldAuthStatus::kNetworkError,
           .message = "Connection lost while waiting in the realm queue."}));
    }
  }
}

WorldAuthResult RealmSession::ParseAuthResponse(const std::uint8_t* payload,
                                                std::size_t size) {
  RealmConnectionAuthResponsePayload parsed;
  if (!ParseRealmConnectionAuthResponse(payload, size, parsed)) {
    return {.status = WorldAuthStatus::kAuthFailed, .message = "Empty SMSG_AUTH_RESPONSE."};
  }

  if (parsed.result_code == AUTH_OK) {
    return {.status = WorldAuthStatus::kSuccess,
            .message = "World server authenticated.",
            .result_code = parsed.result_code,
            .has_account_info = parsed.has_account_info,
            .has_queue_position = parsed.has_queue_position,
            .expansion_level = parsed.expansion_level,
            .billing_time = parsed.billing_time,
            .billing_flags = parsed.billing_flags,
            .billing_rested = parsed.billing_rested,
            .free_character_migration = parsed.free_character_migration};
  }

  if (parsed.result_code == AUTH_WAIT_QUEUE) {
    return {.status = WorldAuthStatus::kQueuePosition,
            .message = "Position in queue: " + std::to_string(parsed.queue_position),
            .result_code = parsed.result_code,
            .has_account_info = parsed.has_account_info,
            .queue_position = parsed.queue_position,
            .has_queue_position = parsed.has_queue_position,
            .expansion_level = parsed.expansion_level,
            .billing_time = parsed.billing_time,
            .billing_flags = parsed.billing_flags,
            .billing_rested = parsed.billing_rested,
            .free_character_migration = parsed.free_character_migration};
  }

  return {.status = WorldAuthStatus::kAuthFailed,
          .message = "World auth failed (code=" + std::to_string(parsed.result_code) + ").",
          .result_code = parsed.result_code,
          .has_account_info = parsed.has_account_info,
          .has_queue_position = parsed.has_queue_position,
          .billing_time = parsed.billing_time,
          .billing_flags = parsed.billing_flags,
          .billing_rested = parsed.billing_rested,
          .free_character_migration = parsed.free_character_migration};
}

bool RealmSession::SendPacket(const WorldPacket& pkt) {
  if (!connected()) return false;
  std::scoped_lock send_lock(send_mutex_);
  if (!connected()) return false;
  if (transfer_pending_) {
    held_outbound_packets_.push_back(pkt);
    return true;
  }
  return SendPacketOnStream(PrimaryStream(), pkt);
}

bool RealmSession::SendPacketOnStream(
    const std::shared_ptr<RealmStream>& stream,
    const WorldPacket& packet,
    const std::uint32_t timeout_ms,
    const std::function<bool()>& should_cancel) {
  if (!stream || !stream->client.IsConnected()) {
    return false;
  }
  auto raw = packet.SerializeClientFrame();
  if (stream->crypt.IsInitialized()) {

    const std::size_t hdr_len = ClientFullHeaderLength(packet.payload.size());
    stream->crypt.EncryptSend(raw.data(), hdr_len);
  }
  const bool sent = stream->client.Write(raw, timeout_ms, should_cancel);
  if (!sent && stream == PrimaryStream() && !stream->client.IsConnected()) {
    connected_.store(false, std::memory_order_release);
    authenticated_.store(false, std::memory_order_release);
    phase_.store(RealmSessionPhase::kDisconnected,
                 std::memory_order_release);
  }
  return sent;
}

bool RealmSession::HandleGlueStartupPacket(const WorldPacket& pkt) {
  GlueStartupDispatchContext context;
  context.account_name = account_name_;
  context.current_character_guid = pending_character_guid_;
  context.send_packet = [this](const WorldPacket& response) {
    return SendPacket(response);
  };
  return openwow::net::wotlk::HandleGlueStartupPacket(pkt, context);
}

bool RealmSession::HandleHandshakePacket(const WorldPacket& pkt) {
  if (pkt.IsOpcode(Opcode::SMSG_ADDON_INFO)) {
    std::size_t consumed = 0;
    if (!RealmAddonHandshakeState::Instance().ProcessServerInfo(
            pkt.payload.data(), pkt.payload.size(), &consumed)) {
      return false;
    }
    if (consumed != pkt.payload.size()) {
      LogRealmMessageUnderRead("RealmSession::HandleHandshakePacket",
                               pkt.opcode,
                               consumed,
                               pkt.payload.size());
    }
    addon_info_received_ = true;
    if (addon_info_processed_callback_) {
      addon_info_processed_callback_();
    }
    return true;
  }

  if (pkt.IsOpcode(Opcode::SMSG_CLIENTCACHE_VERSION)) {
    if (pkt.payload.size() < sizeof(std::uint32_t)) {
      return false;
    }
    if (pkt.payload.size() != sizeof(std::uint32_t)) {
      LogRealmMessageUnderRead("RealmSession::HandleHandshakePacket",
                               pkt.opcode,
                               sizeof(std::uint32_t),
                               pkt.payload.size());
    }
    std::memcpy(&client_cache_version_, pkt.payload.data(),
                sizeof(client_cache_version_));
    if (client_cache_version_callback_) {
      client_cache_version_callback_(client_cache_version_);
    }
    return true;
  }

  return false;
}

void RealmSession::ResetReceiveState() {
  const auto primary = PrimaryStream();
  if (primary) {
    primary->receive_buffer.clear();
    primary->incoming_frame.Reset();
  }
  deferred_packets_.clear();
}

std::shared_ptr<RealmSession::RealmStream> RealmSession::PrimaryStream() const {
  std::scoped_lock lock(stream_mutex_);
  return primary_stream_;
}

std::shared_ptr<RealmSession::RealmStream> RealmSession::SecondaryStream() const {
  std::scoped_lock lock(stream_mutex_);
  return secondary_stream_;
}

bool RealmSession::StartSecondaryConnection(
    const std::uint32_t address_v4,
    const std::uint16_t port,
    const std::uint32_t transfer_cookie) {
  if (SecondaryStream() || secondary_receive_thread_.joinable()) {
    return false;
  }

  auto stream = std::make_shared<RealmStream>();
  {
    std::scoped_lock stream_lock(stream_mutex_);
    secondary_stream_ = stream;
  }
  {
    std::scoped_lock transfer_lock(transfer_mutex_);
    transfer_cookie_ = transfer_cookie;
    secondary_control_packets_.clear();
    secondary_disconnected_ = false;
  }
  secondary_event_pending_.store(false, std::memory_order_release);
  secondary_stop_requested_.store(false, std::memory_order_release);
  secondary_receive_thread_ = std::thread(
      &RealmSession::SecondaryReceiveMain, this, std::move(stream),
      FormatRedirectAddress(address_v4), port);
  return true;
}

void RealmSession::SecondaryReceiveMain(
    std::shared_ptr<RealmStream> stream,
    std::string host,
    const std::uint16_t port) {
  const auto stopped = [this] {
    return secondary_stop_requested_.load(std::memory_order_acquire);
  };
  if (!stream->client.Connect(host, port, kSecondaryConnectTimeoutMs,
                              stopped)) {
    PublishSecondaryDisconnect();
    return;
  }

  while (!stopped()) {
    WorldPacket packet;
    if (!ReceivePacketFromStream(stream, packet, 0, stopped, "secondary")) {
      PublishSecondaryDisconnect();
      return;
    }

    if (packet.IsOpcode(Opcode::SMSG_AUTH_CHALLENGE)) {
      const auto challenge = ParseRealmAuthChallenge(packet.payload);
      if (!challenge) {
        stream->client.Disconnect();
        PublishSecondaryDisconnect();
        return;
      }
      const auto nonce = SolveRealmProofOfWork(account_name_, *challenge);
      if (!nonce) {
        stream->client.Disconnect();
        PublishSecondaryDisconnect();
        return;
      }

      WorldPacket proof(Opcode::CMSG_REDIRECTION_AUTH_PROOF);
      proof.AppendString(account_name_);
      proof.AppendU64(*nonce);
      const auto digest = ComputeRealmRedirectionAuthDigest(
          account_name_, session_key_, challenge->auth_seed);
      proof.AppendBytes(digest.data(), digest.size());
      if (!SendPacketOnStream(stream, proof)) {
        PublishSecondaryDisconnect();
        return;
      }

      std::array<std::uint8_t, 32> challenge_bytes{};
      std::copy_n(packet.payload.begin() + kChallengeWordsOffset,
                  challenge_bytes.size(),
                  challenge_bytes.begin());
      stream->crypt.Init(session_key_.data(), challenge_bytes);
      continue;
    }

    if (packet.IsOpcode(Opcode::SMSG_REDIRECT_CLIENT) ||
        packet.IsOpcode(Opcode::SMSG_SUSPEND_COMMS) ||
        packet.IsOpcode(Opcode::SMSG_FORCE_SEND_QUEUED_PACKETS)) {
      const bool transfer_complete =
          packet.IsOpcode(Opcode::SMSG_FORCE_SEND_QUEUED_PACKETS);
      PublishSecondaryControlPacket(std::move(packet));
      if (transfer_complete) {
        return;
      }
      continue;
    }

    stream->client.Disconnect();
    PublishSecondaryDisconnect();
    return;
  }
}

void RealmSession::PublishSecondaryControlPacket(WorldPacket packet) {
  {
    std::scoped_lock lock(transfer_mutex_);
    secondary_control_packets_.push_back(std::move(packet));
  }
  secondary_event_pending_.store(true, std::memory_order_release);
}

void RealmSession::PublishSecondaryDisconnect() {
  if (secondary_stop_requested_.load(std::memory_order_acquire)) {
    return;
  }
  {
    std::scoped_lock lock(transfer_mutex_);
    secondary_disconnected_ = true;
  }
  secondary_event_pending_.store(true, std::memory_order_release);
}

bool RealmSession::TakeSecondaryEvent(WorldPacket& packet,
                                      bool& disconnected) {
  std::scoped_lock lock(transfer_mutex_);
  disconnected = false;
  if (!secondary_control_packets_.empty()) {
    packet = std::move(secondary_control_packets_.front());
    secondary_control_packets_.pop_front();
  } else if (secondary_disconnected_) {
    secondary_disconnected_ = false;
    disconnected = true;
  } else {
    secondary_event_pending_.store(false, std::memory_order_release);
    return false;
  }

  secondary_event_pending_.store(
      !secondary_control_packets_.empty() || secondary_disconnected_,
      std::memory_order_release);
  return true;
}

void RealmSession::JoinSecondaryReceiveThread() {
  if (secondary_receive_thread_.joinable() &&
      secondary_receive_thread_.get_id() != std::this_thread::get_id()) {
    secondary_receive_thread_.join();
  }
}

void RealmSession::StopSecondaryConnection() {
  secondary_stop_requested_.store(true, std::memory_order_release);
  const auto secondary = SecondaryStream();
  if (secondary) {
    secondary->client.Disconnect();
  }
  JoinSecondaryReceiveThread();
  {
    std::scoped_lock stream_lock(stream_mutex_);
    if (secondary_stream_ == secondary) {
      secondary_stream_.reset();
    }
  }
  {
    std::scoped_lock transfer_lock(transfer_mutex_);
    secondary_control_packets_.clear();
    secondary_disconnected_ = false;
  }
  secondary_event_pending_.store(false, std::memory_order_release);
  secondary_stop_requested_.store(false, std::memory_order_release);
}

void RealmSession::HandleSecondaryDisconnect() {
  std::uint32_t cookie = 0;
  {
    std::scoped_lock lock(transfer_mutex_);
    cookie = transfer_cookie_;
  }
  StopSecondaryConnection();

  WorldPacket failed(Opcode::CMSG_REDIRECTION_FAILED);
  failed.AppendU32(cookie);
  (void)SendPacket(failed);
}

RealmSession::ConnectionPacketResult RealmSession::HandleConnectionPacket(
    const WorldPacket& packet,
    const ConnectionRole role) {
  if (packet.IsOpcode(Opcode::SMSG_REDIRECT_CLIENT)) {
    if (packet.payload.size() < kRedirectPrefixBytes) {
      if (role == ConnectionRole::kPrimary) {
        Disconnect();
        return ConnectionPacketResult::kFatal;
      }
      HandleSecondaryDisconnect();
      return ConnectionPacketResult::kConsumed;
    }

    const std::span<const std::uint8_t> payload(packet.payload);
    const std::uint32_t address_v4 = ReadU32Le(payload, 0);
    const std::uint16_t port = ReadU16Le(payload, kRedirectAddressBytes);
    const std::uint32_t cookie = ReadU32Le(
        payload, kRedirectAddressBytes + kRedirectPortBytes);
    {
      std::scoped_lock transfer_lock(transfer_mutex_);
      transfer_cookie_ = cookie;
    }
    bool transfer_unavailable = SecondaryStream() != nullptr;
    {
      std::scoped_lock send_lock(send_mutex_);
      transfer_unavailable = transfer_unavailable || transfer_pending_;
    }
    if (transfer_unavailable) {
      WorldPacket failed(Opcode::CMSG_REDIRECTION_FAILED);
      failed.AppendU32(cookie);
      (void)SendPacket(failed);
      return ConnectionPacketResult::kConsumed;
    }

    if (packet.payload.size() != kRedirectPayloadBytes) {
      Disconnect();
      return ConnectionPacketResult::kFatal;
    }
    const auto expected_digest = ComputeSha1PadHmac(
        session_key_, payload.first<kRedirectAddressBytes>(),
        payload.subspan<kRedirectAddressBytes, kRedirectPortBytes>());
    if (!std::equal(expected_digest.begin(), expected_digest.end(),
                    packet.payload.begin() + kRedirectPrefixBytes)) {
      Disconnect();
      return ConnectionPacketResult::kFatal;
    }
    if (!StartSecondaryConnection(address_v4, port, cookie)) {
      WorldPacket failed(Opcode::CMSG_REDIRECTION_FAILED);
      failed.AppendU32(cookie);
      (void)SendPacket(failed);
    }
    return ConnectionPacketResult::kConsumed;
  }

  if (packet.IsOpcode(Opcode::SMSG_SUSPEND_COMMS)) {
    bool pending = false;
    {
      std::scoped_lock send_lock(send_mutex_);
      pending = transfer_pending_;
    }
    if (role != ConnectionRole::kPrimary || pending || !SecondaryStream() ||
        packet.payload.size() != sizeof(std::uint32_t)) {
      if (role == ConnectionRole::kPrimary) {
        Disconnect();
        return ConnectionPacketResult::kFatal;
      }
      HandleSecondaryDisconnect();
      return ConnectionPacketResult::kConsumed;
    }

    WorldPacket acknowledge(Opcode::CMSG_SUSPEND_COMMS_ACK);
    acknowledge.AppendU32(ReadU32Le(packet.payload, 0));
    {
      std::scoped_lock send_lock(send_mutex_);

      (void)SendPacketOnStream(PrimaryStream(), acknowledge);
      transfer_pending_ = true;
    }
    return ConnectionPacketResult::kConsumed;
  }

  if (packet.IsOpcode(Opcode::SMSG_FORCE_SEND_QUEUED_PACKETS)) {
    CompleteConnectionTransfer(role);
    return ConnectionPacketResult::kConsumed;
  }

  bool pending = false;
  {
    std::scoped_lock send_lock(send_mutex_);
    pending = transfer_pending_;
  }
  if (role == ConnectionRole::kSecondary || pending) {
    if (role == ConnectionRole::kSecondary) {
      HandleSecondaryDisconnect();
      return ConnectionPacketResult::kConsumed;
    }
    Disconnect();
    return ConnectionPacketResult::kFatal;
  }
  return ConnectionPacketResult::kDeliver;
}

void RealmSession::CompleteConnectionTransfer(const ConnectionRole source) {
  std::shared_ptr<RealmStream> displaced_primary;
  if (source == ConnectionRole::kSecondary) {
    if (!SecondaryStream()) {
      Disconnect();
      return;
    }
    std::scoped_lock send_lock(send_mutex_);

    JoinSecondaryReceiveThread();
    std::shared_ptr<RealmStream> promoted_primary;
    {
      std::scoped_lock stream_lock(stream_mutex_);
      displaced_primary = std::move(primary_stream_);
      primary_stream_ = std::move(secondary_stream_);
      promoted_primary = primary_stream_;
    }
    if (displaced_primary) {
      displaced_primary->client.Disconnect();
    }
    transfer_pending_ = false;
    while (!held_outbound_packets_.empty()) {
      WorldPacket held = std::move(held_outbound_packets_.front());
      held_outbound_packets_.pop_front();
      if (!SendPacketOnStream(promoted_primary, held)) {
        connected_.store(false, std::memory_order_release);
        authenticated_.store(false, std::memory_order_release);
        phase_.store(RealmSessionPhase::kDisconnected,
                     std::memory_order_release);
        break;
      }
    }
  } else {
    StopSecondaryConnection();
    std::scoped_lock send_lock(send_mutex_);
    transfer_pending_ = false;
    while (!held_outbound_packets_.empty()) {
      WorldPacket held = std::move(held_outbound_packets_.front());
      held_outbound_packets_.pop_front();
      if (!SendPacketOnStream(PrimaryStream(), held)) {
        break;
      }
    }
  }

  {
    std::scoped_lock transfer_lock(transfer_mutex_);
    secondary_control_packets_.clear();
    secondary_disconnected_ = false;
    transfer_cookie_ = 0;
  }
  secondary_event_pending_.store(false, std::memory_order_release);
  secondary_stop_requested_.store(false, std::memory_order_release);
}

bool RealmSession::TakeDeferredPacket(WorldPacket& out) {
  if (deferred_packets_.empty()) {
    return false;
  }
  out = std::move(deferred_packets_.front());
  deferred_packets_.pop_front();
  return true;
}

std::vector<WorldPacket> RealmSession::DrainDeferredPackets() {
  std::vector<WorldPacket> packets;
  packets.reserve(deferred_packets_.size());
  while (!deferred_packets_.empty()) {
    packets.push_back(std::move(deferred_packets_.front()));
    deferred_packets_.pop_front();
  }
  return packets;
}

void RealmSession::DeferPacket(WorldPacket packet) {
  deferred_packets_.push_back(std::move(packet));
}

bool RealmSession::RecvPacket(
    WorldPacket& out,
    const std::uint32_t timeout_ms,
    const std::function<bool()>& should_cancel) {
  using Clock = std::chrono::steady_clock;
  const bool has_deadline = timeout_ms != 0;
  const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);

  while (connected() && !(should_cancel && should_cancel())) {
    WorldPacket secondary_packet;
    bool secondary_disconnected = false;
    if (TakeSecondaryEvent(secondary_packet, secondary_disconnected)) {
      if (secondary_disconnected) {
        HandleSecondaryDisconnect();
        continue;
      }
      const auto result = HandleConnectionPacket(
          secondary_packet, ConnectionRole::kSecondary);
      if (result == ConnectionPacketResult::kFatal) {
        return false;
      }
      continue;
    }

    const auto now = Clock::now();
    if (has_deadline && now >= deadline) {
      return false;
    }
    std::uint32_t remaining_ms = 0;
    if (has_deadline) {
      const auto remaining =
          std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
      remaining_ms = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
          remaining.count(), 1, std::numeric_limits<std::uint32_t>::max()));
    }

    const auto interrupted = [this, &should_cancel] {
      return (should_cancel && should_cancel()) ||
             secondary_event_pending_.load(std::memory_order_acquire);
    };
    WorldPacket primary_packet;
    const auto primary = PrimaryStream();
    if (!ReceivePacketFromStream(primary, primary_packet, remaining_ms,
                                 interrupted, "primary")) {
      if (secondary_event_pending_.load(std::memory_order_acquire)) {
        continue;
      }
      if (should_cancel && should_cancel()) {
        return false;
      }
      if (!primary || !primary->client.IsConnected()) {
        StopSecondaryConnection();
        connected_.store(false, std::memory_order_release);
        authenticated_.store(false, std::memory_order_release);
        phase_.store(RealmSessionPhase::kDisconnected,
                     std::memory_order_release);
      }
      return false;
    }

    const auto result =
        HandleConnectionPacket(primary_packet, ConnectionRole::kPrimary);
    if (result == ConnectionPacketResult::kFatal) {
      return false;
    }
    if (result == ConnectionPacketResult::kConsumed) {
      continue;
    }
    out = std::move(primary_packet);
    return true;
  }
  return false;
}

bool RealmSession::ReceivePacketFromStream(
    const std::shared_ptr<RealmStream>& stream,
    WorldPacket& out,
    const std::uint32_t timeout_ms,
    const std::function<bool()>& should_cancel,
    const char* const stream_name) {
  const auto cancelled = [&should_cancel] {
    return should_cancel && should_cancel();
  };
  if (!stream || !stream->client.IsConnected() || cancelled()) return false;

  using Clock = std::chrono::steady_clock;
  const bool has_deadline = timeout_ms != 0;
  const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
  const auto read_until = [&](const std::size_t required_size) {
    while (stream->receive_buffer.size() < required_size) {
      if (cancelled()) {
        return false;
      }
      const auto now = Clock::now();
      if (has_deadline && now >= deadline) {
        return false;
      }
      std::uint32_t remaining_ms = 0;
      if (has_deadline) {
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        remaining_ms = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
            remaining.count(), 1, std::numeric_limits<std::uint32_t>::max()));
      }
      auto chunk = has_deadline
          ? stream->client.ReadSome(4096, remaining_ms, should_cancel)
          : stream->client.ReadSomeUntilCancelled(4096, should_cancel);
      if (chunk.empty()) {
        if (cancelled()) {
          return false;
        }
        if (!stream->client.IsConnected()) {
          const char* frame_stage = "size";
          if (stream->incoming_frame.header_decrypted) {
            frame_stage = "body";
          } else if (stream->incoming_frame.first_byte_decrypted) {
            frame_stage = "header";
          }
          openwow::diagnostics::Log(
              openwow::diagnostics::LogLevel::kWarn,
              std::string("RealmSession: ") + stream_name +
                  " receive stream closed stage=" +
                  frame_stage + " buffered_bytes=" +
                  std::to_string(stream->receive_buffer.size()) +
                  " expected_frame_bytes=" +
                  std::to_string(stream->incoming_frame.frame_len));
        }
        return false;
      }
      stream->receive_buffer.insert(stream->receive_buffer.end(),
                                    chunk.begin(), chunk.end());

      if (cancelled()) {
        return false;
      }
    }
    return !cancelled();
  };

  if (!stream->incoming_frame.first_byte_decrypted) {
    if (!read_until(1)) return false;
    if (stream->crypt.IsInitialized()) {
      stream->crypt.DecryptRecv(stream->receive_buffer.data(), 1);
    }
    stream->incoming_frame.first_byte_decrypted = true;
    stream->incoming_frame.size_field_len =
        WorldPacket::ServerSizeFieldLength(stream->receive_buffer.front());
    stream->incoming_frame.header_len =
        ServerFullHeaderLength(stream->receive_buffer.front());
  }

  if (!stream->incoming_frame.header_decrypted) {
    if (!read_until(stream->incoming_frame.header_len)) return false;
    if (stream->crypt.IsInitialized()) {
      stream->crypt.DecryptRecv(stream->receive_buffer.data() + 1,
                                stream->incoming_frame.header_len - 1);
    }

    std::size_t payload_plus_opcode = 0;
    if (stream->incoming_frame.size_field_len == 3) {
      payload_plus_opcode =
          (static_cast<std::size_t>(stream->receive_buffer[0] & 0x7F) << 16) |
          (static_cast<std::size_t>(stream->receive_buffer[1]) << 8) |
          static_cast<std::size_t>(stream->receive_buffer[2]);
    } else {
      payload_plus_opcode =
          (static_cast<std::size_t>(stream->receive_buffer[0]) << 8) |
          static_cast<std::size_t>(stream->receive_buffer[1]);
    }

    if (payload_plus_opcode < 2) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                         "RealmSession: malformed world packet size value=" +
                             std::to_string(payload_plus_opcode) +
                             " size_field_bytes=" +
                             std::to_string(stream->incoming_frame.size_field_len));
      stream->client.Disconnect();
      return false;
    }

    stream->incoming_frame.frame_len =
        stream->incoming_frame.header_len + payload_plus_opcode - 2;
    stream->incoming_frame.header_decrypted = true;
  }

  if (!read_until(stream->incoming_frame.frame_len)) return false;

  auto decoded = WorldPacket::DecodeServerFrame(std::span<const std::uint8_t>(
      stream->receive_buffer.data(), stream->incoming_frame.frame_len));
  if (!decoded.IsComplete() ||
      decoded.bytes_consumed != stream->incoming_frame.frame_len) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "RealmSession: decoded world frame disagrees with "
                       "incremental header");
    stream->client.Disconnect();
    return false;
  }
  out = std::move(decoded.packet);

  stream->receive_buffer.erase(
      stream->receive_buffer.begin(),
      stream->receive_buffer.begin() +
          static_cast<std::ptrdiff_t>(stream->incoming_frame.frame_len));
  stream->incoming_frame.Reset();
  return true;
}

bool RealmSession::ReceiveExpectedPacket(
    const std::initializer_list<Opcode> expected_opcodes,
    WorldPacket& out,
    const std::uint32_t timeout_ms,
    const char* const scope,
    const std::function<bool()>& should_cancel) {
  using Clock = std::chrono::steady_clock;
  const bool has_deadline = timeout_ms != 0;
  const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);

  while (connected() && !(should_cancel && should_cancel())) {
    const auto now = Clock::now();
    if (has_deadline && now >= deadline) {
      return false;
    }
    std::uint32_t remaining_ms = 0;
    if (has_deadline) {
      const auto remaining =
          std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
      remaining_ms = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
          remaining.count(), 1, std::numeric_limits<std::uint32_t>::max()));
    }
    if (!RecvPacket(out, remaining_ms, should_cancel)) {
      if (!connected() || (should_cancel && should_cancel())
          || (has_deadline && Clock::now() >= deadline)) {
        const char* reason = !connected()
            ? "stream_closed"
            : ((should_cancel && should_cancel()) ? "caller_cancelled"
                                                  : "deadline_expired");
        const auto primary = PrimaryStream();
        openwow::diagnostics::Log(
            openwow::diagnostics::LogLevel::kWarn,
            std::string(scope) + ": expected packet receive failed reason=" +
                reason + " buffered_bytes=" +
                std::to_string(primary ? primary->receive_buffer.size() : 0U));
        return false;
      }
      continue;
    }

    if (std::find(expected_opcodes.begin(), expected_opcodes.end(),
                  out.GetOpcode()) != expected_opcodes.end()) {
      return true;
    }
    if (ObserveConnectionPacket(out,
                                openwow::core::GameClock::GetTickCount32())) {
      continue;
    }
    if (HandleHandshakePacket(out) || HandleGlueStartupPacket(out)) {
      continue;
    }

    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kDebug,
        std::string(scope) + ": deferring unsolicited opcode 0x" +
            FormatOpcodeHex(out.opcode));
    DeferPacket(std::move(out));
  }
  return false;
}

bool RealmSession::TryReceiveRealmSplit(WorldPacket& out) {
  std::unique_lock operation_lock(operation_mutex_, std::try_to_lock);
  if (!operation_lock.owns_lock() || !CanPerformCharacterOperation()) {
    return false;
  }

  const auto deferred = std::find_if(
      deferred_packets_.begin(), deferred_packets_.end(),
      [](const WorldPacket& packet) {
        return packet.IsOpcode(Opcode::SMSG_REALM_SPLIT);
      });
  if (deferred != deferred_packets_.end()) {
    out = std::move(*deferred);
    deferred_packets_.erase(deferred);
    return true;
  }

  WorldPacket packet;
  constexpr std::uint32_t kNonBlockingPollTimeoutMs = 1;
  if (!RecvPacket(packet, kNonBlockingPollTimeoutMs)) {
    return false;
  }
  if (packet.IsOpcode(Opcode::SMSG_REALM_SPLIT)) {
    out = std::move(packet);
    return true;
  }

  if (!ObserveConnectionPacket(packet,
                               openwow::core::GameClock::GetTickCount32()) &&
      !HandleHandshakePacket(packet) && !HandleGlueStartupPacket(packet)) {
    DeferPacket(std::move(packet));
  }
  return false;
}

CharacterListResult RealmSession::FetchCharacterList(
    const std::uint32_t timeout_ms,
    const std::function<bool()>& should_cancel) {
  std::unique_lock operation_lock(operation_mutex_);
  if (!CanPerformCharacterOperation()) {
    return {.ok = false,
            .message = "Not authenticated.",
            .trailing_u32s = char_enum_trailing_u32s_};
  }

  const WorldPacket req = PacketSender::BuildCharEnum();
  if (!SendPacket(req)) {
    return {.ok = false,
            .message = "Failed to send CMSG_CHAR_ENUM.",
            .trailing_u32s = char_enum_trailing_u32s_};
  }

  WorldPacket resp;
  if (!ReceiveExpectedPacket({Opcode::SMSG_CHAR_ENUM}, resp, timeout_ms,
                             "RealmSession::FetchCharacterList",
                             should_cancel)) {
    return {.ok = false,
            .message = (should_cancel && should_cancel())
                ? "Character list request cancelled."
                : "Timeout waiting for SMSG_CHAR_ENUM.",
            .trailing_u32s = char_enum_trailing_u32s_};
  }

  RealmConnectionCharEnumPayload parsed;
  if (!ParseRealmConnectionCharEnum(resp.payload.data(), resp.payload.size(), parsed)) {
    return {.ok = false,
            .message = "Malformed SMSG_CHAR_ENUM.",
            .trailing_u32s = char_enum_trailing_u32s_};
  }
  if (parsed.has_trailing_u32s) {
    char_enum_trailing_u32s_ = parsed.trailing_u32s;
  }

  std::vector<CharacterSummary> chars;
  chars.reserve(parsed.characters.size());
  for (const auto& src : parsed.characters) {
    CharacterSummary c;
    c.id = src.guid;
    c.name = src.name;
    c.level = static_cast<int>(src.level);
    c.race_id = src.race;
    c.class_id = src.char_class;
    c.gender = src.gender;
    c.skin = src.skin;
    c.face = src.face;
    c.hair_style = src.hair_style;
    c.hair_color = src.hair_color;
    c.facial_hair = src.facial_hair;
    c.zone_id = src.zone_id;
    c.map_id = src.map_id;
    c.x = src.x;
    c.y = src.y;
    c.z = src.z;
    c.guild_id = src.guild_id;
    c.char_flags = src.char_flags;
    c.at_login_flags = src.customize_flags;
    c.is_first_login = (src.first_login != 0);
    c.pet_display_id = src.pet_display_id;
    c.pet_level = src.pet_level;
    c.pet_family = src.pet_family;
    for (std::size_t slot = 0; slot < c.equipment.size(); ++slot) {
      c.equipment[slot].display_id = src.equipment[slot].display_id;
      c.equipment[slot].inv_type = src.equipment[slot].inventory_type;
      c.equipment[slot].enchant_id = src.equipment[slot].enchant_aura;
    }
    chars.push_back(std::move(c));
  }

  return {
      .ok = true,
      .message = "Characters parsed (" + std::to_string(chars.size()) + ").",
      .characters = std::move(chars),
      .trailing_u32s = char_enum_trailing_u32s_,
  };
}

CharacterCreateResult RealmSession::CreateCharacter(const std::string& name,
                                                    std::uint8_t race,
                                                    std::uint8_t cls,
                                                    std::uint8_t gender,
                                                    std::uint8_t skin,
                                                    std::uint8_t face,
                                                    std::uint8_t hair_style,
                                                    std::uint8_t hair_color,
                                                    std::uint8_t facial_hair,
                                                    std::uint32_t timeout_ms) {
  std::unique_lock operation_lock(operation_mutex_);
  if (!CanPerformCharacterOperation()) {
    return {.ok = false, .result_code = 0, .message = "Not authenticated."};
  }

  const WorldPacket req = PacketSender::BuildCharCreate(
      name, race, cls, gender, skin, face, hair_style, hair_color,
      facial_hair, 0);

  if (!SendPacket(req)) {
    return {.ok = false, .result_code = 0, .message = "Failed to send CMSG_CHAR_CREATE."};
  }

  WorldPacket resp;
  if (!ReceiveExpectedPacket({Opcode::SMSG_CHAR_CREATE}, resp, timeout_ms,
                             "RealmSession::CreateCharacter")) {
    return {.ok = false, .result_code = 0,
            .message = "Timeout waiting for SMSG_CHAR_CREATE."};
  }
  if (resp.payload.empty()) {
    return {.ok = false, .result_code = 0, .message = "No SMSG_CHAR_CREATE received."};
  }
  if (resp.payload.size() > 1) {
    LogRealmMessageUnderRead("RealmSession::CreateCharacter", resp.opcode,
                             1, resp.payload.size());
  }

  const std::uint8_t result_code = resp.payload[0];
  if (result_code == CHAR_CREATE_SUCCESS) {
    return {.ok = true, .result_code = result_code, .message = "Character created."};
  }
  return {.ok = false, .result_code = result_code,
          .message = "Character creation failed (code=" + std::to_string(result_code) + ")."};
}

CharacterDeleteResult RealmSession::DeleteCharacter(std::uint64_t guid, std::uint32_t timeout_ms) {
  std::unique_lock operation_lock(operation_mutex_);
  if (!CanPerformCharacterOperation()) {
    return {.ok = false, .result_code = 0, .message = "Not authenticated."};
  }

  const WorldPacket req = PacketSender::BuildCharDelete(guid);

  if (!SendPacket(req)) {
    return {.ok = false, .result_code = 0, .message = "Failed to send CMSG_CHAR_DELETE."};
  }

  WorldPacket resp;
  if (!ReceiveExpectedPacket({Opcode::SMSG_CHAR_DELETE}, resp, timeout_ms,
                             "RealmSession::DeleteCharacter")) {
    return {.ok = false, .result_code = 0,
            .server_outcome_unknown = true,
            .message = "Timeout waiting for SMSG_CHAR_DELETE."};
  }
  if (resp.payload.empty()) {
    return {.ok = false, .result_code = 0, .message = "No SMSG_CHAR_DELETE received."};
  }
  if (resp.payload.size() > 1) {
    LogRealmMessageUnderRead("RealmSession::DeleteCharacter", resp.opcode,
                             1, resp.payload.size());
  }

  const std::uint8_t result_code = resp.payload[0];
  if (result_code == CHAR_DELETE_SUCCESS) {
    return {.ok = true, .result_code = result_code, .message = "Character deleted."};
  }
  return {.ok = false, .result_code = result_code,
          .message = "Character deletion failed (code=" + std::to_string(result_code) + ")."};
}

CharacterRenameResult RealmSession::RenameCharacter(std::uint64_t guid,
                                                    const std::string& new_name,
                                                    std::uint32_t timeout_ms) {
  std::unique_lock operation_lock(operation_mutex_);
  if (!CanPerformCharacterOperation()) {
    return {.ok = false, .result_code = 0xFF, .new_name = {}};
  }

  WorldPacket req(Opcode::CMSG_CHAR_RENAME);
  req.AppendU64(guid);
  req.AppendString(new_name.c_str());

  if (!SendPacket(req)) {
    return {.ok = false, .result_code = 0xFF, .new_name = {}};
  }

  WorldPacket resp;
  if (!ReceiveExpectedPacket({Opcode::SMSG_CHAR_RENAME}, resp, timeout_ms,
                             "RealmSession::RenameCharacter") ||
      resp.payload.empty()) {
    return {.ok = false, .result_code = 0xFF, .new_name = {}};
  }

  const std::uint8_t result_code = resp.payload[0];
  if (result_code == RESPONSE_SUCCESS && resp.payload.size() > 9) {

    std::size_t offset = 1;
    const std::uint64_t response_guid = ReadOptionalU64(resp.payload, offset);
    std::string confirmed_name;
    if (ReadCharacterServiceCString(resp.payload, offset,
                                    kCharacterServiceNameBufferBytes,
                                    confirmed_name)) {
      return {.ok = true, .result_code = result_code, .guid = response_guid,
              .new_name = std::move(confirmed_name)};
    }
  }
  return {.ok = false, .result_code = result_code, .new_name = {}};
}

CharacterDeclinedNamesResult RealmSession::SetPlayerDeclinedNames(
    std::uint64_t guid, std::string_view base_name,
    const std::array<std::string, 5>& declined_forms,
    std::uint32_t timeout_ms) {
  std::unique_lock operation_lock(operation_mutex_);
  if (!CanPerformCharacterOperation()) {
    return {.ok = false, .result_code = 0xFFFFFFFFu, .guid = 0};
  }

  WorldPacket req(Opcode::CMSG_SET_PLAYER_DECLINED_NAMES);
  req.AppendU64(guid);
  const std::string base_name_copy(base_name);
  req.AppendString(base_name_copy.c_str());
  for (const auto& form : declined_forms) {
    req.AppendString(form.c_str());
  }

  if (!SendPacket(req)) {
    return {.ok = false, .result_code = 0xFFFFFFFFu, .guid = 0};
  }

  WorldPacket resp;
  if (!ReceiveExpectedPacket(
          {Opcode::SMSG_SET_PLAYER_DECLINED_NAMES_RESULT}, resp, timeout_ms,
          "RealmSession::SetPlayerDeclinedNames") ||
      resp.payload.size() < 4) {
    return {.ok = false, .result_code = 0xFFFFFFFFu, .guid = 0};
  }

  CharacterDeclinedNamesResult result{};
  std::memcpy(&result.result_code, resp.payload.data(), sizeof(result.result_code));
  result.ok = (result.result_code == 0);
  if (result.ok) {
    if (resp.payload.size() < 12) {
      return {.ok = false, .result_code = 0xFFFFFFFFu, .guid = 0};
    }
    std::memcpy(&result.guid,
                resp.payload.data() + sizeof(result.result_code),
                sizeof(result.guid));
  }
  return result;
}

CharacterCustomizeResult RealmSession::CustomizeCharacter(
    std::uint64_t guid, const std::string& name,
    std::uint8_t gender, std::uint8_t skin,
    std::uint8_t hair_style, std::uint8_t hair_color,
    std::uint8_t facial_hair, std::uint8_t face,
    std::uint32_t timeout_ms) {
  std::unique_lock operation_lock(operation_mutex_);
  if (!CanPerformCharacterOperation()) {
    return {.ok = false, .result_code = 0xFF};
  }

  WorldPacket req(Opcode::CMSG_CHAR_CUSTOMIZE);
  req.AppendU64(guid);
  req.AppendString(name.c_str());
  req.AppendU8(gender);
  req.AppendU8(skin);
  req.AppendU8(hair_style);
  req.AppendU8(hair_color);
  req.AppendU8(facial_hair);
  req.AppendU8(face);

  if (!SendPacket(req)) {
    return {.ok = false, .result_code = 0xFF};
  }

  WorldPacket resp;
  if (!ReceiveExpectedPacket({Opcode::SMSG_CHAR_CUSTOMIZE}, resp, timeout_ms,
                             "RealmSession::CustomizeCharacter") ||
      resp.payload.empty()) {
    return {.ok = false, .result_code = 0xFF};
  }

  const std::uint8_t result_code = resp.payload[0];
  CharacterCustomizeResult result{
      .ok = (result_code == RESPONSE_SUCCESS),
      .result_code = result_code,
  };
  if (result.ok) {
    const auto parsed = ParseCharacterServicePayload(resp.payload, false);
    result.guid = parsed.guid;
    result.name = std::move(parsed.name);
    result.gender = parsed.gender;
    result.skin = parsed.skin;
    result.face = parsed.face;
    result.hair_style = parsed.hair_style;
    result.hair_color = parsed.hair_color;
    result.facial_hair = parsed.facial_hair;
  }
  return result;
}

CharacterFactionOrRaceChangeResult RealmSession::FactionChangeCharacter(
    std::uint64_t guid, const std::string& name,
    std::uint8_t gender, std::uint8_t skin,
    std::uint8_t hair_style, std::uint8_t hair_color,
    std::uint8_t facial_hair, std::uint8_t face, std::uint8_t race,
    std::uint32_t timeout_ms) {
  std::unique_lock operation_lock(operation_mutex_);
  if (!CanPerformCharacterOperation()) {
    return {.ok = false, .result_code = 0xFF};
  }

  WorldPacket req(Opcode::CMSG_CHAR_FACTION_CHANGE);
  req.AppendU64(guid);
  req.AppendString(name.c_str());
  req.AppendU8(gender);
  req.AppendU8(skin);
  req.AppendU8(hair_style);
  req.AppendU8(hair_color);
  req.AppendU8(facial_hair);
  req.AppendU8(face);
  req.AppendU8(race);

  if (!SendPacket(req)) {
    return {.ok = false, .result_code = 0xFF};
  }

  WorldPacket resp;
  if (!ReceiveExpectedPacket({Opcode::SMSG_CHAR_FACTION_CHANGE}, resp,
                             timeout_ms,
                             "RealmSession::FactionChangeCharacter") ||
      resp.payload.empty()) {
    return {.ok = false, .result_code = 0xFF};
  }

  const std::uint8_t result_code = resp.payload[0];
  CharacterFactionOrRaceChangeResult result{
      .ok = (result_code == RESPONSE_SUCCESS),
      .result_code = result_code,
  };
  if (result.ok) {
    const auto parsed = ParseCharacterServicePayload(resp.payload, true);
    result.guid = parsed.guid;
    result.name = std::move(parsed.name);
    result.race = parsed.race;
    result.gender = parsed.gender;
    result.skin = parsed.skin;
    result.face = parsed.face;
    result.hair_style = parsed.hair_style;
    result.hair_color = parsed.hair_color;
    result.facial_hair = parsed.facial_hair;
  }
  return result;
}

CharacterFactionOrRaceChangeResult RealmSession::RaceChangeCharacter(
    std::uint64_t guid, const std::string& name,
    std::uint8_t gender, std::uint8_t skin,
    std::uint8_t hair_style, std::uint8_t hair_color,
    std::uint8_t facial_hair, std::uint8_t face, std::uint8_t race,
    std::uint32_t timeout_ms) {
  std::unique_lock operation_lock(operation_mutex_);
  if (!CanPerformCharacterOperation()) {
    return {.ok = false, .result_code = 0xFF};
  }

  WorldPacket req(Opcode::CMSG_CHAR_RACE_CHANGE);
  req.AppendU64(guid);
  req.AppendString(name.c_str());
  req.AppendU8(gender);
  req.AppendU8(skin);
  req.AppendU8(hair_style);
  req.AppendU8(hair_color);
  req.AppendU8(facial_hair);
  req.AppendU8(face);
  req.AppendU8(race);

  if (!SendPacket(req)) {
    return {.ok = false, .result_code = 0xFF};
  }

  WorldPacket resp;
  if (!ReceiveExpectedPacket({Opcode::SMSG_CHAR_FACTION_CHANGE}, resp,
                             timeout_ms,
                             "RealmSession::RaceChangeCharacter") ||
      resp.payload.empty()) {
    return {.ok = false, .result_code = 0xFF};
  }

  const std::uint8_t result_code = resp.payload[0];
  CharacterFactionOrRaceChangeResult result{
      .ok = (result_code == RESPONSE_SUCCESS),
      .result_code = result_code,
  };
  if (result.ok) {
    const auto parsed = ParseCharacterServicePayload(resp.payload, true);
    result.guid = parsed.guid;
    result.name = std::move(parsed.name);
    result.race = parsed.race;
    result.gender = parsed.gender;
    result.skin = parsed.skin;
    result.face = parsed.face;
    result.hair_style = parsed.hair_style;
    result.hair_color = parsed.hair_color;
    result.facial_hair = parsed.facial_hair;
  }
  return result;
}

WorldEnterResult RealmSession::EnterWorld(std::uint64_t character_guid, std::uint32_t timeout_ms) {
  std::unique_lock operation_lock(operation_mutex_);
  auto finish = [this](WorldEnterResult result) {
    pending_character_guid_ = 0;
    if (result.status == WorldEnterStatus::kSuccess) {
      phase_.store(RealmSessionPhase::kWorld, std::memory_order_release);
    } else if (phase() == RealmSessionPhase::kEnteringWorld) {
      RestoreCharacterSelectAfterFailedWorldEntry();
    }
    return result;
  };
  if (!CanPerformCharacterOperation()) {
    return finish(
        {.status = WorldEnterStatus::kLoginFailed, .message = "Not authenticated."});
  }
  if (character_guid == 0) {
    return finish({.status = WorldEnterStatus::kInvalidCharacter,
                   .message = "Invalid character GUID."});
  }

  phase_.store(RealmSessionPhase::kEnteringWorld, std::memory_order_release);
  pending_character_guid_ = character_guid;
  WorldPacket req = PacketSender::BuildPlayerLogin(character_guid);

  if (!SendPacket(req)) {
    return finish({.status = WorldEnterStatus::kNetworkError,
                   .message = "Failed to send CMSG_PLAYER_LOGIN."});
  }

  WorldPacket resp;
  if (!ReceiveExpectedPacket(
          {Opcode::SMSG_LOGIN_VERIFY_WORLD,
           Opcode::SMSG_CHARACTER_LOGIN_FAILED},
          resp, timeout_ms, "RealmSession::EnterWorld")) {
    return finish({.status = WorldEnterStatus::kNetworkError,
                   .message = "Timeout waiting for login response."});
  }

  if (resp.IsOpcode(Opcode::SMSG_CHARACTER_LOGIN_FAILED)) {
    const std::uint8_t reason =
        resp.payload.empty() ? 0 : resp.payload.front();
    if (resp.payload.size() > 1) {
      LogRealmMessageUnderRead("RealmSession::EnterWorld", resp.opcode,
                               1, resp.payload.size());
    }
    return finish(
        {.status = WorldEnterStatus::kLoginFailed,
         .message = "Character login failed (code=" + std::to_string(reason) + ").",
         .result_code = reason});
  }

  if (resp.payload.size() < 20) {
    return finish({.status = WorldEnterStatus::kNetworkError,
                   .message = "Malformed SMSG_LOGIN_VERIFY_WORLD."});
  }
  if (resp.payload.size() > 20) {
    LogRealmMessageUnderRead("RealmSession::EnterWorld", resp.opcode,
                             20, resp.payload.size());
  }

  WorldEnterResult result{.status = WorldEnterStatus::kSuccess,
                          .message = "Entering world."};
  std::memcpy(&result.map_id, resp.payload.data(), 4);
  std::memcpy(&result.x, resp.payload.data() + 4, 4);
  std::memcpy(&result.y, resp.payload.data() + 8, 4);
  std::memcpy(&result.z, resp.payload.data() + 12, 4);
  std::memcpy(&result.orientation, resp.payload.data() + 16, 4);
  return finish(result);
}

std::vector<CharacterSummary> ParseCharacterList(const std::string& serialized) {
  if (serialized.empty()) {
    return {{.id = 1, .name = "TestCharacter", .level = 80}};
  }
  std::vector<CharacterSummary> out;
  std::stringstream ss(serialized);
  std::string token;
  std::uint64_t fallback_id = 1;
  while (std::getline(ss, token, ';')) {
    if (token.empty()) continue;
    CharacterSummary c;

    std::string rest = token;
    const auto hash = rest.find('#');
    if (hash != std::string::npos) {
      std::uint64_t explicit_id = 0;
      const std::string id_text = rest.substr(0, hash);
      std::from_chars(id_text.data(), id_text.data() + id_text.size(), explicit_id);
      if (explicit_id > 0) {
        c.id = explicit_id;
        rest = rest.substr(hash + 1);
      } else {
        c.id = fallback_id++;
      }
    } else {
      c.id = fallback_id++;
    }
    const auto at = rest.find('@');
    if (at == std::string::npos) {
      c.name = rest;
    } else {
      c.name = rest.substr(0, at);
      int lv = 1;
      const std::string lv_text = rest.substr(at + 1);
      std::from_chars(lv_text.data(), lv_text.data() + lv_text.size(), lv);
      c.level = std::clamp(lv, 1, 80);
    }
    if (c.name.empty()) c.name = "Character" + std::to_string(c.id);
    out.push_back(std::move(c));
  }
  if (out.empty()) {
    out.push_back({.id = 1, .name = "TestCharacter", .level = 80});
  }
  return out;
}

std::vector<CharacterSummary> FetchCharacterList() {
  return ParseCharacterList(GetEnvOrDefault("OPENWOW_CHARACTERS", "TestCharacter@80"));
}

}

#pragma GCC diagnostic pop
