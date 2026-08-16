
#include "openwow/ui/game/api/game_lua_api_ui_util.h"
#include "openwow/core/console.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/data/startup_filesystem_state.h"

#include <optional>

namespace openwow::ui::game::detail {

int LuaRestartGx([[maybe_unused]] lua_State* L) {
  openwow::core::ida::Console_ExecuteGraphicsRestart();
  return 0;
}

int LuaGetExistingLocales(lua_State* L) {
  const auto& locale_ring = openwow::data::GetStartupLocaleRing();
  const auto& availability = openwow::data::GetStartupLocaleAvailability();

  int count = 0;
  for (std::size_t i = 0; i < locale_ring.size(); ++i) {
    if (!availability[i]) {
      continue;
    }
    lua_pushstring(L, locale_ring[i]);
    ++count;
  }
  return count;
}

}
