#include "openwow/game/achievements/adapters/lua/achievement_activity_lua_api.h"

#include "openwow/data/formats/dbc/dbc_entries_extended.h"
#include "openwow/game/achievements/model/achievement_completion_order.h"
#include "openwow/game/achievements/rules/achievement_category_resolver.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/lua_result_capacity.h"
#include "openwow/ui/runtime/lua/lua_call.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace openwow::ui::game::detail {
namespace {

constexpr std::size_t kLatestCompletedAchievementLimit = 5;

struct CompletedAchievementView {
  openwow::game::AchievementId id;
  openwow::game::PackedAchievementTime completion_date;
};

bool NewerCompletionSortsFirst(
    const openwow::data::dbc::DbcLoader& dbc,
    const CompletedAchievementView& lhs,
    const CompletedAchievementView& rhs) {
  const auto* lhs_metadata = dbc.achievement().LookupEntry(lhs.id.value);
  const auto* rhs_metadata = dbc.achievement().LookupEntry(rhs.id.value);
  return openwow::game::CompletedAchievementSortsBefore(
      lhs.completion_date.ToWireValue(),
      lhs_metadata != nullptr ? lhs_metadata->order_in_group : 0,
      rhs.completion_date.ToWireValue(),
      rhs_metadata != nullptr ? rhs_metadata->order_in_group : 0);
}

template <typename AchievementRange>
std::vector<CompletedAchievementView> CollectLatestCompletions(
    const openwow::data::dbc::DbcLoader& dbc,
    const AchievementRange& achievements) {
  std::vector<CompletedAchievementView> latest;
  for (const auto& achievement : achievements) {
    if (!openwow::game::AchievementResolvesOutsideStatisticsTree(
            dbc, achievement.id)) {
      continue;
    }
    latest.push_back({achievement.id, achievement.completion_date});
  }

  std::stable_sort(
      latest.begin(), latest.end(),
      [&dbc](const CompletedAchievementView& lhs,
             const CompletedAchievementView& rhs) {
        return NewerCompletionSortsFirst(dbc, lhs, rhs);
      });
  if (latest.size() > kLatestCompletedAchievementLimit) {
    latest.resize(kLatestCompletedAchievementLimit);
  }
  return latest;
}

int PushAchievementIds(
    lua_State* state,
    const std::vector<CompletedAchievementView>& achievements,
    const char* capacity_context) {
  const int result_count = openwow::ui::ReserveLuaResultCapacity(
      state, achievements.size(), capacity_context);
  openwow::ui::lua::LuaCall results(state);
  for (const auto& achievement : achievements) {
    results.PushNumber(achievement.id.value);
  }
  return result_count;
}

int PushCriteriaIds(
    lua_State* state,
    const std::vector<openwow::game::AchievementCriteriaId>& criteria_ids) {
  const int result_count = openwow::ui::ReserveLuaResultCapacity(
      state, criteria_ids.size(), "achievement criteria IDs");
  openwow::ui::lua::LuaCall results(state);
  for (const auto criteria_id : criteria_ids) {
    results.PushNumber(criteria_id.value);
  }
  return result_count;
}

}

int LuaGetLatestCompletedAchievements(lua_State* state) {
  const auto* session = GetWorldSession(state);
  const auto* dbc = GetDbcLoader(state);
  if (session == nullptr || dbc == nullptr) {
    return 0;
  }

  return PushAchievementIds(
      state,
      CollectLatestCompletions(
          *dbc, session->achievements().completed_list()),
      "completed achievement IDs");
}

int LuaGetLatestUpdatedStats(lua_State* state) {
  const auto* session = GetWorldSession(state);
  return session != nullptr
             ? PushCriteriaIds(
                   state, session->achievements().latest_updated_stats())
             : 0;
}

int LuaGetLatestCompletedComparisonAchievements(lua_State* state) {
  const auto* session = GetWorldSession(state);
  const auto* dbc = GetDbcLoader(state);
  if (session == nullptr || dbc == nullptr ||
      session->achievements().comparison_data_status() !=
          openwow::game::ComparisonAchievementDataStatus::kAvailable) {
    return 0;
  }

  return PushAchievementIds(
      state,
      CollectLatestCompletions(
          *dbc, session->achievements().last_inspect().achievements),
      "comparison achievement IDs");
}

int LuaGetLatestUpdatedComparisonStats(lua_State* state) {
  const auto* session = GetWorldSession(state);
  if (session == nullptr ||
      session->achievements().comparison_data_status() !=
          openwow::game::ComparisonAchievementDataStatus::kAvailable) {
    return 0;
  }
  return PushCriteriaIds(
      state,
      session->achievements().latest_updated_comparison_stats());
}

}
