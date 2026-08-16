
#include "openwow/game/player_pvp_info.h"

namespace openwow::game {

bool CanSaveTabardNow(std::uint32_t guild_id, bool has_pending_save) {
  if (guild_id == 0) return false;
  if (has_pending_save) return false;

  return true;
}

UpdatePVPTimerResult
UpdatePVPTimerOnFlagsChanged(const UpdatePVPTimerParams& params) {
  constexpr std::uint32_t kInPvP      = 0x200u;
  constexpr std::uint32_t kPvPTimer   = 0x40000u;
  constexpr std::uint32_t kTimerDuration = 300000u;

  if ((params.new_player_flags & kInPvP) != 0) {
    return {0, true};
  }

  bool new_has_pvp_timer = (params.new_player_flags & kPvPTimer) != 0;
  bool old_has_pvp_timer = (params.old_player_flags & kPvPTimer) != 0;

  if (!new_has_pvp_timer || !old_has_pvp_timer) {
    return {params.current_time + kTimerDuration, true};
  }

  return {0, false};
}

PvpTimerResult GetPVPTimer(std::uint32_t player_flags,
                            std::uint32_t pvp_timer_value,
                            std::uint32_t current_time) {
  if ((player_flags & 0x40000) == 0) {
    return {300000};
  }

  if (pvp_timer_value != 0) {
    std::int32_t diff =
        static_cast<std::int32_t>(current_time - pvp_timer_value);
    if (diff < 0) {
      return {static_cast<std::int32_t>(pvp_timer_value - current_time)};
    }
  }

  return {-1};
}

GrantLevelEligibilityResult
CheckGrantLevelEligibility(const GrantLevelEligibilityParams& params) {
  if (params.target_guid == 0) return GrantLevelEligibilityResult::kInvalidTarget;
  if (!params.target_is_player) return GrantLevelEligibilityResult::kNotAPlayer;
  if (!params.has_refer_a_friend_link) {
    return GrantLevelEligibilityResult::kTargetNotReferAFriendLinked;
  }

  if (params.source_faction_group_mask != 0 && params.target_faction_group_mask != 0 &&
      params.source_faction_group_mask != params.target_faction_group_mask) {
    return GrantLevelEligibilityResult::kDifferentFaction;
  }

  if (params.target_level >= 60) return GrantLevelEligibilityResult::kTargetMaxLevel;

  if (params.source_level <= params.target_level) {
    return GrantLevelEligibilityResult::kTargetLevelTooHigh;
  }

  if (!params.can_assist) return GrantLevelEligibilityResult::kCannotAssist;

  if (!params.has_grant_level_availability) {
    return GrantLevelEligibilityResult::kGrantLevelUnavailable;
  }

  return GrantLevelEligibilityResult::kSuccess;
}

}
