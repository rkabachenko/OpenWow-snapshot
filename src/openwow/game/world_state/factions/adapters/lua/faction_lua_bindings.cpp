#include "openwow/game/world_state/factions/adapters/lua/faction_lua_bindings.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/runtime/lua/lua_composition.h"

extern "C" {
#include <lua.hpp>
}

namespace openwow::ui::game::detail {

int LuaGetNumFactions(lua_State* L);
int LuaGetFactionInfo(lua_State* L);
int LuaGetFactionInfoByID(lua_State* L);
int LuaGetWatchedFactionInfo(lua_State* L);
int LuaSetWatchedFactionIndex(lua_State* L);
int LuaFactionToggleAtWar(lua_State* L);
int LuaCollapseFactionHeader(lua_State* L);
int LuaCollapseAllFactionHeaders(lua_State* L);
int LuaSetFactionInactive(lua_State* L);
int LuaSetFactionActive(lua_State* L);
int LuaIsFactionInactive(lua_State* L);
int LuaExpandFactionHeader(lua_State* L);
int LuaExpandAllFactionHeaders(lua_State* L);
int LuaSetSelectedFaction(lua_State* L);
int LuaGetSelectedFaction(lua_State* L);

}

namespace openwow::ui::game {

using namespace detail;

namespace {

constexpr openwow::ui::LuaGlobalBinding kFactionLuaBindings[] = {
    {"GetNumFactions", LuaGetNumFactions},
    {"GetFactionInfo", LuaGetFactionInfo},
    {"GetFactionInfoByID", LuaGetFactionInfoByID},
    {"GetWatchedFactionInfo", LuaGetWatchedFactionInfo},
    {"SetWatchedFactionIndex", LuaSetWatchedFactionIndex},
    {"FactionToggleAtWar", LuaFactionToggleAtWar},
    {"CollapseFactionHeader", LuaCollapseFactionHeader},
    {"CollapseAllFactionHeaders", LuaCollapseAllFactionHeaders},
    {"SetFactionInactive", LuaSetFactionInactive},
    {"SetFactionActive", LuaSetFactionActive},
    {"IsFactionInactive", LuaIsFactionInactive},
    {"ExpandFactionHeader", LuaExpandFactionHeader},
    {"ExpandAllFactionHeaders", LuaExpandAllFactionHeaders},
    {"SetSelectedFaction", LuaSetSelectedFaction},
    {"GetSelectedFaction", LuaGetSelectedFaction},
};

}

openwow::ui::lua::NativeBindingCatalog FactionNativeBindingCatalog() {
  return openwow::ui::lua::NativeFunctionCatalog(
      "game.world_state.factions", openwow::ui::lua::BindingScope::kWorld, kFactionLuaBindings);
}

}
