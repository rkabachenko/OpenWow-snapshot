#include "openwow/game/actions/adapters/lua/action_lua_bindings.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/runtime/lua/lua_composition.h"

extern "C" {
#include <lua.hpp>
}

namespace openwow::ui::game::detail {

int LuaGetActionInfo(lua_State* L);
int LuaGetActionTexture(lua_State* L);
int LuaGetActionCount(lua_State* L);
int LuaGetActionCooldown(lua_State* L);
int LuaGetActionAutocast(lua_State* L);
int LuaHasAction(lua_State* L);
int LuaIsUsableAction(lua_State* L);
int LuaIsCurrentAction(lua_State* L);
int LuaIsAutoRepeatAction(lua_State* L);
int LuaIsAttackAction(lua_State* L);
int LuaIsConsumableAction(lua_State* L);
int LuaIsStackableAction(lua_State* L);
int LuaGetActionText(lua_State* L);
int LuaUseAction(lua_State* L);
int LuaPickupAction(lua_State* L);
int LuaPlaceAction(lua_State* L);
int LuaGetBonusBarOffset(lua_State* L);
int LuaGetActionBarPage(lua_State* L);
int LuaChangeActionBarPage(lua_State* L);
int LuaGetActionBarToggles(lua_State* L);
int LuaActionHasRange(lua_State* L);
int LuaIsActionInRange(lua_State* L);
int LuaIsEquippedAction(lua_State* L);
int LuaGetMultiCastBarOffset(lua_State* L);
int LuaSetActionBarToggles(lua_State* L);

}

namespace openwow::ui::game {

using namespace detail;

namespace {

constexpr openwow::ui::LuaGlobalBinding kActionLuaBindings[] = {
    {"GetActionInfo", LuaGetActionInfo},
    {"GetActionTexture", LuaGetActionTexture},
    {"GetActionCount", LuaGetActionCount},
    {"GetActionCooldown", LuaGetActionCooldown},
    {"GetActionAutocast", LuaGetActionAutocast},
    {"HasAction", LuaHasAction},
    {"IsUsableAction", LuaIsUsableAction},
    {"IsCurrentAction", LuaIsCurrentAction},
    {"IsAutoRepeatAction", LuaIsAutoRepeatAction},
    {"IsAttackAction", LuaIsAttackAction},
    {"IsConsumableAction", LuaIsConsumableAction},
    {"IsStackableAction", LuaIsStackableAction},
    {"GetActionText", LuaGetActionText},
    {"UseAction", LuaUseAction},
    {"PickupAction", LuaPickupAction},
    {"PlaceAction", LuaPlaceAction},
    {"GetBonusBarOffset", LuaGetBonusBarOffset},
    {"GetActionBarPage", LuaGetActionBarPage},
    {"ChangeActionBarPage", LuaChangeActionBarPage},
    {"GetActionBarToggles", LuaGetActionBarToggles},
    {"ActionHasRange", LuaActionHasRange},
    {"IsActionInRange", LuaIsActionInRange},
    {"IsEquippedAction", LuaIsEquippedAction},
    {"GetMultiCastBarOffset", LuaGetMultiCastBarOffset},
    {"SetActionBarToggles", LuaSetActionBarToggles},
};

}

openwow::ui::lua::NativeBindingCatalog ActionNativeBindingCatalog() {
  return openwow::ui::lua::NativeFunctionCatalog(
      "game.actions", openwow::ui::lua::BindingScope::kWorld, kActionLuaBindings);
}

}
