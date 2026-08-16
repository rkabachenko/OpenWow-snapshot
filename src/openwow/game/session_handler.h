
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "openwow/runtime/time/game_time.h"
#include "openwow/game/game_time_callback_registry.h"
#include "openwow/game/object_guid.h"
#include "openwow/game/packet_reader.h"
#include "openwow/net/wotlk/main_thread_packet_dispatcher.h"

namespace openwow::game {

struct GameTimeInfo {
  std::uint32_t packed_time = 0;
  float game_speed = openwow::core::ida::kRetailMinimumGameSpeed;

  std::uint32_t tz_hint = 0;
};

struct AccountDataTimesInfo {
  std::uint32_t server_time = 0;
  std::uint32_t mask = 0;
  std::vector<std::uint32_t> timestamps;
};

inline constexpr std::uint32_t kTransferPendingNoPreviousMap = 0xFFFFFFFFu;

struct TransferPendingInfo {
  std::uint32_t map_id = 0;
  bool has_map_change_details = false;
  std::uint32_t transport_entry = 0;
  std::uint32_t previous_map_id = kTransferPendingNoPreviousMap;
};

struct NewWorldInfo {
  std::uint32_t map_id = 0;
  float x = 0.0f, y = 0.0f, z = 0.0f, orientation = 0.0f;
  bool fully_consumed = false;
};

struct LogoutResponseInfo {
  std::uint32_t result = 0;
  bool instant = false;
};

struct AccountDataUpdate {
  std::uint64_t guid = 0;
  std::uint32_t type = 0;
  std::uint32_t time = 0;
  std::uint32_t decompressed_size = 0;
  std::vector<std::uint8_t> compressed_data;
};

struct AccountDataComplete {
  std::uint32_t type = 0;
  std::uint32_t unk = 0;
};

struct DeclinedNamesResult {
  std::uint32_t result = 0;
  std::uint64_t guid = 0;
};

struct GameTimeUpdate {
  std::uint32_t time = 0;
  std::uint32_t unk = 0;
};

struct MoveFlagInfo {
  ObjectGuid guid{0};
  std::uint32_t counter = 0;
};

struct KnockBackInfo {
  ObjectGuid guid{0};
  std::uint32_t counter = 0;
  float cos_angle = 0.0f;
  float sin_angle = 0.0f;
  float speed_xy = 0.0f;
  float speed_z = 0.0f;
};

enum class MirrorTimerType : std::uint32_t {
  kFatigue = 0,
  kBreath = 1,
  kFeignDeath = 2,
};

struct MirrorTimerStart {
  MirrorTimerType type{};
  std::uint32_t value = 0;
  std::uint32_t max_value = 0;
  std::int32_t scale = 0;
  bool paused = false;
  std::uint32_t spell_id = 0;
};

struct MirrorTimerStop {
  MirrorTimerType type{};
};

struct ProficiencyInfo {
  std::uint8_t item_class = 0;
  std::uint32_t subclass_mask = 0;
};

struct ComboPointInfo {
  ObjectGuid target{0};
  std::uint8_t points = 0;
};

struct PlaySoundInfo {
  std::uint32_t sound_id = 0;
};

struct StandStateInfo {
  std::uint8_t state = 0;
};

struct SessionPacketEffects {
  std::function<void(const AccountDataTimesInfo&)> account_data_times;
  std::function<void(const LogoutResponseInfo&)> logout_response;
  std::function<void(const AccountDataUpdate&)> account_data_update;
  std::function<void(std::uint32_t)> client_cache_version;
};

class SessionHandler {
 public:
  explicit SessionHandler(
      openwow::core::ida::GameTimeData* shared_game_time = nullptr) noexcept;
  SessionHandler(const SessionHandler&) = delete;
  SessionHandler& operator=(const SessionHandler&) = delete;
  SessionHandler(SessionHandler&&) = delete;
  SessionHandler& operator=(SessionHandler&&) = delete;

  void BindWorldPacketHandlers(
      net::wotlk::MainThreadPacketDispatcher& dispatcher,
      SessionPacketEffects effects);

  bool HandleLoginSetTimeSpeed(const std::uint8_t* data, std::size_t len);
  bool HandleGameTimeSet(const std::uint8_t* data, std::size_t len);
  bool HandleGameSpeedSet(const std::uint8_t* data, std::size_t len);
  bool HandleAccountDataTimes(const std::uint8_t* data, std::size_t len);
  bool HandleTransferPending(const std::uint8_t* data, std::size_t len);
  bool HandleNewWorld(const std::uint8_t* data, std::size_t len);
  bool HandleLogoutResponse(const std::uint8_t* data, std::size_t len);
  bool HandleLogoutComplete(const std::uint8_t* data, std::size_t len);

  bool HandleForceMoveRoot(const std::uint8_t* data, std::size_t len);
  bool HandleForceMoveUnroot(const std::uint8_t* data, std::size_t len);
  bool HandleMoveKnockBack(const std::uint8_t* data, std::size_t len);
  bool HandleMoveSetCanFly(const std::uint8_t* data, std::size_t len);
  bool HandleMoveUnsetCanFly(const std::uint8_t* data, std::size_t len);

  bool HandleStartMirrorTimer(const std::uint8_t* data, std::size_t len);
  bool HandleStopMirrorTimer(const std::uint8_t* data, std::size_t len);

  bool HandleSetProficiency(const std::uint8_t* data, std::size_t len);
  bool HandleStandStateUpdate(const std::uint8_t* data, std::size_t len);
  bool HandleUpdateComboPoints(const std::uint8_t* data, std::size_t len);
  bool HandlePlaySound(const std::uint8_t* data, std::size_t len);

  bool HandleUpdateAccountData(const std::uint8_t* data, std::size_t len);
  bool HandleClientCacheVersion(const std::uint8_t* data, std::size_t len);

  bool HandleUpdateAccountDataComplete(const std::uint8_t* data, std::size_t len);
  bool HandleDeclinedNamesResult(const std::uint8_t* data, std::size_t len);
  bool HandleGameTimeUpdate(const std::uint8_t* data, std::size_t len);

  [[nodiscard]] GameTimeInfo game_time() const;
  [[nodiscard]] const openwow::core::ida::GameTimeData& game_time_data() const {
    return *game_time_;
  }
  [[nodiscard]] const AccountDataTimesInfo& account_data() const { return acct_data_; }
  [[nodiscard]] const TransferPendingInfo& transfer_pending() const { return transfer_; }
  [[nodiscard]] const NewWorldInfo& new_world() const { return new_world_; }
  [[nodiscard]] const LogoutResponseInfo& logout_response() const { return logout_resp_; }
  [[nodiscard]] bool logout_complete() const { return logout_complete_; }
  bool ConsumeLogoutComplete();

  [[nodiscard]] const MoveFlagInfo& last_root() const { return last_root_; }
  [[nodiscard]] const MoveFlagInfo& last_unroot() const { return last_unroot_; }
  [[nodiscard]] const KnockBackInfo& last_knockback() const { return last_kb_; }
  [[nodiscard]] const MoveFlagInfo& last_can_fly() const { return last_can_fly_; }
  [[nodiscard]] const MoveFlagInfo& last_unset_fly() const { return last_unset_fly_; }
  [[nodiscard]] bool is_rooted() const { return rooted_; }
  [[nodiscard]] bool can_fly() const { return can_fly_; }

  [[nodiscard]] const MirrorTimerStart& mirror_timer_start() const { return mirror_start_; }
  [[nodiscard]] const MirrorTimerStop& mirror_timer_stop() const { return mirror_stop_; }

  [[nodiscard]] const ProficiencyInfo& last_proficiency() const { return proficiency_; }
  [[nodiscard]] std::uint32_t GetProficiencyMask(std::uint8_t item_class) const {
    return item_class < proficiency_masks_.size() ? proficiency_masks_[item_class] : 0;
  }
  [[nodiscard]] const StandStateInfo& stand_state() const { return stand_state_; }
  [[nodiscard]] const ComboPointInfo& combo_points() const { return combo_; }
  [[nodiscard]] const PlaySoundInfo& last_sound() const { return last_sound_; }

  [[nodiscard]] const std::optional<AccountDataUpdate>& last_account_data_update() const {
    return last_account_data_update_;
  }
  [[nodiscard]] std::uint32_t client_cache_version() const {
    return client_cache_version_;
  }

  [[nodiscard]] const std::optional<AccountDataComplete>& last_account_data_complete() const {
    return last_account_data_complete_;
  }
  [[nodiscard]] const std::optional<DeclinedNamesResult>& last_declined_names_result() const {
    return last_declined_names_result_;
  }
  [[nodiscard]] const std::optional<GameTimeUpdate>& last_game_time_update() const {
    return last_game_time_update_;
  }
  [[nodiscard]] GameTimeCallbackRegistry& game_time_callbacks() {
    return game_time_callbacks_;
  }
  [[nodiscard]] const GameTimeCallbackRegistry& game_time_callbacks() const {
    return game_time_callbacks_;
  }
  [[nodiscard]] float GetGameTimeHourOfDay() const;

  void SetPackedGameTime(std::uint32_t packed_time,
                         std::uint32_t timezone_hint = 0,
                         bool notify_current_minute = false);

  void AdvanceGameTime(float dt_seconds);
  void Clear();

 private:

  openwow::core::ida::GameTimeData owned_game_time_{};
  openwow::core::ida::GameTimeData* game_time_{&owned_game_time_};
  AccountDataTimesInfo acct_data_{};
  TransferPendingInfo transfer_{};
  NewWorldInfo new_world_{};
  LogoutResponseInfo logout_resp_{};
  bool logout_complete_ = false;

  MoveFlagInfo last_root_{};
  MoveFlagInfo last_unroot_{};
  KnockBackInfo last_kb_{};
  MoveFlagInfo last_can_fly_{};
  MoveFlagInfo last_unset_fly_{};
  bool rooted_ = false;
  bool can_fly_ = false;

  MirrorTimerStart mirror_start_{};
  MirrorTimerStop mirror_stop_{};

  ProficiencyInfo proficiency_{};
  std::array<std::uint32_t, 17> proficiency_masks_{};
  StandStateInfo stand_state_{};
  ComboPointInfo combo_{};
  PlaySoundInfo last_sound_{};

  std::optional<AccountDataUpdate> last_account_data_update_;
  std::uint32_t client_cache_version_ = 0;

  std::optional<AccountDataComplete> last_account_data_complete_;
  std::optional<DeclinedNamesResult> last_declined_names_result_;
  std::optional<GameTimeUpdate> last_game_time_update_;
  GameTimeCallbackRegistry game_time_callbacks_{};
  SessionPacketEffects packet_effects_{};
  std::vector<net::wotlk::MainThreadPacketDispatcher::Registration>
      packet_registrations_;

  bool ReadMoveFlagPacket(const std::uint8_t* data, std::size_t len,
                          MoveFlagInfo& out);
};

}
