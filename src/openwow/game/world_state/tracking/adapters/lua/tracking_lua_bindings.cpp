#include "openwow/game/world_state/tracking/adapters/lua/tracking_lua_bindings.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/runtime/lua/lua_composition.h"

extern "C" {
#include <lua.hpp>
}

namespace openwow::ui::game::detail {

int LuaGetNumTrackingTypes(lua_State* L);
int LuaGetTrackingInfo(lua_State* L);
int LuaSetTracking(lua_State* L);
int LuaGetTrackingTexture(lua_State* L);

}

namespace openwow::ui::game {

using namespace detail;

namespace {

constexpr openwow::ui::LuaGlobalBinding kTrackingLuaBindings[] = {
    {"GetNumTrackingTypes", LuaGetNumTrackingTypes},
    {"GetTrackingInfo", LuaGetTrackingInfo},
    {"SetTracking", LuaSetTracking},
    {"GetTrackingTexture", LuaGetTrackingTexture},
};

}

openwow::ui::lua::NativeBindingCatalog TrackingNativeBindingCatalog() {
  return openwow::ui::lua::NativeFunctionCatalog(
      "game.world_state.tracking", openwow::ui::lua::BindingScope::kWorld, kTrackingLuaBindings);
}

}
