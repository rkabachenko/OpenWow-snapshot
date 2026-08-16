#include "openwow/ui/game/api/game_lua_api_globals.h"

#include "openwow/ui/frame_script_standard_globals.h"
#include "openwow/ui/game/secure_execution.h"

namespace openwow::ui::game {

void RegisterStandardWowGlobals(lua_State* state) {
  openwow::ui::RegisterFrameScriptStandardGlobals(
      state, openwow::ui::FrameScriptGlobalProfile::Game);
  SecureExecution::RegisterLuaBindings(state);
}

}
