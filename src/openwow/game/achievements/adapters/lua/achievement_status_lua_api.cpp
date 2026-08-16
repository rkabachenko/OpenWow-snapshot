#include "openwow/game/achievements/adapters/lua/achievement_status_lua_api.h"

#include "openwow/game/achievements/application/achievement_state.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/runtime/lua/lua_call.h"

namespace openwow::ui::game::detail {

int LuaCanShowAchievementUI(lua_State* state) {
  const auto* session = GetWorldSession(state);
  const bool ready =
      session != nullptr &&
      session->achievements().ui_readiness() ==
          openwow::game::AchievementUiReadiness::kReady;
  return openwow::ui::lua::LuaCall(state).PushBoolean(ready).ResultCount();
}

int LuaHasCompletedAnyAchievement(lua_State* state) {
  const auto* session = GetWorldSession(state);
  const bool has_completed_achievement =
      session != nullptr && !session->achievements().completed().empty();
  return openwow::ui::lua::LuaCall(state)
      .PushBoolean(has_completed_achievement)
      .ResultCount();
}

}
