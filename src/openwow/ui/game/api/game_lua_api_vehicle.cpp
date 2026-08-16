
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/api/game_lua_api_movement.h"
#include "openwow/ui/game/api/game_lua_api_vehicle.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/game/c_input_control.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/player_control_runtime.h"
#include "openwow/game/unit_vehicle.h"
#include "openwow/game/vehicle_helpers.h"
#include "openwow/game/vehicle.h"
#include "openwow/game/vehicle_passenger.h"
#include "openwow/game/vehicle_system.h"
#include "openwow/ui/game/script_event_dispatch.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace openwow::ui::game::detail {

namespace {

constexpr std::uint32_t kVehicleAimHasCustomPitchBoundsBit = 0x40;
constexpr std::uint32_t kVehicleAimAngleAdjustableBit = 0x400;
constexpr std::uint32_t kVehicleAimPowerAdjustableBit = 0x800;
constexpr float kVehicleAimDefaultMinPitch = -1.5707964f;
constexpr float kVehicleAimDefaultMaxPitch = 1.5707964f;
constexpr float kVehicleAimZeroRangeEpsilon = 0.000099999997f;

bool CanUseVehicleAction(const ::openwow::game::WorldSession* session,
                         const std::uint32_t required_seat_flag) {
  return session != nullptr &&
         ::openwow::game::CanUseVehicleControlAction(*session, required_seat_flag);
}

bool CanExitCurrentVehicle(const ::openwow::game::WorldSession* session) {
  return CanUseVehicleAction(
      session, ::openwow::game::VehicleControlSeatFlag::kCanExit);
}

bool CanSwitchCurrentVehicleSeat(const ::openwow::game::WorldSession* session) {
  return CanUseVehicleAction(
      session, ::openwow::game::VehicleControlSeatFlag::kCanSwitch);
}

bool HasVehicleAimEffectiveMover(lua_State* L) {
  auto* session = GetWorldSession(L);
  return session && session->objects().GetActivePlayer();
}

struct VehicleAimContext {
  enum class AimSource {
    ActivePlayer,
    EffectiveMoverVehicle,
  };

  ::openwow::game::WorldSession* session = nullptr;
  ::openwow::game::ObjectGuid mover_guid{};
  AimSource aim_source = AimSource::ActivePlayer;
  std::uint32_t seat_flags = 0;
  float min_pitch = kVehicleAimDefaultMinPitch;
  float max_pitch = kVehicleAimDefaultMaxPitch;
};

std::optional<VehicleAimContext> BuildActivePlayerVehicleAimContext(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (!session) {
    return std::nullopt;
  }

  auto* player = session->objects().GetActivePlayer();
  if (!player) {
    return std::nullopt;
  }

  VehicleAimContext context;
  context.session = session;
  context.mover_guid = player->GetGuid();

  auto& vehicle_system = ::openwow::game::VehicleSystem::Get();
  if (!vehicle_system.IsInVehicle()) {
    return context;
  }

  const auto seat = vehicle_system.GetSeat(vehicle_system.GetPlayerSeat());
  if (!seat.has_value()) {
    return context;
  }

  context.seat_flags = static_cast<std::uint32_t>(seat->flags);
  if ((context.seat_flags & kVehicleAimHasCustomPitchBoundsBit) != 0) {
    context.min_pitch = vehicle_system.GetAimPitchMin();
    context.max_pitch = vehicle_system.GetAimPitchMax();
  }

  return context;
}

std::optional<VehicleAimContext> BuildEffectiveVehicleAimContext(lua_State* L) {
  auto context = BuildActivePlayerVehicleAimContext(L);
  if (!context.has_value() ||
      (context->seat_flags & kVehicleAimPowerAdjustableBit) == 0) {
    return context;
  }

  auto& vehicle_system = ::openwow::game::VehicleSystem::Get();
  const auto vehicle_guid = vehicle_system.GetVehicleGuid();
  if (vehicle_guid.IsEmpty()) {
    return context;
  }

  const auto* vehicle = context->session->objects().GetUnit(vehicle_guid);
  if (!vehicle) {
    return context;
  }

  context->aim_source = VehicleAimContext::AimSource::EffectiveMoverVehicle;
  context->mover_guid = vehicle_guid;
  return context;
}

double GetVehicleAimAngle(const VehicleAimContext& context) {
  if (context.aim_source == VehicleAimContext::AimSource::EffectiveMoverVehicle) {
    return ::openwow::game::VehicleSystem::Get().GetAimAngle();
  }

  const auto* mover = context.session->objects().GetUnit(context.mover_guid);
  if (!mover) {
    return 0.0;
  }

  return mover->GetMovementInfo().pitch;
}

float GetVehicleAimAngleAdjustmentBase(const VehicleAimContext& context) {
  if (context.aim_source == VehicleAimContext::AimSource::ActivePlayer) {
    if (const auto* input = ::openwow::game::GetInputControlSingleton();
        input != nullptr && input->IsPitchLockActive()) {
      return input->GetPitchLockValue();
    }
  }

  return static_cast<float>(GetVehicleAimAngle(context));
}

void SetVehicleAimAngle(const VehicleAimContext& context, float angle) {
  const float clamped_angle =
      std::clamp(angle, context.min_pitch, context.max_pitch);
  auto& objects = context.session->objects();

  auto* const mover = objects.GetMutableUnit(context.mover_guid);
  if (mover != nullptr) {
    const std::uint32_t timestamp =
        ::openwow::core::GameClock::GetTickCount32();
    if (auto* const input = ::openwow::game::GetInputControlSingleton();
        input != nullptr) {
      (void)input->RequestVehicleAimPitch(*context.session, timestamp, *mover,
                                          clamped_angle);
    } else if ((mover->GetMovementInfo().flags2 & 0x0010u) != 0u) {
      mover->Movement().SendSetVehiclePitch(
          *context.session, timestamp, clamped_angle);
    } else {
      mover->Movement().SendSetPitch(*context.session, timestamp, clamped_angle);
    }

    auto movement = mover->GetMovementInfo();
    movement.pitch = clamped_angle;
    mover->SetMovementInfo(movement);
  }

  if (context.aim_source == VehicleAimContext::AimSource::EffectiveMoverVehicle) {
    auto& vehicle_system = ::openwow::game::VehicleSystem::Get();
    vehicle_system.SetAimPitchLimits(context.min_pitch, context.max_pitch);
    vehicle_system.SetAimAngle(clamped_angle);
  }

  ::openwow::ui::game::ScriptEventDispatch::Get().FireVehicleAngleUpdate(
      clamped_angle, context.min_pitch, context.max_pitch);
}

struct VehicleUIIndicatorCache {
  struct Seat {
    std::uint32_t id = 0;
    std::uint32_t virtual_seat_index = 0;
    float x_pos = 0.0f;
    float y_pos = 0.0f;
  };

  const ::openwow::data::dbc::DbcLoader* loader = nullptr;
  int cached_indicator_id = -1;
  std::string background_texture;
  std::vector<Seat> seats;
};

VehicleUIIndicatorCache& GetVehicleUIIndicatorCache() {
  static VehicleUIIndicatorCache cache;
  return cache;
}

void RefreshVehicleUIIndicatorCache(
    const ::openwow::data::dbc::DbcLoader& dbc, int indicator_id) {
  auto& cache = GetVehicleUIIndicatorCache();
  cache.loader = &dbc;

  const auto* indicator =
      dbc.vehicle_ui_indicator().LookupEntry(static_cast<std::uint32_t>(indicator_id));
  if (!indicator) {
    cache.cached_indicator_id = 0;
    cache.background_texture.clear();
    cache.seats.clear();
    return;
  }

  cache.cached_indicator_id = static_cast<int>(indicator->id);
  cache.background_texture.assign(indicator->background_texture);
  cache.seats.clear();

  for (const auto& seat_entry : dbc.vehicle_ui_ind_seat().entries()) {

    if (seat_entry.id != 0u &&
        seat_entry.vehicle_ui_indicator_id ==
            static_cast<std::uint32_t>(indicator_id)) {
      cache.seats.push_back({seat_entry.id, seat_entry.virtual_seat_index,
                             seat_entry.x_pos, seat_entry.y_pos});
    }
  }
  std::sort(cache.seats.begin(), cache.seats.end(),
            [](const auto& lhs, const auto& rhs) { return lhs.id < rhs.id; });
}

}

void ResetVehicleAimPowerState() {
  ::openwow::game::InputControl_SetVehicleAimNormalizedPower(0.0f);
}

int LuaCanExitVehicle(lua_State* L) {
  lua_pushwowbool(L, CanExitCurrentVehicle(GetWorldSession(L)));
  return 1;
}

int LuaVehicleExit(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (!session) return 0;

  const auto* unit = static_cast<const ::openwow::game::CGUnit_C*>(nullptr);
  if (lua_isstring(L, 1)) {
    const auto uid = UnitIdArg(L, 1);
    const auto* obj = ResolveUnit(session, uid);
    unit = obj != nullptr && obj->IsUnit()
               ? session->objects().GetUnit(obj->GetGuid())
               : nullptr;
  } else {
    unit = session->objects().GetUnit(
        session->player_control_runtime().ActiveMoverGuid());
  }

  if (unit == nullptr) return 0;

  (void)luaL_optinteger(L, 2, 0);

  if (!CanExitCurrentVehicle(session)) return 0;

  ::openwow::game::UnitVehicle_RequestExit(*session, unit);
  return 0;
}

int LuaIsVehicleAimAngleAdjustable(lua_State* L) {
  const auto context = BuildActivePlayerVehicleAimContext(L);
  const bool adjustable = context.has_value() &&
                          (context->seat_flags & kVehicleAimAngleAdjustableBit) != 0;
  lua_pushwowbool(L, adjustable);
  return 1;
}

int LuaIsUsingVehicleControls(lua_State* L) {
  lua_pushwowbool(L, CanUseVehicleAction(
                         GetWorldSession(L),
                         ::openwow::game::VehicleControlSeatFlag::kHasControls));
  return 1;
}

int LuaVehicleAimUpStart(lua_State* L) {
  return LuaPitchUpStart(L);
}

int LuaVehicleAimUpStop(lua_State* L) {
  return LuaPitchUpStop(L);
}

int LuaVehicleAimDownStart(lua_State* L) {
  return LuaPitchDownStart(L);
}

int LuaVehicleAimDownStop(lua_State* L) {
  return LuaPitchDownStop(L);
}

int LuaVehicleAimIncrement(lua_State* L) {
  const auto context = BuildActivePlayerVehicleAimContext(L);
  if (!context.has_value()) return 0;
  const float delta = static_cast<float>(luaL_optnumber(L, 1, 0.1));
  SetVehicleAimAngle(*context, GetVehicleAimAngleAdjustmentBase(*context) + delta);
  return 0;
}

int LuaVehicleAimDecrement(lua_State* L) {
  const auto context = BuildActivePlayerVehicleAimContext(L);
  if (!context.has_value()) return 0;
  const float delta = static_cast<float>(luaL_optnumber(L, 1, 0.1));
  SetVehicleAimAngle(*context, GetVehicleAimAngleAdjustmentBase(*context) - delta);
  return 0;
}

int LuaVehicleAimGetAngle(lua_State* L) {
  const auto context = BuildEffectiveVehicleAimContext(L);
  lua_pushnumber(L, context.has_value() ? GetVehicleAimAngle(*context) : 0.0);
  return 1;
}

int LuaVehicleAimGetNormPower(lua_State* L) {
  lua_pushnumber(L,
                 ::openwow::game::InputControl_GetVehicleAimNormalizedPower());
  return 1;
}

int LuaVehicleAimRequestAngle(lua_State* L) {
  const auto context = BuildActivePlayerVehicleAimContext(L);
  if (!context.has_value() || !lua_isnumber(L, 1) ||
      (context->seat_flags & kVehicleAimAngleAdjustableBit) == 0) {
    return 0;
  }
  SetVehicleAimAngle(*context, static_cast<float>(lua_tonumber(L, 1)));
  return 0;
}

int LuaVehicleAimSetNormPower(lua_State* L) {
  if (!HasVehicleAimEffectiveMover(L) || !lua_isnumber(L, 1)) return 0;
  ::openwow::game::InputControl_SetVehicleAimNormalizedPower(
      static_cast<float>(lua_tonumber(L, 1)));
  return 0;
}

int LuaGetVehicleUIIndicator(lua_State* L) {
  if (!lua_isnumber(L, 1))
    return luaL_error(L, "Usage: GetVehicleUIIndicator(indicatorID)");

  const auto indicator_id =
      openwow::ui::TruncateLuaNumberToI32(lua_tonumber(L, 1));

  const auto* dbc = GetDbcLoader(L);
  if (!dbc) return 0;

  auto& cache = GetVehicleUIIndicatorCache();
  if (cache.loader != dbc || cache.cached_indicator_id != indicator_id)
    RefreshVehicleUIIndicatorCache(*dbc, indicator_id);

  if (cache.cached_indicator_id <= 0)
    return 0;

  lua_pushlstring(L, cache.background_texture.data(), cache.background_texture.size());
  lua_pushnumber(L, static_cast<lua_Number>(cache.seats.size()));
  return 2;
}

int LuaGetVehicleUIIndicatorSeat(lua_State* L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2))
    return luaL_error(
        L, "Usage: GetVehicleUIIndicatorSeat(indicatorID, indicatorSeatIndex)");

  const auto indicator_id =
      openwow::ui::TruncateLuaNumberToI32(lua_tonumber(L, 1));
  const auto seat_index = static_cast<std::uint32_t>(
                              openwow::ui::TruncateLuaNumberToI32(
                                  lua_tonumber(L, 2))) -
                          1u;

  const auto* dbc = GetDbcLoader(L);
  if (!dbc) return 0;

  auto& cache = GetVehicleUIIndicatorCache();
  if (cache.loader != dbc || cache.cached_indicator_id != indicator_id)
    RefreshVehicleUIIndicatorCache(*dbc, indicator_id);

  if (seat_index >= static_cast<std::uint32_t>(cache.seats.size()))
    return 0;

  const auto& seat = cache.seats[seat_index];
  lua_pushnumber(L, static_cast<lua_Number>(
                        static_cast<int>(seat.virtual_seat_index)));
  lua_pushnumber(L, static_cast<lua_Number>(seat.x_pos));
  lua_pushnumber(L, static_cast<lua_Number>(seat.y_pos));
  return 3;
}

int LuaUnitVehicleSeatCount(lua_State* L) {
  auto* session = GetWorldSession(L);
  const auto uid = UnitIdArg(L, 1);
  const auto* unit = ResolveUnitObject(ResolveUnit(session, uid));

  int seat_count = 0;
  if (unit != nullptr) {
    if (const auto* root_vehicle = ::openwow::game::ResolveRootVehicleUnit(*unit);
        root_vehicle != nullptr) {
      seat_count = ::openwow::game::CountExpandedVehicleSeats(*root_vehicle);
    }
  }

  lua_pushnumber(L, static_cast<lua_Number>(seat_count));
  return 1;
}

int LuaUnitControllingVehicle(lua_State* L) {
  auto* const session = GetWorldSession(L);
  const auto* const unit = ResolveUnitObject(ResolveUnit(session, UnitIdArg(L, 1)));
  const auto* const passenger =
      unit != nullptr ? unit->Vehicle().GetVehiclePassengerComponent() : nullptr;
  const bool controls_vehicle =
      passenger != nullptr &&
      passenger->GetTransitionState() ==
          ::openwow::game::VehiclePassengerTransitionType::kAttached &&
      unit->State().GetCharmedUnitGUID().GetRawValue() == passenger->GetVehicleUnitGuid();
  lua_pushboolean(L, controls_vehicle ? 1 : 0);
  return 1;
}

int LuaCanSwitchVehicleSeat(lua_State* L) {
  const bool can_switch = ::openwow::game::GetInputControlSingleton() != nullptr &&
                           CanSwitchCurrentVehicleSeat(GetWorldSession(L));
  lua_pushboolean(L, can_switch ? 1 : 0);
  return 1;
}

int LuaCanSwitchVehicleSeats(lua_State* L) {
  lua_pushwowbool(L, CanSwitchCurrentVehicleSeat(GetWorldSession(L)));
  return 1;
}

int LuaIsVehicleAimPowerAdjustable(lua_State* L) {
  const auto context = BuildActivePlayerVehicleAimContext(L);
  const bool adjustable = context.has_value() &&
                          (context->seat_flags & kVehicleAimPowerAdjustableBit) != 0;
  lua_pushwowbool(L, adjustable);
  return 1;
}

int LuaVehicleAimGetNormAngle(lua_State* L) {
  const auto context = BuildEffectiveVehicleAimContext(L);
  if (!context.has_value()) {
    lua_pushnumber(L, 0.0);
    return 1;
  }

  const float range = context->max_pitch - context->min_pitch;
  const double norm = (std::fabs(range) <= kVehicleAimZeroRangeEpsilon)
                          ? 0.0
                          : (GetVehicleAimAngle(*context) - context->min_pitch) /
                                static_cast<double>(range);
  lua_pushnumber(L, norm);
  return 1;
}

int LuaVehicleAimRequestNormAngle(lua_State* L) {
  const auto context = BuildEffectiveVehicleAimContext(L);
  if (!context.has_value() || !lua_isnumber(L, 1) ||
      (context->seat_flags & kVehicleAimAngleAdjustableBit) == 0) {
    return 0;
  }

  const float norm = static_cast<float>(lua_tonumber(L, 1));
  SetVehicleAimAngle(
      *context,
      norm * (context->max_pitch - context->min_pitch) + context->min_pitch);
  return 0;
}

int LuaVehicleNextSeat(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (!session || !CanSwitchCurrentVehicleSeat(session)) return 0;

  const auto* const active_mover =
      session->objects().GetUnit(
          session->player_control_runtime().ActiveMoverGuid());
  if (active_mover != nullptr) {
    ::openwow::game::UnitVehicle_RequestNextSeat(*session, active_mover);
  }
  return 0;
}

int LuaVehiclePrevSeat(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (!session || !CanSwitchCurrentVehicleSeat(session)) return 0;

  const auto* const active_mover =
      session->objects().GetUnit(
          session->player_control_runtime().ActiveMoverGuid());
  if (active_mover != nullptr) {
    ::openwow::game::UnitVehicle_RequestPrevSeat(*session, active_mover);
  }
  return 0;
}

int LuaRetailCanEjectPassengerFromSeat(lua_State* L) {
  if (lua_isnumber(L, 1) == 0) {
    return luaL_error(L, "Usage: CanEjectPassengerFromSeat(seatIndex)");
  }

  const auto one_based_seat_index =
      openwow::ui::TruncateLuaNumberToI32(lua_tonumber(L, 1));
  auto* const session = GetWorldSession(L);
  const bool can_eject =
      session != nullptr &&
      ::openwow::game::CanEjectPassengerFromSeat(*session, one_based_seat_index);
  lua_pushboolean(L, can_eject ? 1 : 0);
  return 1;
}

int LuaRetailEjectPassengerFromSeat(lua_State* L) {
  if (lua_isnumber(L, 1) == 0) {
    return luaL_error(L, "Usage: EjectPassengerFromSeat(seatIndex)");
  }

  if (auto* const session = GetWorldSession(L); session != nullptr) {
    (void)::openwow::game::EjectPassengerFromSeat(
        *session, openwow::ui::TruncateLuaNumberToI32(lua_tonumber(L, 1)));
  }
  return 0;
}

}
