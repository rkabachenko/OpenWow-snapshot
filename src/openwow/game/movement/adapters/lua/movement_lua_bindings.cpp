#include "openwow/game/movement/adapters/lua/movement_lua_bindings.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/runtime/lua/lua_composition.h"

extern "C" {
#include <lua.hpp>
}

namespace openwow::ui::game::detail {

int LuaSitStandOrDescendStart(lua_State* L);
int LuaToggleAutoRun(lua_State* L);
int LuaAscendStop(lua_State* L);
int LuaCameraOrSelectOrMoveStart(lua_State* L);
int LuaCameraOrSelectOrMoveStop(lua_State* L);
int LuaDetectWowMouse(lua_State* L);
int LuaDescendStop(lua_State* L);
int LuaIsMouselooking(lua_State* L);
int LuaMouselookStart(lua_State* L);
int LuaMouselookStop(lua_State* L);
int LuaMoveAndSteerStart(lua_State* L);
int LuaMoveAndSteerStop(lua_State* L);
int LuaMoveBackwardStart(lua_State* L);
int LuaMoveBackwardStop(lua_State* L);
int LuaMoveForwardStart(lua_State* L);
int LuaMoveForwardStop(lua_State* L);
int LuaPitchDownStart(lua_State* L);
int LuaPitchDownStop(lua_State* L);
int LuaPitchUpStart(lua_State* L);
int LuaPitchUpStop(lua_State* L);
int LuaStrafeLeftStart(lua_State* L);
int LuaStrafeLeftStop(lua_State* L);
int LuaStrafeRightStart(lua_State* L);
int LuaStrafeRightStop(lua_State* L);
int LuaToggleRun(lua_State* L);
int LuaToggleSheath(lua_State* L);
int LuaTurnLeftStart(lua_State* L);
int LuaTurnLeftStop(lua_State* L);
int LuaSetMouselookOverrideBinding(lua_State* L);
int LuaTurnOrActionStart(lua_State* L);
int LuaTurnOrActionStop(lua_State* L);
int LuaTurnRightStart(lua_State* L);
int LuaTurnRightStop(lua_State* L);
int LuaJumpOrAscendStart(lua_State* L);

}

namespace openwow::ui::game {

using namespace detail;

namespace {

constexpr openwow::ui::LuaGlobalBinding kMovementLuaBindings[] = {
    {"SitStandOrDescendStart", LuaSitStandOrDescendStart},
    {"ToggleAutoRun", LuaToggleAutoRun},
    {"AscendStop", LuaAscendStop},
    {"CameraOrSelectOrMoveStart", LuaCameraOrSelectOrMoveStart},
    {"CameraOrSelectOrMoveStop", LuaCameraOrSelectOrMoveStop},
    {"DetectWowMouse", LuaDetectWowMouse},
    {"DescendStop", LuaDescendStop},
    {"IsMouselooking", LuaIsMouselooking},
    {"MouselookStart", LuaMouselookStart},
    {"MouselookStop", LuaMouselookStop},
    {"MoveAndSteerStart", LuaMoveAndSteerStart},
    {"MoveAndSteerStop", LuaMoveAndSteerStop},
    {"MoveBackwardStart", LuaMoveBackwardStart},
    {"MoveBackwardStop", LuaMoveBackwardStop},
    {"MoveForwardStart", LuaMoveForwardStart},
    {"MoveForwardStop", LuaMoveForwardStop},
    {"PitchDownStart", LuaPitchDownStart},
    {"PitchDownStop", LuaPitchDownStop},
    {"PitchUpStart", LuaPitchUpStart},
    {"PitchUpStop", LuaPitchUpStop},
    {"StrafeLeftStart", LuaStrafeLeftStart},
    {"StrafeLeftStop", LuaStrafeLeftStop},
    {"StrafeRightStart", LuaStrafeRightStart},
    {"StrafeRightStop", LuaStrafeRightStop},
    {"ToggleRun", LuaToggleRun},
    {"ToggleSheath", LuaToggleSheath},
    {"TurnLeftStart", LuaTurnLeftStart},
    {"TurnLeftStop", LuaTurnLeftStop},
    {"SetMouselookOverrideBinding", LuaSetMouselookOverrideBinding},
    {"TurnOrActionStart", LuaTurnOrActionStart},
    {"TurnOrActionStop", LuaTurnOrActionStop},
    {"TurnRightStart", LuaTurnRightStart},
    {"TurnRightStop", LuaTurnRightStop},
    {"JumpOrAscendStart", LuaJumpOrAscendStart},
};

}

openwow::ui::lua::NativeBindingCatalog MovementNativeBindingCatalog() {
  return openwow::ui::lua::NativeFunctionCatalog(
      "game.movement", openwow::ui::lua::BindingScope::kWorld, kMovementLuaBindings);
}

}
