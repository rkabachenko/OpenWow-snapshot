
#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaCanExitVehicle(lua_State* L);
int LuaVehicleExit(lua_State* L);
int LuaIsVehicleAimAngleAdjustable(lua_State* L);
int LuaIsUsingVehicleControls(lua_State* L);
int LuaVehicleAimUpStart(lua_State* L);
int LuaVehicleAimUpStop(lua_State* L);
int LuaVehicleAimDownStart(lua_State* L);
int LuaVehicleAimDownStop(lua_State* L);
int LuaVehicleAimIncrement(lua_State* L);
int LuaVehicleAimDecrement(lua_State* L);
int LuaVehicleAimGetAngle(lua_State* L);
int LuaVehicleAimGetNormPower(lua_State* L);
int LuaVehicleAimRequestAngle(lua_State* L);
int LuaVehicleAimSetNormPower(lua_State* L);
int LuaGetVehicleUIIndicator(lua_State* L);
int LuaGetVehicleUIIndicatorSeat(lua_State* L);
int LuaUnitVehicleSeatCount(lua_State* L);
int LuaUnitControllingVehicle(lua_State* L);
int LuaCanSwitchVehicleSeat(lua_State* L);

int LuaCanSwitchVehicleSeats(lua_State* L);
int LuaIsVehicleAimPowerAdjustable(lua_State* L);
int LuaVehicleAimGetNormAngle(lua_State* L);

int LuaVehicleAimRequestNormAngle(lua_State* L);

int LuaVehicleNextSeat(lua_State* L);
int LuaVehiclePrevSeat(lua_State* L);

void ResetVehicleAimPowerState();

}
