#include "openwow/game/achievements/adapters/lua/achievement_link_lua_api.h"

#include "openwow/game/achievements/adapters/lua/achievement_progress_formatter.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/tooltip_formatter.h"
#include "openwow/ui/lua_numeric.h"
#include "openwow/ui/runtime/lua/lua_call.h"

#include <array>
#include <cstdint>

namespace openwow::ui::game::detail {
namespace {

constexpr std::uint32_t kHiddenIndexedCriteriaFlag = 0x2;

struct AchievementLinkDateFields final {
  int month = -1;
  int day = -1;
  int year = -1;
};

std::array<std::uint32_t, 4> BuildAchievementLinkCriteriaMask(
    const openwow::data::dbc::DbcLoader& dbc,
    const openwow::data::dbc::AchievementEntry& achievement,
    const std::unordered_map<openwow::game::AchievementCriteriaId,
                             openwow::game::CriteriaProgress,
                             openwow::game::AchievementCriteriaIdHash>&
        progress_entries) {
  std::array<std::uint32_t, 4> masks{};
  const auto* root = ResolveCriteriaRootAchievement(dbc, achievement);
  std::uint32_t visible_criteria_index = 0;

  for (const auto* criteria : CollectAchievementCriteriaEntries(
           dbc, openwow::game::AchievementId{root->id})) {
    if ((criteria->flags & kHiddenIndexedCriteriaFlag) != 0) {
      continue;
    }

    const auto progress_it = progress_entries.find(
        openwow::game::AchievementCriteriaId{criteria->id});
    if (progress_it != progress_entries.end() &&
        progress_it->second.counter.value >= criteria->quantity &&
        visible_criteria_index < 128) {
      const auto mask_index = visible_criteria_index / 32;
      const auto bit_index = visible_criteria_index % 32;
      masks[mask_index] |= (1u << bit_index);
    }
    ++visible_criteria_index;
  }
  return masks;
}

AchievementLinkDateFields BuildAchievementLinkDateFields(
    const openwow::game::CompletedAchievement* completed_achievement) {
  if (completed_achievement == nullptr) {
    return {};
  }
  const auto packed_time =
      completed_achievement->completion_date.ToWireValue();
  return {
      .month = static_cast<int>((packed_time >> 20) & 0xFu),
      .day = static_cast<int>((packed_time >> 14) & 0x3Fu),
      .year = static_cast<int>((packed_time >> 24) & 0x1Fu),
  };
}

}

int LuaGetAchievementLink(lua_State* state) {
  const openwow::ui::lua::LuaCall arguments(state);
  const auto achievement_id = openwow::game::AchievementId{
      TruncateLuaNumberToWrappedLowU32(arguments.RequireNumber(
          1, "Usage: GetAchievementLink(achievementID)"))};

  const auto* session = GetWorldSession(state);
  const auto* dbc = GetDbcLoader(state);
  if (session == nullptr || dbc == nullptr) {
    return 0;
  }
  const auto* achievement =
      dbc->achievement().LookupEntry(achievement_id.value);
  if (achievement == nullptr) {
    return 0;
  }

  const auto* completed_achievement =
      session->achievements().FindCompletedAchievement(achievement_id);
  const bool is_completed = completed_achievement != nullptr;
  auto criteria_masks = BuildAchievementLinkCriteriaMask(
      *dbc, *achievement, session->achievements().criteria());
  const auto* root = ResolveCriteriaRootAchievement(*dbc, *achievement);
  if (is_completed && root->count == 0) {
    criteria_masks.fill(0xFFFFFFFFu);
  }

  const auto date = BuildAchievementLinkDateFields(completed_achievement);
  return openwow::ui::lua::LuaCall(state)
      .PushString(FormatAchievementLink(
          achievement_id.value,
          session->objects().GetLocalPlayerGuid().GetRawValue(),
          is_completed ? 1u : 0u, date.month, date.day, date.year,
          criteria_masks[0], criteria_masks[1], criteria_masks[2],
          criteria_masks[3]))
      .ResultCount();
}

}
