
#pragma once

#include <cstdint>

namespace openwow::game {

[[nodiscard]] bool CanSaveTabardNow(std::uint32_t guild_id,
                                     bool has_pending_save);

struct UpdatePVPTimerParams {
  std::uint32_t new_player_flags;
  std::uint32_t old_player_flags;
  std::uint32_t current_time;
};

struct UpdatePVPTimerResult {
  std::uint32_t new_timer_value;
  bool timer_changed;
};

[[nodiscard]] UpdatePVPTimerResult
UpdatePVPTimerOnFlagsChanged(const UpdatePVPTimerParams& params);

struct PvpTimerResult {
  std::int32_t remaining_ms;
};

[[nodiscard]] PvpTimerResult GetPVPTimer(std::uint32_t player_flags,
                                          std::uint32_t pvp_timer_value,
                                          std::uint32_t current_time);

enum class GrantLevelEligibilityResult : std::uint8_t {
  kSuccess                    = 0,
  kTargetNotReferAFriendLinked = 1,
  kTargetLevelTooHigh         = 2,
  kGrantLevelUnavailable      = 3,
  kNotAPlayer                 = 4,
  kDifferentFaction           = 5,
  kCannotAssist               = 6,
  kTargetMaxLevel             = 7,
  kInvalidTarget              = 8,
};

struct GrantLevelEligibilityParams {
  std::uint64_t target_guid;
  std::uint32_t source_level;
  std::uint32_t source_faction_group_mask;
  std::uint32_t target_level;
  std::uint32_t target_faction_group_mask;
  bool target_is_player;
  bool has_refer_a_friend_link;
  bool can_assist;
  bool has_grant_level_availability;

};

[[nodiscard]] GrantLevelEligibilityResult
CheckGrantLevelEligibility(const GrantLevelEligibilityParams& params);

}
