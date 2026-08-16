#include "openwow/game/achievements/adapters/lua/achievement_category_lua_api.h"

#include "openwow/data/formats/dbc/dbc_entries_extended.h"
#include "openwow/game/achievements/rules/achievement_category_resolver.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/lua_numeric.h"
#include "openwow/ui/runtime/lua/lua_call.h"

#include <cstdint>
#include <vector>

namespace openwow::ui::game::detail {
namespace {

constexpr std::uint32_t kAchievementHiddenFlag = 0x2;

int PushCategoryTable(lua_State* state,
                      const std::vector<std::uint32_t>& category_ids) {
  return openwow::ui::lua::LuaCall(state).PushNumberArrayTable(category_ids);
}

}

int LuaGetCategoryList(lua_State* state) {
  const auto* dbc = GetDbcLoader(state);
  if (dbc == nullptr) {
    return PushCategoryTable(state, {});
  }

  std::vector<std::uint32_t> category_ids;
  for (const auto& category : dbc->achievement_category()) {
    if (category.id != openwow::game::kAchievementStatisticsRootCategoryId &&
        !openwow::game::AchievementCategoryResolvesToStatisticsTree(
            *dbc, category.id)) {
      category_ids.push_back(category.id);
    }
  }
  return PushCategoryTable(state, category_ids);
}

int LuaGetStatisticsCategoryList(lua_State* state) {
  const auto* dbc = GetDbcLoader(state);
  if (dbc == nullptr) {
    return PushCategoryTable(state, {});
  }

  std::vector<std::uint32_t> category_ids;
  for (const auto& category : dbc->achievement_category()) {
    if (category.id != openwow::game::kAchievementStatisticsRootCategoryId &&
        openwow::game::AchievementCategoryResolvesToStatisticsTree(
            *dbc, category.id)) {
      category_ids.push_back(category.id);
    }
  }
  return PushCategoryTable(state, category_ids);
}

int LuaGetCategoryInfo(lua_State* state) {
  const openwow::ui::lua::LuaCall arguments(state);
  const auto category_id = openwow::ui::SaturateLuaNumberToU32(
      arguments.RequireNumber(1, "Usage: GetCategoryInfo(categoryID)"));

  const auto* dbc = GetDbcLoader(state);
  const auto* category =
      dbc != nullptr ? dbc->achievement_category().LookupEntry(category_id)
                     : nullptr;
  if (category == nullptr) {
    return 0;
  }

  openwow::ui::lua::LuaCall results(state);
  results.PushString(category->name);

  if (category->parent_category ==
      openwow::game::kAchievementStatisticsRootCategoryId) {
    results.PushNumber(-1.0);
  } else {
    results.PushNumber(static_cast<std::int32_t>(category->parent_category));
  }
  return results.PushNumber(0.0).ResultCount();
}

int LuaGetAchievementCategory(lua_State* state) {
  const openwow::ui::lua::LuaCall arguments(state);
  const auto achievement_id = TruncateLuaNumberToSseI32(
      arguments.RequireNumber(
          1, "Usage: GetAchievementCategory(achievementID)"));
  if (achievement_id < 0) {
    return 0;
  }

  const auto* dbc = GetDbcLoader(state);
  const auto* achievement =
      dbc != nullptr
          ? dbc->achievement().LookupEntry(
                static_cast<std::uint32_t>(achievement_id))
          : nullptr;
  if (achievement == nullptr || achievement->category == 0 ||
      (achievement->flags & kAchievementHiddenFlag) != 0) {
    return 0;
  }

  return openwow::ui::lua::LuaCall(state)
      .PushNumber(achievement->category)
      .ResultCount();
}

}
