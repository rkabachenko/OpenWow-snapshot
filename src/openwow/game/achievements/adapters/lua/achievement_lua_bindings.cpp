#include "openwow/game/achievements/adapters/lua/achievement_lua_bindings.h"

#include "openwow/ui/runtime/lua/lua_composition.h"

#include "openwow/game/achievements/adapters/lua/achievement_activity_lua_api.h"
#include "openwow/game/achievements/adapters/lua/achievement_category_lua_api.h"
#include "openwow/game/achievements/adapters/lua/achievement_category_count_lua_api.h"
#include "openwow/game/achievements/adapters/lua/achievement_completion_count_lua_api.h"
#include "openwow/game/achievements/adapters/lua/achievement_comparison_session_lua_api.h"
#include "openwow/game/achievements/adapters/lua/achievement_criteria_lua_api.h"
#include "openwow/game/achievements/adapters/lua/achievement_link_lua_api.h"
#include "openwow/game/achievements/adapters/lua/achievement_detail_lua_api.h"
#include "openwow/game/achievements/adapters/lua/achievement_navigation_lua_api.h"
#include "openwow/game/achievements/adapters/lua/achievement_points_lua_api.h"
#include "openwow/game/achievements/adapters/lua/achievement_reward_lua_api.h"
#include "openwow/game/achievements/adapters/lua/achievement_status_lua_api.h"
#include "openwow/game/achievements/adapters/lua/achievement_statistic_lua_api.h"
#include "openwow/game/achievements/adapters/lua/achievement_tracking_lua_api.h"
#include "openwow/game/quests/adapters/lua/completed_quest_lua_api.h"

#include "openwow/ui/lua_binding_registry.h"

extern "C" {
#include <lua.hpp>
}

namespace openwow::ui::game {

using namespace detail;

namespace {

constexpr openwow::ui::LuaGlobalBinding kAchievementBindings[] = {

    {"GetCategoryList", LuaGetCategoryList},
    {"GetStatisticsCategoryList", LuaGetStatisticsCategoryList},
    {"GetCategoryInfo", LuaGetCategoryInfo},
    {"GetCategoryNumAchievements", LuaGetCategoryNumAchievements},
    {"GetComparisonCategoryNumAchievements", LuaGetComparisonCategoryNumAchievements},
    {"GetAchievementInfo", LuaGetAchievementInfo},
    {"GetAchievementNumRewards", LuaGetAchievementNumRewards},
    {"GetAchievementReward", LuaGetAchievementReward},
    {"GetAchievementNumCriteria", LuaGetAchievementNumCriteria},
    {"GetAchievementCriteriaInfo", LuaGetAchievementCriteriaInfo},
    {"SetAchievementComparisonUnit", LuaSetAchievementComparisonUnit},
    {"ClearAchievementComparisonUnit", LuaClearAchievementComparisonUnit},
    {"GetAchievementComparisonInfo", LuaGetAchievementComparisonInfo},
    {"GetPreviousAchievement", LuaGetPreviousAchievement},
    {"GetNextAchievement", LuaGetNextAchievement},
    {"GetAchievementCategory", LuaGetAchievementCategory},
    {"GetAchievementLink", LuaGetAchievementLink},
    {"GetNumCompletedAchievements", LuaGetNumCompletedAchievements},
    {"GetNumComparisonCompletedAchievements", LuaGetNumComparisonCompletedAchievements},
    {"GetLatestCompletedAchievements", LuaGetLatestCompletedAchievements},
    {"GetLatestUpdatedStats", LuaGetLatestUpdatedStats},
    {"GetLatestCompletedComparisonAchievements", LuaGetLatestCompletedComparisonAchievements},
    {"GetLatestUpdatedComparisonStats", LuaGetLatestUpdatedComparisonStats},
    {"GetTotalAchievementPoints", LuaGetTotalAchievementPoints},
    {"GetAchievementInfoFromCriteria", LuaGetAchievementInfoFromCriteria},
    {"GetStatistic", LuaGetStatistic},
    {"GetComparisonStatistic", LuaGetComparisonStatistic},
    {"GetComparisonAchievementPoints", LuaGetComparisonAchievementPoints},
    {"CanShowAchievementUI", LuaCanShowAchievementUI},
    {"GetTrackedAchievements", LuaGetTrackedAchievements},
    {"AddTrackedAchievement", LuaAddTrackedAchievement},
    {"RemoveTrackedAchievement", LuaRemoveTrackedAchievement},
    {"IsTrackedAchievement", LuaIsTrackedAchievement},
    {"GetNumTrackedAchievements", LuaGetNumTrackedAchievements},
    {"HasCompletedAnyAchievement", LuaHasCompletedAnyAchievement},
    {"QueryQuestsCompleted", LuaQueryQuestsCompleted},
    {"GetQuestsCompleted", LuaGetQuestsCompleted},
};

}

openwow::ui::lua::NativeBindingCatalog AchievementNativeBindingCatalog() {
  return openwow::ui::lua::NativeFunctionCatalog(
      "game.achievements", openwow::ui::lua::BindingScope::kWorld, kAchievementBindings);
}

}
