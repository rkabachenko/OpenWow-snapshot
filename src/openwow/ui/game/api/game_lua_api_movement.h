
#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaAscendStop(lua_State* L);
int LuaJumpOrAscendStart(lua_State* L);
int LuaCameraOrSelectOrMoveStart(lua_State* L);
int LuaCameraOrSelectOrMoveStop(lua_State* L);
int LuaDetectWowMouse(lua_State* L);
int LuaDescendStop(lua_State* L);
int LuaIsMouselooking(lua_State* L);
int LuaMouselookStart(lua_State* L);
int LuaMouselookStop(lua_State* L);
int LuaSitStandOrDescendStart(lua_State* L);
int LuaMoveAndSteerStart(lua_State* L);
int LuaMoveAndSteerStop(lua_State* L);
int LuaMoveBackwardStart(lua_State* L);
int LuaMoveBackwardStop(lua_State* L);
int LuaMoveViewDownStart(lua_State* L);
int LuaMoveViewDownStop(lua_State* L);
int LuaMoveForwardStart(lua_State* L);
int LuaMoveForwardStop(lua_State* L);
int LuaMoveViewInStart(lua_State* L);
int LuaMoveViewInStop(lua_State* L);
int LuaMoveViewLeftStart(lua_State* L);
int LuaMoveViewLeftStop(lua_State* L);
int LuaMoveViewOutStart(lua_State* L);
int LuaMoveViewOutStop(lua_State* L);
int LuaMoveViewRightStart(lua_State* L);
int LuaMoveViewRightStop(lua_State* L);
int LuaMoveViewUpStart(lua_State* L);
int LuaMoveViewUpStop(lua_State* L);
int LuaPitchDownStart(lua_State* L);
int LuaPitchDownStop(lua_State* L);
int LuaPitchUpStart(lua_State* L);
int LuaPitchUpStop(lua_State* L);
int LuaStrafeLeftStart(lua_State* L);
int LuaStrafeLeftStop(lua_State* L);
int LuaStrafeRightStart(lua_State* L);
int LuaStrafeRightStop(lua_State* L);
int LuaToggleRun(lua_State* L);
int LuaToggleAutoRun(lua_State* L);
int LuaToggleSheath(lua_State* L);
int LuaTurnLeftStart(lua_State* L);
int LuaTurnLeftStop(lua_State* L);
int LuaSetMouselookOverrideBinding(lua_State* L);
int LuaTurnOrActionStart(lua_State* L);
int LuaTurnOrActionStop(lua_State* L);
int LuaTurnRightStart(lua_State* L);
int LuaTurnRightStop(lua_State* L);
int LuaFlipCameraYaw(lua_State* L);
int LuaResetView(lua_State* L);
int LuaApi_NextView(lua_State* L);
int LuaApi_VehicleCameraZoomIn(lua_State* L);
int LuaApi_VehicleCameraZoomOut(lua_State* L);

struct WorldMouseDeltaResult {
  bool handled{false};
  bool steers_mover{false};
};

[[nodiscard]] WorldMouseDeltaResult HandleWorldMouseDelta(
    lua_State* L, float dx, float dy);
void CancelWorldMouseInput(lua_State* L);

void HandleWorldApplicationActivation(lua_State* L, bool active);

}
