#include "openwow/game/spells/glyphs/adapters/lua/glyph_lua_bindings.h"
#include "openwow/ui/runtime/lua/lua_composition.h"
#include "openwow/game/spells/glyphs/adapters/lua/glyph_lua_api.h"

#include "openwow/ui/lua_binding_registry.h"

extern "C" {
#include <lua.hpp>
}

namespace openwow::ui::game {

using namespace detail;

namespace {

constexpr openwow::ui::LuaGlobalBinding kGlyphLuaBindings[] = {
    {"GetNumGlyphSockets", LuaGetNumGlyphSockets},
    {"GetGlyphSocketInfo", LuaGetGlyphSocketInfo},
    {"GlyphMatchesSocket", LuaGlyphMatchesSocket},
    {"PlaceGlyphInSocket", LuaPlaceGlyphInSocket},
    {"RemoveGlyphFromSocket", LuaRemoveGlyphFromSocket},
    {"GetGlyphLink", LuaGetGlyphLink},
};

}

openwow::ui::lua::NativeBindingCatalog GlyphNativeBindingCatalog() {
  return openwow::ui::lua::NativeFunctionCatalog(
      "game.spells.glyphs", openwow::ui::lua::BindingScope::kWorld, kGlyphLuaBindings);
}

}
