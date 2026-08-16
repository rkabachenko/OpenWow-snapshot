
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "openwow/game/object_guid.h"
#include "openwow/game/packet_reader.h"

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::game {

struct DifficultyUpdate {
  std::uint32_t difficulty = 0;
  std::uint32_t update_default = 0;
  std::uint32_t update_group = 0;
};

struct RaidLockout {
  std::uint32_t map_id = 0;
  std::uint32_t difficulty = 0;
  std::uint64_t lockout_id = 0;
  std::uint8_t locked = 0;
  std::uint8_t extended = 0;
  std::uint32_t reset_time = 0;
};

struct InstanceBootWarning {
  std::uint32_t remaining_ms = 0;
  std::uint32_t reason = 0;
};

enum class InstanceResetFailReason : std::uint32_t {
  kPlayersInside = 0,
  kOfflineMembers = 1,
  kZoning = 2,
};

struct InstanceResetFailed {
  std::uint32_t reason = 0;
  std::uint32_t map_id = 0;
};

enum class EncounterFrameType : std::uint32_t {
  kEngage = 0,
  kDisengage = 1,
  kUpdatePriority = 2,
  kAddTimer = 3,
  kAddObjective = 4,
  kUpdateObjective = 5,
  kRemoveObjective = 6,
  kRefreshFrames = 7,
};

struct EncounterUpdate {
  std::uint32_t type = 0;
  ObjectGuid unit{ObjectGuid(0)};
  std::uint8_t param1 = 0;
  std::uint8_t param2 = 0;
};

struct EncounterUnitFrame {
  ObjectGuid guid{ObjectGuid(0)};
  std::uint8_t priority = 0;
};

struct EncounterTimer {
  std::uint8_t duration_offset_ms = 0;
  std::uint32_t deadline_ms = 0;
};

struct EncounterObjective {
  std::uint8_t id = 0;
  std::uint32_t progress = 0;
};

struct InstanceLockWarning {
  std::uint32_t time_remaining = 0;
  std::uint32_t encounter_mask = 0;
  std::uint8_t extend_lock = 0;
};

struct ActiveInstanceLock {
  std::uint32_t remaining_ms = 0;
  std::int32_t remaining_seconds = 0;
  std::uint32_t encounter_mask = 0;
  bool extend_lock = false;
};

struct ResetInstanceVisibilityState {
  std::uint32_t anchor_map_id = 0;
  std::uint32_t anchor_unix_time = 0;
};

struct RaidInstanceMessage {
  std::uint32_t type = 0;
  std::uint32_t map_id = 0;
  std::uint32_t difficulty = 0;
  std::uint32_t time_remaining = 0;
  std::uint8_t welcome_flag1 = 0;
  std::uint8_t welcome_flag2 = 0;
};

enum class PlayerDifficultyChangeResultCode : std::uint32_t {
  kCurrentDifficulty = 0,
  kCooldownMessage = 1,
  kWorldState = 2,
  kEncounter = 3,
  kCombat = 4,
  kPlayerBusy = 5,
  kCooldownStarted = 6,
  kAlreadyStarted = 7,
  kChatMessage = 8,
  kChanged = 9,
};

struct PlayerDifficultyChangeResult {
  std::uint32_t code = 0;
  std::uint32_t value = 0;
};

std::string FormatDungeonNameWithDifficulty(const data::dbc::DbcLoader *dbc,
                                            std::uint32_t map_id,
                                            std::uint32_t difficulty);

std::string FormatRoundedSpellDurationText(std::uint32_t remaining_ms);
std::string FormatRoundedGeneralDurationText(std::uint32_t remaining_ms);
std::string FormatDifficultyChangeCooldownText(std::uint32_t remaining_ms);
void RegisterInstanceConsoleCommands();
void UnregisterInstanceConsoleCommands();

class InstanceHandler {
public:
  static constexpr std::size_t kMaxEncounterUnitFrames = 16;

  bool HandleSetDungeonDifficulty(const std::uint8_t *data, std::size_t len);
  bool HandleSetRaidDifficulty(const std::uint8_t *data, std::size_t len);
  bool HandleRaidInstanceInfo(const std::uint8_t *data, std::size_t len);
  bool HandleInstanceReset(const std::uint8_t *data, std::size_t len);
  bool HandleInstanceResetFailed(const std::uint8_t *data, std::size_t len);
  bool HandleEncounterUpdate(const std::uint8_t *data, std::size_t len);
  bool HandleRaidGroupOnly(const std::uint8_t *data, std::size_t len);
  bool HandleChangePlayerDifficultyResult(const std::uint8_t *data, std::size_t len,
                                          std::uint32_t current_unix_time);

  bool HandleInstanceLockWarning(const std::uint8_t *data, std::size_t len);
  bool HandleInstanceSaveCreated(const std::uint8_t *data, std::size_t len);
  bool HandleUpdateLastInstance(const std::uint8_t *data, std::size_t len);
  bool HandleUpdateInstanceOwnership(const std::uint8_t *data, std::size_t len);

  bool HandleRaidInstanceMessage(const std::uint8_t *data, std::size_t len);
  bool HandleViewPhaseShift(const std::uint8_t *data, std::size_t len);

  const DifficultyUpdate &dungeon_difficulty() const {
    return dungeon_diff_;
  }
  const DifficultyUpdate &raid_difficulty() const {
    return raid_diff_;
  }
  const std::vector<RaidLockout> &lockouts() const {
    return lockouts_;
  }
  [[nodiscard]] std::optional<RaidLockout> SetRaidLockoutExtended(std::size_t index,
                                                                  bool extended);
  const InstanceBootWarning &last_instance_boot_warning() const {
    return last_instance_boot_warning_;
  }
  [[nodiscard]] std::uint32_t last_reset_map() const {
    return last_reset_map_;
  }
  [[nodiscard]] std::uint32_t instance_boot_deadline_ms() const {
    return instance_boot_deadline_ms_;
  }
  [[nodiscard]] std::int32_t
  GetInstanceBootTimeRemainingSeconds(std::uint32_t current_tick_ms) const;
  const InstanceResetFailed &last_reset_failed() const {
    return last_reset_failed_;
  }
  const EncounterUpdate &last_encounter() const {
    return last_encounter_;
  }
  const std::vector<EncounterUnitFrame> &encounter_unit_frames() const {
    return encounter_unit_frames_;
  }
  const std::vector<EncounterTimer> &encounter_timers() const {
    return encounter_timers_;
  }
  const std::vector<EncounterObjective> &encounter_objectives() const {
    return encounter_objectives_;
  }
  [[nodiscard]] ObjectGuid encounter_unit_guid(std::size_t index) const {
    if (index >= encounter_unit_frames_.size())
      return ObjectGuid();
    return encounter_unit_frames_[index].guid;
  }
  [[nodiscard]] bool last_encounter_fires_unit_frame_event() const {
    return last_encounter_fires_unit_frame_event_;
  }

  [[nodiscard]] const std::optional<InstanceLockWarning> &last_lock_warning() const {
    return last_lock_warning_;
  }
  [[nodiscard]] ActiveInstanceLock GetActiveInstanceLock(std::uint32_t current_tick_ms) const;
  [[nodiscard]] std::uint32_t last_instance_save_created() const {
    return last_instance_save_created_;
  }
  [[nodiscard]] std::uint32_t last_instance_map_id() const {
    return last_instance_map_id_;
  }
  [[nodiscard]] std::uint32_t instance_save_count() const {
    return instance_save_count_;
  }
  [[nodiscard]] const ResetInstanceVisibilityState &reset_instance_visibility_state() const {
    return reset_instance_visibility_state_;
  }

  void SetResetInstanceVisibilityAnchor(std::uint32_t map_id, std::uint32_t current_unix_time);

  [[nodiscard]] const std::optional<RaidInstanceMessage> &last_raid_instance_msg() const {
    return last_raid_instance_msg_;
  }
  [[nodiscard]] std::uint32_t last_phase_mask() const {
    return phase_mask_;
  }
  [[nodiscard]] const PlayerDifficultyChangeResult &change_player_difficulty_result() const {
    return change_player_difficulty_result_;
  }
  [[nodiscard]] std::uint8_t player_difficulty() const {
    return player_difficulty_;
  }
  [[nodiscard]] std::uint32_t player_difficulty_change_cooldown_end_unix() const {
    return player_difficulty_change_cooldown_end_unix_;
  }
  [[nodiscard]] bool IsPlayerDifficultyChangeCooldownActive(std::uint32_t current_unix_time) const;
  [[nodiscard]] std::uint32_t
  GetPlayerDifficultyChangeCooldownRemainingMs(std::uint32_t current_unix_time) const;

  void Clear();

private:
  bool ParseDifficulty(const std::uint8_t *data, std::size_t len, DifficultyUpdate &out);
  void ResetActiveInstanceLockState();
  void ClearResetInstanceVisibilityAnchor();
  void ClearActiveInstanceLock();
  void SetActiveInstanceLock(std::uint32_t remaining_ms, std::uint32_t encounter_mask,
                             bool extend_lock, std::uint32_t current_tick_ms);
  void SetInstanceBootCountdownMs(std::uint32_t remaining_ms, std::uint32_t current_tick_ms);
  void PurgeExpiredEncounterTimers(std::uint32_t current_tick_ms);
  void SortEncounterUnitFrames();
  bool TryAddEncounterUnitFrame(ObjectGuid guid, std::uint8_t priority);
  bool TryUpdateEncounterUnitPriority(ObjectGuid guid, std::uint8_t priority);
  bool RemoveEncounterUnitFrame(ObjectGuid guid);

  DifficultyUpdate dungeon_diff_{};
  DifficultyUpdate raid_diff_{};
  std::vector<RaidLockout> lockouts_;
  InstanceBootWarning last_instance_boot_warning_{};
  std::uint32_t last_reset_map_ = 0;
  std::uint32_t instance_boot_deadline_ms_ = 0;
  InstanceResetFailed last_reset_failed_{};
  EncounterUpdate last_encounter_{};
  bool last_encounter_fires_unit_frame_event_ = false;
  std::vector<EncounterUnitFrame> encounter_unit_frames_;
  std::vector<EncounterTimer> encounter_timers_;
  std::vector<EncounterObjective> encounter_objectives_;

  std::optional<InstanceLockWarning> last_lock_warning_;
  std::uint32_t instance_lock_deadline_ms_ = 0;
  std::uint32_t instance_lock_encounter_mask_ = 0;
  bool instance_lock_extend_ = false;
  std::uint32_t last_instance_save_created_ = 0;
  std::uint32_t last_instance_map_id_ = 0;
  std::uint32_t instance_save_count_ = 0;
  ResetInstanceVisibilityState reset_instance_visibility_state_{};

  std::optional<RaidInstanceMessage> last_raid_instance_msg_;
  std::uint32_t phase_mask_ = 0;
  PlayerDifficultyChangeResult change_player_difficulty_result_{};
  std::uint8_t player_difficulty_ = 0;
  std::uint32_t player_difficulty_change_cooldown_end_unix_ = 0;
};

}
