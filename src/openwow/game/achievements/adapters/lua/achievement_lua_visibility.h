#pragma once

#include "openwow/data/formats/dbc/dbc_entries_extended.h"
#include "openwow/data/formats/dbc/dbc_loader.h"

#include <cstdint>

namespace openwow::ui::game::detail {

inline const openwow::data::dbc::AchievementEntry* FindVisibleAchievement(
    const openwow::data::dbc::DbcLoader& dbc,
    const std::int32_t achievement_id) {
  constexpr std::uint32_t kHiddenFlag = 0x2;
  if (achievement_id < 0) {
    return nullptr;
  }

  const auto* achievement =
      dbc.achievement().LookupEntry(
          static_cast<std::uint32_t>(achievement_id));
  if (achievement == nullptr || (achievement->flags & kHiddenFlag) != 0) {
    return nullptr;
  }
  return achievement;
}

}
