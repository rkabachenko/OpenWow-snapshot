#pragma once

#include "openwow/net/transport/tcp_client.h"
#include "openwow/net/wotlk/protocol/auth_crypt.h"
#include "openwow/net/wotlk/addon_handshake.h"
#include "openwow/net/wotlk/realm_list.h"
#include "openwow/network/protocol/wotlk/world_packet.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace openwow::net::wotlk {

struct CharacterSummary {
  std::uint64_t id{0};
  std::string name;
  int level{1};
  std::uint8_t race_id{1};
  std::uint8_t class_id{1};
  std::uint8_t gender{0};
  std::uint8_t skin{0};
  std::uint8_t face{0};
  std::uint8_t hair_style{0};
  std::uint8_t hair_color{0};
  std::uint8_t facial_hair{0};
  std::uint32_t zone_id{0};
  std::uint32_t map_id{0};
  float x{0}, y{0}, z{0};
  std::uint32_t guild_id{0};
  std::string guild_name;
  std::uint32_t char_flags{0};
  std::uint32_t at_login_flags{0};
  bool is_first_login{false};
  std::uint32_t pet_display_id{0};
  std::uint32_t pet_level{0};
  std::uint32_t pet_family{0};

  struct EquipmentSlot {
    std::uint32_t display_id{0};
    std::uint8_t inv_type{0};
    std::uint32_t enchant_id{0};
  };
  std::array<EquipmentSlot, 23> equipment{};
};

enum class WorldAuthStatus {
  kSuccess,
  kNetworkError,
  kAuthFailed,
  kQueuePosition,
  kCancelled,
};

struct WorldAuthResult {
  WorldAuthStatus status{WorldAuthStatus::kNetworkError};
  std::string message;
  std::uint8_t result_code{0};
  bool has_account_info{false};
  std::uint32_t queue_position{0};
  bool has_queue_position{false};
  std::uint8_t expansion_level{0};
  std::uint32_t billing_time{0};
  std::uint8_t billing_flags{0};
  std::uint32_t billing_rested{0};
  std::uint8_t free_character_migration{0};
};

using WorldAuthProgressCallback = std::function<void(const WorldAuthResult&)>;

enum class WorldEnterStatus {
  kSuccess,
  kInvalidCharacter,
  kInvalidRealmAddress,
  kNetworkError,
  kLoginFailed,
};

enum class RealmSessionPhase : std::uint8_t {
  kDisconnected,
  kConnected,
  kAuthenticating,
  kQueued,
  kCharacterSelect,
  kEnteringWorld,
  kWorld,
};

struct RealmHeartbeatSnapshot {
  std::uint32_t sequence{0};
  std::uint32_t last_send_tick_ms{0};
  std::uint32_t average_latency_ms{0};
  std::uint32_t sample_count{0};
};

struct WorldEnterResult {
  WorldEnterStatus status{WorldEnterStatus::kNetworkError};
  std::string message;
  std::uint8_t result_code{0};
  std::uint32_t map_id{0};
  float x{0}, y{0}, z{0}, orientation{0};
};

struct CharacterListResult {
  bool ok{false};
  std::string message;
  std::vector<CharacterSummary> characters;
  std::array<std::uint32_t, 10> trailing_u32s{};
};

struct CharacterCreateResult {
  bool ok{false};
  std::uint8_t result_code{0};
  std::string message;
};

struct CharacterDeleteResult {
  bool ok{false};
  std::uint8_t result_code{0};

  bool server_outcome_unknown{false};
  std::string message;
};

struct CharacterRenameResult {
  bool ok{false};
  std::uint8_t result_code{0};
  std::string message;
  std::uint64_t guid{0};
  std::string new_name;
};

struct CharacterDeclinedNamesResult {
  bool ok{false};
  std::uint32_t result_code{0xFFFFFFFFu};
  std::uint64_t guid{0};
};

struct CharacterCustomizeResult {
  bool ok{false};
  std::uint8_t result_code{0};
  std::string message;
  std::uint64_t guid{0};
  std::string name;
  std::uint8_t gender{0};
  std::uint8_t skin{0};
  std::uint8_t face{0};
  std::uint8_t hair_style{0};
  std::uint8_t hair_color{0};
  std::uint8_t facial_hair{0};
};

struct CharacterFactionOrRaceChangeResult {
  bool ok{false};
  std::uint8_t result_code{0};
  std::string message;
  std::uint64_t guid{0};
  std::string name;
  std::uint8_t race{0};
  std::uint8_t gender{0};
  std::uint8_t skin{0};
  std::uint8_t face{0};
  std::uint8_t hair_style{0};
  std::uint8_t hair_color{0};
  std::uint8_t facial_hair{0};
};

struct RealmRoutingIds {
  std::uint32_t region_id{0};
  std::uint32_t battlegroup_id{0};
  std::uint32_t realm_id{0};

  bool operator==(const RealmRoutingIds&) const = default;
};

std::uint32_t ComputeRealmAuthClientSeed(std::uint32_t tick_count);

std::array<std::uint8_t, 20> ComputeRealmAuthSessionProof(
    const std::string& account_name,
    std::uint32_t client_seed,
    std::uint32_t auth_seed,
    const std::array<std::uint8_t, 40>& session_key);

class RealmSession {
 public:
  RealmSession();
  ~RealmSession();

  RealmSession(const RealmSession&) = delete;
  RealmSession& operator=(const RealmSession&) = delete;

  void SetSessionKey(const std::uint8_t key[40]);
  void SetSessionKey(const std::array<std::uint8_t, 40>& key);
  void SetAccountName(const std::string& account_name);

  void SetSessionToken(const std::string& session_token);
  const std::string& session_token() const;
  const std::string& account_name() const { return account_name_; }

  [[nodiscard]] const std::array<std::uint8_t, 40>& session_key() const { return session_key_; }
  [[nodiscard]] bool has_session_key() const { return has_session_key_; }
  [[nodiscard]] bool addon_info_received() const { return addon_info_received_; }
  [[nodiscard]] std::uint32_t client_cache_version() const {
    return client_cache_version_;
  }
  [[nodiscard]] const RealmRoutingIds& realm_routing_ids() const {
    return realm_routing_ids_;
  }
  [[nodiscard]] std::array<std::uint32_t, 3> auth_session_seed_words() const {
    return {realm_routing_ids_.region_id,
            realm_routing_ids_.battlegroup_id,
            realm_routing_ids_.realm_id};
  }

  void SetClientSeedProvider(std::function<std::uint32_t()> provider);
  void SetRealmRoutingIds(RealmRoutingIds routing_ids);

  void SetAuthSessionSeedWords(std::uint32_t seed0,
                               std::uint32_t seed1,
                               std::uint32_t seed2);
  void SetAddonInfoProcessedCallback(std::function<void()> callback);
  void SetDisconnectedCallback(std::function<void()> callback) {
    disconnected_callback_ = std::move(callback);
  }
  void SetClientCacheVersionCallback(
      std::function<void(std::uint32_t)> callback) {
    client_cache_version_callback_ = std::move(callback);
  }

  bool Connect(const RealmInfo& realm, std::uint32_t timeout_ms = 5000);
  bool connected() const;
  [[nodiscard]] RealmSessionPhase phase() const {
    return phase_.load(std::memory_order_acquire);
  }
  [[nodiscard]] bool authenticated() const {
    return authenticated_.load(std::memory_order_acquire);
  }
  const RealmInfo& realm() const;

  void CancelPendingIo();
  void Disconnect();

  bool ServiceHeartbeat(std::uint32_t now_tick_ms);
  bool ObserveConnectionPacket(const WorldPacket& packet,
                               std::uint32_t now_tick_ms);
  [[nodiscard]] RealmHeartbeatSnapshot heartbeat_snapshot() const;
  void SetPingSentCallback(
      std::function<void(std::uint32_t ,
                         std::uint32_t )> callback);

  bool ReturnToCharacterSelect();

  WorldAuthResult Authenticate(std::uint32_t timeout_ms = 10000,
                               WorldAuthProgressCallback progress_callback = {},
                               const std::function<bool()>& should_cancel = {});

  static WorldAuthResult ParseAuthResponse(const std::uint8_t* payload, std::size_t size);

  CharacterListResult FetchCharacterList(
      std::uint32_t timeout_ms = 5000,
      const std::function<bool()>& should_cancel = {});
  CharacterCreateResult CreateCharacter(const std::string& name,
                                        std::uint8_t race,
                                        std::uint8_t cls,
                                        std::uint8_t gender,
                                        std::uint8_t skin,
                                        std::uint8_t face,
                                        std::uint8_t hair_style,
                                        std::uint8_t hair_color,
                                        std::uint8_t facial_hair,
                                        std::uint32_t timeout_ms = 5000);
  CharacterDeleteResult DeleteCharacter(std::uint64_t guid, std::uint32_t timeout_ms = 5000);
  WorldEnterResult EnterWorld(std::uint64_t character_guid, std::uint32_t timeout_ms = 30000);

  CharacterRenameResult RenameCharacter(std::uint64_t guid, const std::string& new_name,
                                        std::uint32_t timeout_ms = 5000);

  CharacterDeclinedNamesResult SetPlayerDeclinedNames(
      std::uint64_t guid, std::string_view base_name,
      const std::array<std::string, 5>& declined_forms,
      std::uint32_t timeout_ms = 5000);

  CharacterCustomizeResult CustomizeCharacter(std::uint64_t guid, const std::string& name,
                                              std::uint8_t gender, std::uint8_t skin,
                                              std::uint8_t hair_style, std::uint8_t hair_color,
                                              std::uint8_t facial_hair, std::uint8_t face,
                                              std::uint32_t timeout_ms = 5000);

  CharacterFactionOrRaceChangeResult FactionChangeCharacter(
      std::uint64_t guid, const std::string& name,
      std::uint8_t gender, std::uint8_t skin,
      std::uint8_t hair_style, std::uint8_t hair_color,
      std::uint8_t facial_hair, std::uint8_t face, std::uint8_t race,
      std::uint32_t timeout_ms = 5000);

  CharacterFactionOrRaceChangeResult RaceChangeCharacter(
      std::uint64_t guid, const std::string& name,
      std::uint8_t gender, std::uint8_t skin,
      std::uint8_t hair_style, std::uint8_t hair_color,
      std::uint8_t facial_hair, std::uint8_t face, std::uint8_t race,
      std::uint32_t timeout_ms = 5000);

  bool SendPacket(const WorldPacket& pkt);
  bool RecvPacket(WorldPacket& out,
                  std::uint32_t timeout_ms = 5000,
                  const std::function<bool()>& should_cancel = {});

  bool TryReceiveRealmSplit(WorldPacket& out);

  bool TakeDeferredPacket(WorldPacket& out);
  std::vector<WorldPacket> DrainDeferredPackets();
  [[nodiscard]] std::size_t deferred_packet_count() const {
    return deferred_packets_.size();
  }

 private:
  struct IncomingFrameState {
    bool first_byte_decrypted{false};
    bool header_decrypted{false};
    std::size_t size_field_len{0};
    std::size_t header_len{0};
    std::size_t frame_len{0};

    void Reset() { *this = {}; }
  };

  struct RealmStream {
    openwow::net::TcpClient client;
    AuthCrypt crypt;
    std::vector<std::uint8_t> receive_buffer;
    IncomingFrameState incoming_frame;
  };

  enum class ConnectionRole : std::uint8_t {
    kPrimary,
    kSecondary,
  };

  enum class ConnectionPacketResult : std::uint8_t {
    kDeliver,
    kConsumed,
    kFatal,
  };

  RealmInfo realm_{};
  std::string account_name_;
  std::uint64_t pending_character_guid_{0};

  std::array<std::uint32_t, 10> char_enum_trailing_u32s_{};
  std::string session_token_;
  std::array<std::uint8_t, 40> session_key_{};
  bool has_session_key_{false};

  mutable std::mutex stream_mutex_;
  std::shared_ptr<RealmStream> primary_stream_;
  std::shared_ptr<RealmStream> secondary_stream_;
  std::atomic_bool connected_{false};
  std::atomic_bool authenticated_{false};
  std::atomic<RealmSessionPhase> phase_{RealmSessionPhase::kDisconnected};

  mutable std::mutex send_mutex_;
  mutable std::mutex operation_mutex_;

  static constexpr std::uint32_t kHeartbeatIntervalMs = 30'000;
  static constexpr std::size_t kHeartbeatSampleCapacity = 16;
  mutable std::mutex heartbeat_mutex_;
  std::uint32_t heartbeat_last_send_tick_ms_{0};
  std::uint32_t heartbeat_sequence_{0};
  std::uint32_t heartbeat_acknowledged_sequence_{0};
  std::array<std::uint32_t, kHeartbeatSampleCapacity> heartbeat_samples_{};
  std::size_t heartbeat_sample_begin_{0};
  std::size_t heartbeat_sample_write_{0};
  std::size_t heartbeat_sample_count_{0};
  std::function<void(std::uint32_t, std::uint32_t)> ping_sent_callback_;

  mutable std::mutex transfer_mutex_;
  std::thread secondary_receive_thread_;
  std::atomic_bool secondary_stop_requested_{false};
  std::atomic_bool secondary_event_pending_{false};
  std::deque<WorldPacket> secondary_control_packets_;
  bool secondary_disconnected_{false};
  std::uint32_t transfer_cookie_{0};

  bool transfer_pending_{false};
  std::deque<WorldPacket> held_outbound_packets_;

  std::deque<WorldPacket> deferred_packets_;

  bool ReceiveExpectedPacket(std::initializer_list<Opcode> expected_opcodes,
                             WorldPacket& out,
                             std::uint32_t timeout_ms,
                             const char* scope,
                             const std::function<bool()>& should_cancel = {});
  void ResetReceiveState();
  [[nodiscard]] std::shared_ptr<RealmStream> PrimaryStream() const;
  [[nodiscard]] std::shared_ptr<RealmStream> SecondaryStream() const;
  bool ReceivePacketFromStream(
      const std::shared_ptr<RealmStream>& stream,
      WorldPacket& out,
      std::uint32_t timeout_ms,
      const std::function<bool()>& should_cancel,
      const char* stream_name);

  bool SendPacketOnStream(const std::shared_ptr<RealmStream>& stream,
                          const WorldPacket& packet,
                          std::uint32_t timeout_ms = 5000,
                          const std::function<bool()>& should_cancel = {});
  ConnectionPacketResult HandleConnectionPacket(
      const WorldPacket& packet,
      ConnectionRole role);
  bool StartSecondaryConnection(std::uint32_t address_v4,
                                std::uint16_t port,
                                std::uint32_t transfer_cookie);
  void SecondaryReceiveMain(std::shared_ptr<RealmStream> stream,
                            std::string host,
                            std::uint16_t port);
  void PublishSecondaryControlPacket(WorldPacket packet);
  void PublishSecondaryDisconnect();
  bool TakeSecondaryEvent(WorldPacket& packet, bool& disconnected);
  void StopSecondaryConnection();
  void JoinSecondaryReceiveThread();
  void HandleSecondaryDisconnect();
  void CompleteConnectionTransfer(ConnectionRole source);
  void DeferPacket(WorldPacket packet);
  bool HandleGlueStartupPacket(const WorldPacket& pkt);
  bool HandleHandshakePacket(const WorldPacket& pkt);
  [[nodiscard]] bool CanPerformCharacterOperation() const;
  void RestoreCharacterSelectAfterFailedWorldEntry();
  void ResetHeartbeat(std::uint32_t baseline_tick_ms = 0);

  bool addon_info_received_{false};
  std::uint32_t client_cache_version_{0};
  RealmRoutingIds realm_routing_ids_{};
  std::function<std::uint32_t()> client_seed_provider_;
  std::function<void()> addon_info_processed_callback_;
  std::function<void(std::uint32_t)> client_cache_version_callback_;
  std::function<void()> disconnected_callback_;
};

std::vector<CharacterSummary> FetchCharacterList();
std::vector<CharacterSummary> ParseCharacterList(const std::string& serialized);

}
