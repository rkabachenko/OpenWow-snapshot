
#include "openwow/game/quest_xp_calc.h"

#include <algorithm>

namespace openwow::game {

std::uint32_t QuestXPCalc::CalculateRewardXP(
    const openwow::data::dbc::DbcLoader& dbc,
    const std::int32_t quest_level,
    const std::uint32_t player_level,
    const std::uint32_t xp_group_id) {
  if (xp_group_id >= openwow::data::dbc::QuestXPEntry{}.difficulty.size()) {
    return 0;
  }

  const auto effective_level =
      quest_level != -1 ? quest_level : static_cast<std::int32_t>(player_level);
  if (effective_level < 0) {
    return 0;
  }

  const auto* xp_row =
      dbc.quest_xp().LookupEntry(static_cast<std::uint32_t>(effective_level));
  if (xp_row == nullptr) {
    return 0;
  }

  const auto base_xp = xp_row->difficulty[xp_group_id];
  if (base_xp == 0) {
    return 0;
  }

  const auto raw_scale =
      2 * (effective_level - static_cast<std::int32_t>(player_level)) + 20;
  const auto scale = std::clamp(raw_scale, 1, 10);
  const auto scaled_xp =
      static_cast<std::uint32_t>(scale * static_cast<std::int32_t>(base_xp) / 10);

  if (scaled_xp <= 100) {
    return 5u * ((scaled_xp + 2u) / 5u);
  }
  if (scaled_xp <= 500) {
    return 10u * ((scaled_xp + 5u) / 10u);
  }
  if (scaled_xp <= 1000) {
    return 25u * ((scaled_xp + 12u) / 25u);
  }
  return 50u * ((scaled_xp + 25u) / 50u);
}

}
