#include "openwow/game/actors/vehicles/adapters/lua/vehicle_lua_bindings.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/runtime/lua/lua_composition.h"

extern "C" {
#include <lua.hpp>
}

namespace openwow::ui::game::detail {

int LuaUnitHasVehicleUI(lua_State* L);
int LuaUnitTargetsVehicleInRaidUI(lua_State* L);
int LuaUnitInVehicle(lua_State* L);
int LuaUnitInVehicleControlSeat(lua_State* L);
int LuaUnitUsingVehicle(lua_State* L);
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
int LuaRetailEjectPassengerFromSeat(lua_State* L);
int LuaIsVehicleAimPowerAdjustable(lua_State* L);
int LuaUnitVehicleSeatInfo(lua_State* L);
int LuaVehicleAimGetNormAngle(lua_State* L);
int LuaVehicleAimRequestNormAngle(lua_State* L);
int LuaVehicleNextSeat(lua_State* L);
int LuaVehiclePrevSeat(lua_State* L);
int LuaRetailCanEjectPassengerFromSeat(lua_State* L);

}

namespace openwow::ui::game {

using namespace detail;

namespace {

constexpr openwow::ui::LuaGlobalBinding kVehicleLuaBindings[] = {
    {"UnitHasVehicleUI", LuaUnitHasVehicleUI},
    {"UnitTargetsVehicleInRaidUI", LuaUnitTargetsVehicleInRaidUI},
    {"UnitInVehicle", LuaUnitInVehicle},
    {"UnitInVehicleControlSeat", LuaUnitInVehicleControlSeat},
    {"UnitUsingVehicle", LuaUnitUsingVehicle},
    {"CanExitVehicle", LuaCanExitVehicle},
    {"VehicleExit", LuaVehicleExit},
    {"IsVehicleAimAngleAdjustable", LuaIsVehicleAimAngleAdjustable},
    {"IsUsingVehicleControls", LuaIsUsingVehicleControls},
    {"VehicleAimUpStart", LuaVehicleAimUpStart},
    {"VehicleAimUpStop", LuaVehicleAimUpStop},
    {"VehicleAimDownStart", LuaVehicleAimDownStart},
    {"VehicleAimDownStop", LuaVehicleAimDownStop},
    {"VehicleAimIncrement", LuaVehicleAimIncrement},
    {"VehicleAimDecrement", LuaVehicleAimDecrement},
    {"VehicleAimGetAngle", LuaVehicleAimGetAngle},
    {"VehicleAimGetNormPower", LuaVehicleAimGetNormPower},
    {"VehicleAimRequestAngle", LuaVehicleAimRequestAngle},
    {"VehicleAimSetNormPower", LuaVehicleAimSetNormPower},
    {"GetVehicleUIIndicator", LuaGetVehicleUIIndicator},
    {"GetVehicleUIIndicatorSeat", LuaGetVehicleUIIndicatorSeat},
    {"UnitVehicleSeatCount", LuaUnitVehicleSeatCount},
    {"UnitControllingVehicle", LuaUnitControllingVehicle},
    {"CanSwitchVehicleSeat", LuaCanSwitchVehicleSeat},
    {"CanSwitchVehicleSeats", LuaCanSwitchVehicleSeats},
    {"EjectPassengerFromSeat", LuaRetailEjectPassengerFromSeat},
    {"IsVehicleAimPowerAdjustable", LuaIsVehicleAimPowerAdjustable},
    {"UnitVehicleSeatInfo", LuaUnitVehicleSeatInfo},
    {"VehicleAimGetNormAngle", LuaVehicleAimGetNormAngle},
    {"VehicleAimRequestNormAngle", LuaVehicleAimRequestNormAngle},
    {"VehicleNextSeat", LuaVehicleNextSeat},
    {"VehiclePrevSeat", LuaVehiclePrevSeat},
    {"CanEjectPassengerFromSeat", LuaRetailCanEjectPassengerFromSeat},
};

}

openwow::ui::lua::NativeBindingCatalog VehicleNativeBindingCatalog() {
  return openwow::ui::lua::NativeFunctionCatalog(
      "game.actors.vehicles", openwow::ui::lua::BindingScope::kWorld, kVehicleLuaBindings);
}

}
