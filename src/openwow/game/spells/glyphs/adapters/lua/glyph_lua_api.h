#pragma once

#include "openwow/ui/runtime/lua/lua_composition.h"

struct lua_State;

namespace openwow::ui::game::detail {

int LuaGetNumGlyphSockets(lua_State* L);
int LuaGetGlyphSocketInfo(lua_State* L);
int LuaGetGlyphLink(lua_State* L);
int LuaPlaceGlyphInSocket(lua_State* L);
int LuaRemoveGlyphFromSocket(lua_State* L);
int LuaGlyphMatchesSocket(lua_State* L);

[[nodiscard]] openwow::ui::lua::NativeBindingCatalog GlyphConstantCatalog();

}
