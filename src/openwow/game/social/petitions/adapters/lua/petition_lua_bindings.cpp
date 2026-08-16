#include "openwow/game/social/petitions/adapters/lua/petition_lua_bindings.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/runtime/lua/lua_composition.h"

extern "C" {
#include <lua.hpp>
}

namespace openwow::ui::game::detail {

int LuaCanSignPetition(lua_State* L);
int LuaClosePetition(lua_State* L);
int LuaClosePetitionVendor(lua_State* L);
int LuaClickPetitionButton(lua_State* L);
int LuaGetNumPetitionItems(lua_State* L);
int LuaGetNumPetitionNames(lua_State* L);
int LuaGetPetitionInfo(lua_State* L);
int LuaGetPetitionNameInfo(lua_State* L);
int LuaHasFilledPetition(lua_State* L);
int LuaRenamePetition(lua_State* L);
int LuaSignPetition(lua_State* L);
int LuaTurnInPetition(lua_State* L);
int LuaTurnInGuildCharter(lua_State* L);
int LuaGetPetitionItemInfo(lua_State* L);

}

namespace openwow::ui::game {

using namespace detail;

namespace {

constexpr openwow::ui::LuaGlobalBinding kPetitionLuaBindings[] = {
    {"CanSignPetition", LuaCanSignPetition},
    {"ClosePetition", LuaClosePetition},
    {"ClosePetitionVendor", LuaClosePetitionVendor},
    {"ClickPetitionButton", LuaClickPetitionButton},
    {"GetNumPetitionItems", LuaGetNumPetitionItems},
    {"GetNumPetitionNames", LuaGetNumPetitionNames},
    {"GetPetitionInfo", LuaGetPetitionInfo},
    {"GetPetitionNameInfo", LuaGetPetitionNameInfo},
    {"HasFilledPetition", LuaHasFilledPetition},
    {"RenamePetition", LuaRenamePetition},
    {"SignPetition", LuaSignPetition},
    {"TurnInPetition", LuaTurnInPetition},
    {"TurnInGuildCharter", LuaTurnInGuildCharter},
    {"GetPetitionItemInfo", LuaGetPetitionItemInfo},
};

}

openwow::ui::lua::NativeBindingCatalog PetitionNativeBindingCatalog() {
  return openwow::ui::lua::NativeFunctionCatalog(
      "game.social.petitions", openwow::ui::lua::BindingScope::kWorld, kPetitionLuaBindings);
}

}
