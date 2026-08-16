#pragma once

#include "openwow/ui/runtime/lua/lua_composition.h"

#include <optional>
#include <string>
#include <string_view>

struct lua_State;

namespace openwow::ui::game::detail {

int LuaHasPetSpells(lua_State* L);

int LuaGetShapeshiftFormCooldown(lua_State* L);
int LuaToggleSpellAutocast(lua_State* L);

int LuaFindSpellBookSlotByID(lua_State* L);
int LuaGetKnownSlotFromHighestRankSlot(lua_State* L);

[[nodiscard]] openwow::ui::lua::NativeBindingCatalog SpellBookConstantCatalog();

}

namespace openwow::game::spells::spellbook::adapters::lua {

[[nodiscard]] std::optional<std::string> ResolveCastSequenceToken(
    lua_State* state, std::string_view body);

}
