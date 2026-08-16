
#include "openwow/game/vehicle_helpers.h"

#include <array>

#include "openwow/runtime/time/game_clock.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/movement_callbacks.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/unit_vehicle.h"
#include "openwow/game/update_fields.h"
#include "openwow/game/vehicle.h"
#include "openwow/game/vehicle_passenger.h"
#include "openwow/game/world_session.h"
#include "openwow/world/camera/world_camera.h"

namespace openwow::game {

namespace {

constexpr std::uint32_t kVehicleSeatFlagTurnWhileMoveAndSteer = 0x00200000u;
constexpr std::uint32_t kVehicleTransitionSitSpellStateFlag = 0x01000000u;
constexpr std::size_t kExpandedVehicleSeatSlots = 8u;

constexpr std::uint32_t kSeatFlagHasEnterAnim = 0x1u;

constexpr std::uint32_t kSeatFlagHasRideAnim = 0x2u;

constexpr std::uint32_t kSeatFlagHasRideUpperAnim = 0x4u;

constexpr std::int32_t kInvalidAnimId = 506;

constexpr std::int32_t kAnimBehaviorNone = -1;
constexpr std::int32_t kAnimBehaviorIdleSheathe = 26;

[[nodiscard]] bool SeatUsesExitTransitionProfile(
    const CGUnit_C& owner,
    const data::dbc::VehicleSeatEntry& seat_entry) {
  const auto owner_bits = owner.GetUInt32(UNIT_FIELD_BYTES_1);
  const auto required_seat_flag = (owner_bits & 0x40u) != 0u ? 0x8u : 0x8000u;
  return (seat_entry.flags & required_seat_flag) != 0u;
}

std::uint32_t ResolveVehicleRecordId(const WorldSession& session, const CGUnit_C& unit) {
  if (const auto* vehicle_entry = unit.Vehicle().GetVehicleEntry(); vehicle_entry != nullptr) {
    return vehicle_entry->id;
  }

  const auto& movement = unit.GetMovementUpdate();
  if (movement.HasUpdateFlag(kUpdateFlagVehicle) && movement.vehicle_id != 0) {
    return movement.vehicle_id;
  }

  const auto& forced_vehicle = session.vehicle().last_force_vehicle();
  if (forced_vehicle.unit_guid == unit.GetGuid() && forced_vehicle.vehicle_rec_id > 0) {
    return static_cast<std::uint32_t>(forced_vehicle.vehicle_rec_id);
  }

  const auto& player_vehicle = session.vehicle().last_vehicle_data();
  if (player_vehicle.vehicle_id != 0 &&
      player_vehicle.player_guid == session.objects().GetActivePlayerGuid() &&
      session.vehicle().GetVehicleGuid() == unit.GetGuid()) {
    return player_vehicle.vehicle_id;
  }

  return 0;
}

[[nodiscard]] bool UnitUsesSitTransitionFallback(const CGUnit_C& unit) {
  return (unit.Animation().GetSelectedStandAnimationFlags() & 0x10u) != 0u ||
         (unit.Animation().GetSelectedStandAnimationId().has_value() &&
          *unit.Animation().GetSelectedStandAnimationId() == 39u) ||
         unit.State().IsSitting();
}

[[nodiscard]] bool UnitUsesKneelTransitionFallback(const CGUnit_C& unit) {

  return (unit.Animation().GetCurrentAnimationId().has_value() &&
          *unit.Animation().GetCurrentAnimationId() == 41u) ||
         (unit.Animation().GetSelectedStandAnimationId().has_value() &&
          *unit.Animation().GetSelectedStandAnimationId() == 41u);
}

[[nodiscard]] bool HasLiveVehicleRecord(const CGUnit_C& unit) {
  return unit.Vehicle().GetVehicleData() != nullptr && unit.Vehicle().GetVehicleEntry() != nullptr;
}

[[nodiscard]] bool ResolveDirectPassengerSeatIndex(const CGUnit_C& vehicle_unit,
                                                   const CGUnit_C& passenger_unit,
                                                   std::uint8_t& out_seat_index) {
  const auto* passenger = passenger_unit.Vehicle().GetVehiclePassengerComponent();
  if (passenger == nullptr) {
    return false;
  }

  const auto vehicle_guid = vehicle_unit.GetGuid().GetRawValue();
  if (passenger->GetAltVehicleGuid() == vehicle_guid) {
    out_seat_index = passenger->GetAltSeatIndex();
    return out_seat_index < kExpandedVehicleSeatSlots;
  }

  if (passenger->GetPrimaryVehicleGuid() == vehicle_guid) {
    out_seat_index = passenger->GetPrimarySeatIndex();
    return out_seat_index < kExpandedVehicleSeatSlots;
  }

  out_seat_index = passenger->GetAltVehicleGuid() != 0 ? passenger->GetAltSeatIndex()
                                                        : passenger->GetPrimarySeatIndex();
  return out_seat_index < kExpandedVehicleSeatSlots;
}

void CollectNestedChildVehicles(const CGUnit_C& vehicle_unit,
                                std::array<const CGUnit_C*, kExpandedVehicleSeatSlots>& out) {
  out.fill(nullptr);
  if (vehicle_unit.Vehicle().GetVehicleData() == nullptr) {
    return;
  }

  vehicle::Vehicle_C_ForEachPassengerUnit(
      vehicle_unit.Vehicle().GetVehicleData(),
      [&](CGUnit_C& passenger_unit) {

        if (!HasLiveVehicleRecord(passenger_unit) || passenger_unit.IsPlayer()) {
          return;
        }

        std::uint8_t seat_index = 0;
        if (!ResolveDirectPassengerSeatIndex(vehicle_unit, passenger_unit, seat_index)) {
          return;
        }

        out[seat_index] = &passenger_unit;
      });
}

[[nodiscard]] bool FindExpandedVehicleSeatRecursive(const CGUnit_C& vehicle_unit,
                                                    int& remaining,
                                                    const CGUnit_C*& out_vehicle_unit,
                                                    std::uint8_t& out_seat_index) {
  const auto* vehicle_entry = vehicle_unit.Vehicle().GetVehicleEntry();
  if (vehicle_entry == nullptr) {
    return false;
  }

  std::array<const CGUnit_C*, kExpandedVehicleSeatSlots> child_vehicles{};
  CollectNestedChildVehicles(vehicle_unit, child_vehicles);

  for (std::size_t seat_index = 0; seat_index < child_vehicles.size(); ++seat_index) {
    if (const auto* child_vehicle = child_vehicles[seat_index]; child_vehicle != nullptr) {
      if (FindExpandedVehicleSeatRecursive(*child_vehicle, remaining,
                                           out_vehicle_unit, out_seat_index)) {
        return true;
      }
      continue;
    }

    if (vehicle_entry->seat_id[seat_index] == 0) {
      continue;
    }

    if (remaining == 0) {
      out_vehicle_unit = &vehicle_unit;
      out_seat_index = static_cast<std::uint8_t>(seat_index);
      return true;
    }

    --remaining;
  }

  return false;
}

}

bool HasVehicleTransitionTargetGuid(const std::uint64_t guid) {
  if (guid == 0) {
    return false;
  }

  const auto guid_hi = static_cast<std::uint32_t>(guid >> 32);
  const auto guid_lo = static_cast<std::uint32_t>(guid & 0xFFFFFFFFu);
  return (guid_hi & 0xF0F00000u) == 0xF0500000u ||
         ((guid_hi & 0xF0000000u) == 0u &&
          (((guid_hi & 0xF07FFFFFu) | guid_lo) != 0u));
}

std::uint32_t ResolveUnitVehicleSeatRecordId(const WorldSession& session, const CGUnit_C& unit) {
  const auto* passenger = unit.Vehicle().GetVehiclePassengerComponent();
  if (passenger == nullptr) {
    return 0;
  }

  if (!unit.Vehicle().HasValidVehicleUnitGuid()) {
    return 0;
  }

  const auto* vehicle_unit = passenger->GetVehicleUnit();
  if (vehicle_unit == nullptr) {
    return 0;
  }

  const auto seat_index =
      passenger->GetAltVehicleGuid() != 0 ? passenger->GetAltSeatIndex()
                                          : passenger->GetPrimarySeatIndex();
  if (seat_index >= 8) {
    return 0;
  }

  return LookupVehicleSeatRecordIdForVehicleSeat(session, *vehicle_unit, seat_index);
}

std::uint32_t LookupVehicleSeatRecordIdForVehicleSeat(
    const WorldSession& session,
    const CGUnit_C& vehicle_unit,
    const std::uint8_t seat_index) {
  if (seat_index >= 8) {
    return 0;
  }

  const auto* dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    return 0;
  }

  const auto* vehicle_entry =
      dbc->vehicle().LookupEntry(ResolveVehicleRecordId(session, vehicle_unit));
  if (vehicle_entry == nullptr) {
    return 0;
  }

  return vehicle_entry->seat_id[seat_index];
}

const openwow::data::dbc::VehicleSeatEntry* LookupVehicleSeatEntryById(
    const WorldSession& session,
    const std::uint32_t seat_record_id) {
  if (seat_record_id == 0) {
    return nullptr;
  }

  const auto* dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    return nullptr;
  }

  return dbc->vehicle_seat().LookupEntry(seat_record_id);
}

const openwow::data::dbc::VehicleSeatEntry* LookupVehicleSeatEntryForVehicleSeat(
    const WorldSession& session,
    const CGUnit_C& vehicle_unit,
    const std::uint8_t seat_index) {
  return LookupVehicleSeatEntryById(
      session, LookupVehicleSeatRecordIdForVehicleSeat(session, vehicle_unit, seat_index));
}

const openwow::data::dbc::VehicleSeatEntry* ResolveUnitVehicleSeatEntry(
    const WorldSession& session,
    const CGUnit_C& unit) {
  return LookupVehicleSeatEntryById(session, ResolveUnitVehicleSeatRecordId(session, unit));
}

bool VehicleTransitionProfileEnabled(
    const CGUnit_C& unit,
    const openwow::data::dbc::VehicleSeatEntry* seat_entry,
    const bool allow_seat_profile) {
  if (unit.State().HasForcedVehicleTransition()) {
    return true;
  }

  return allow_seat_profile && seat_entry != nullptr &&
         (seat_entry->transition_flags & 0x01000000u) != 0u;
}

const openwow::data::dbc::VehicleSeatEntry* LookupCachedUnitVehicleSeatEntry(
    const WorldSession& session,
    const ObjectGuid guid) {
  const auto raw_guid = guid.GetRawValue();
  if (raw_guid == 0) {
    return nullptr;
  }

  if (const auto cached = session.party_stats().GetCachedMember(raw_guid);
      cached.has_value() && cached->stats.vehicle_seat != 0) {
    return LookupVehicleSeatEntryById(session, cached->stats.vehicle_seat);
  }

  return LookupVehicleSeatEntryById(
      session, session.battleground().GetArenaOpponentVehicleSeat(raw_guid));
}

const CGUnit_C* ResolveVehicleControlBoundUnit(const WorldSession& session) {
  const auto* const camera = session.world_camera();
  if (camera == nullptr || camera->bound_object() == 0u) {
    return nullptr;
  }

  return session.objects().GetUnit(ObjectGuid{camera->bound_object()});
}

bool CanUseVehicleControlAction(const WorldSession& session,
                                const std::uint32_t required_seat_flag) {
  const auto* bound_unit = ResolveVehicleControlBoundUnit(session);
  if (bound_unit == nullptr) {
    return false;
  }

  const auto* active_player = session.objects().GetActivePlayer();
  if (active_player == nullptr) {
    return false;
  }

  const auto* const passenger =
      active_player->Vehicle().GetVehiclePassengerComponent();
  if (passenger == nullptr ||
      passenger->GetTransitionState() !=
          VehiclePassengerTransitionType::kAttached) {
    return false;
  }

  const ObjectGuid vehicle_guid{passenger->GetVehicleUnitGuid()};
  if (vehicle_guid.IsEmpty()) {
    return false;
  }

  const auto bound_guid = bound_unit->GetGuid();
  if (bound_guid != vehicle_guid && bound_guid != active_player->GetGuid()) {
    return false;
  }

  const auto* const vehicle_unit = session.objects().GetUnit(vehicle_guid);
  if (vehicle_unit == nullptr || !HasLiveVehicleRecord(*vehicle_unit)) {
    return false;
  }

  const auto* seat_entry = ResolveUnitVehicleSeatEntry(session, *active_player);
  if (seat_entry == nullptr) {
    return false;
  }

  return (seat_entry->flags & required_seat_flag) != 0u;
}

const CGUnit_C* ResolveRootVehicleUnit(const CGUnit_C& unit) {
  return static_cast<const CGUnit_C*>(
      UnitVehicle_FindRootVehicle(const_cast<CGUnit_C*>(&unit), nullptr));
}

int CountExpandedVehicleSeats(const CGUnit_C& vehicle_unit) {
  const auto* vehicle_entry = vehicle_unit.Vehicle().GetVehicleEntry();
  if (vehicle_entry == nullptr) {
    return 0;
  }

  int count = 0;
  for (const auto seat_id : vehicle_entry->seat_id) {
    if (seat_id != 0) {
      ++count;
    }
  }

  std::array<const CGUnit_C*, kExpandedVehicleSeatSlots> child_vehicles{};
  CollectNestedChildVehicles(vehicle_unit, child_vehicles);
  for (const auto* child_vehicle : child_vehicles) {
    if (child_vehicle != nullptr) {
      count += CountExpandedVehicleSeats(*child_vehicle) - 1;
    }
  }

  return count;
}

bool FindExpandedVehicleSeat(const CGUnit_C& vehicle_unit,
                             const int seat_ordinal,
                             const CGUnit_C*& out_vehicle_unit,
                             std::uint8_t& out_seat_index) {
  if (seat_ordinal < 0) {
    return false;
  }

  int remaining = seat_ordinal;
  out_vehicle_unit = nullptr;
  out_seat_index = 0;
  return FindExpandedVehicleSeatRecursive(
      vehicle_unit, remaining, out_vehicle_unit, out_seat_index);
}

const CGUnit_C* FindDirectVehiclePassengerBySeatIndex(const CGUnit_C& vehicle_unit,
                                                      const std::uint8_t seat_index) {
  if (vehicle_unit.Vehicle().GetVehicleData() == nullptr) {
    return nullptr;
  }

  const CGUnit_C* result = nullptr;
  vehicle::Vehicle_C_ForEachPassengerUnit(
      vehicle_unit.Vehicle().GetVehicleData(),
      [&](CGUnit_C& passenger_unit) {
        if (result != nullptr) {
          return;
        }

        std::uint8_t passenger_seat_index = 0;
        if (!ResolveDirectPassengerSeatIndex(vehicle_unit, passenger_unit,
                                             passenger_seat_index)) {
          return;
        }

        if (passenger_seat_index == seat_index) {
          result = &passenger_unit;
        }
      });
  return result;
}

std::int32_t ResolveVehicleSeatTransitionAnimationId(
    const VehiclePassengerC& passenger,
    const data::dbc::VehicleSeatEntry& seat_entry) {
  const auto state = passenger.GetTransitionState();
  const auto flags = passenger.GetFlags();

  switch (state) {
    case VehiclePassengerTransitionType::kEnterWithPos:
    case VehiclePassengerTransitionType::kTransferWithPos:
      if ((seat_entry.flags & kSeatFlagHasEnterAnim) == 0)
        return kInvalidAnimId;
      if ((flags & VehiclePassengerFlag::kPositionDirty) != 0 ||
          seat_entry.enter_anim_start == kAnimBehaviorNone) {
        if (seat_entry.enter_anim_loop == kAnimBehaviorNone)
          return kInvalidAnimId;
        return seat_entry.enter_anim_loop;
      }
      return seat_entry.enter_anim_start;

    case VehiclePassengerTransitionType::kAttached:
      if ((seat_entry.flags & kSeatFlagHasRideAnim) == 0)
        return kInvalidAnimId;
      if ((flags & VehiclePassengerFlag::kPositionDirty) != 0 ||
          seat_entry.ride_anim_start == kAnimBehaviorNone) {
        if (seat_entry.ride_anim_loop == kAnimBehaviorNone)
          return kInvalidAnimId;
        return seat_entry.ride_anim_loop;
      }
      return seat_entry.ride_anim_start;

    case VehiclePassengerTransitionType::kEnterCaptured:
    case VehiclePassengerTransitionType::kEject:
    {
      const auto* owner = passenger.GetOwner();
      if (owner == nullptr ||
          !SeatUsesExitTransitionProfile(*owner, seat_entry))
        return kInvalidAnimId;
      if ((flags & VehiclePassengerFlag::kPositionDirty) != 0 ||
          seat_entry.exit_anim_start == kAnimBehaviorNone) {
        if (seat_entry.exit_anim_loop == kAnimBehaviorNone)
          return kInvalidAnimId;
        return seat_entry.exit_anim_loop;
      }
      return seat_entry.exit_anim_start;
    }

    default:
      return kInvalidAnimId;
  }
}

void Vehicle_DispatchSeatAnimation(VehiclePassengerC& passenger,
                                   const std::int32_t behavior_id) {
  const auto* seat_entry = passenger.GetSeatEntry();

  const auto base_anim =
      seat_entry != nullptr
          ? ResolveVehicleSeatTransitionAnimationId(passenger, *seat_entry)
          : kInvalidAnimId;

  if (base_anim == kInvalidAnimId || seat_entry == nullptr ||
      passenger.GetTransitionState() != VehiclePassengerTransitionType::kAttached ||
      (seat_entry->flags & kSeatFlagHasRideUpperAnim) == 0) {
    passenger.SetFlag(VehiclePassengerFlag::kPositionDirty |
                      VehiclePassengerFlag::kAnimSynced);
    return;
  }

  const auto current_flags = passenger.GetFlags();
  std::int32_t upper_anim;

  if ((current_flags & VehiclePassengerFlag::kAnimSynced) != 0 ||
      seat_entry->ride_upper_anim_start == kAnimBehaviorNone) {

    if (seat_entry->ride_upper_anim_loop == kAnimBehaviorNone) {
      passenger.SetFlag(VehiclePassengerFlag::kPositionDirty |
                        VehiclePassengerFlag::kAnimSynced);
      return;
    }
    upper_anim = seat_entry->ride_upper_anim_loop;
  } else {
    upper_anim = seat_entry->ride_upper_anim_start;
  }

  if (upper_anim == kInvalidAnimId) {
    passenger.SetFlag(VehiclePassengerFlag::kPositionDirty |
                      VehiclePassengerFlag::kAnimSynced);
    return;
  }

  if (behavior_id == kAnimBehaviorNone ||
      behavior_id == kAnimBehaviorIdleSheathe) {
    passenger.SetFlag(VehiclePassengerFlag::kPositionDirty);
  } else {
    passenger.SetFlag(VehiclePassengerFlag::kAnimSynced);
  }
}

int Vehicle_ProcessPendingSeatTimer(WorldSession& session,
                                    VehiclePassengerC* passenger) {
  if (!passenger->HasFlag(VehiclePassengerFlag::kPendingSeatChange))
    return 0;

  const auto now = static_cast<std::int32_t>(core::GameClock::GetTickCount32());
  const auto deadline =
      static_cast<std::int32_t>(passenger->GetPendingSeatChangeDeadlineMs());
  if (now - deadline < 0)
    return 0;

  passenger->ProcessPendingSeatChange(session);
  return 1;
}

void Vehicle_PlayTransitionEmote(
    CGUnit_C& unit,
    const openwow::data::dbc::VehicleSeatEntry* seat_dbc,
    const int animation_id) {
  if (seat_dbc != nullptr &&
      (seat_dbc->flags & kVehicleSeatFlagTurnWhileMoveAndSteer) != 0u &&
      animation_id != -1) {
    unit.Animation().PlayEmoteAnimation(animation_id, true);
    return;
  }

  if (UnitUsesSitTransitionFallback(unit)) {
    unit.Animation().PlayEmoteAnimation(40, false);
    unit.State().SetSpellStateFlags(unit.State().GetSpellStateFlags() |
                            kVehicleTransitionSitSpellStateFlag);
    return;
  }

  if (UnitUsesKneelTransitionFallback(unit)) {
    unit.Animation().PlayEmoteAnimation(41, false);
    return;
  }

  if (animation_id != -1) {
    unit.Animation().PlayEmoteAnimation(animation_id, true);
  }
}

void* Vehicle_ForwardDismount(void* unit, int param) {
  auto* cg_unit = static_cast<CGUnit_C*>(unit);
  if (cg_unit == nullptr) {
    return nullptr;
  }

  cg_unit->Movement().Data().QueueDeferredMoveEvent(
      static_cast<std::uint32_t>(param), 0x34u, true, 0u, 0.0f, false,
      static_cast<std::uint32_t>(param));
  return &cg_unit->Movement().Data();
}

void* Vehicle_ForwardSeatSwitch(void* unit, const int time,
                                const int guid_lo, const int guid_hi,
                                const std::uint8_t seat) {
  auto* cg_unit = static_cast<CGUnit_C*>(unit);
  if (cg_unit == nullptr) {
    return nullptr;
  }

  cg_unit->Movement().Data().QueueVehicleSeatSwitch(
      static_cast<std::uint32_t>(time), static_cast<std::uint32_t>(guid_lo),
      static_cast<std::uint32_t>(guid_hi), seat);
  return &cg_unit->Movement().Data();
}

int Vehicle_ValidateCameraChain(
    void* unit, const openwow::world::WorldCamera& camera) {

  auto* unit_obj = static_cast<CGUnit_C*>(unit);
  if (unit_obj == nullptr) {
    return 0;
  }

  return camera.bound_object() == unit_obj->GetGuid().GetRawValue()
             ? 1
             : 0;
}

void Vehicle_TransformCameraTargetFacingToLocal(
    const ObjectManager& objects, const CGUnit_C& unit, float* facing) {
  if (facing == nullptr) {
    return;
  }

  const auto* const passenger = unit.Vehicle().GetVehiclePassengerComponent();
  if (passenger == nullptr) {
    return;
  }

  vehicle::Vehicle_C_AccumulateObjectFacing(facing,
                                            passenger->GetVehicleObject());
  (void)objects;
}

void Vehicle_RecordCameraFacingMouseYawOverride(
    const ObjectManager& objects, CGUnit_C& unit,
    const openwow::world::WorldCamera& camera,
    const float world_facing) {
  auto* const passenger = unit.Vehicle().GetVehiclePassengerComponent();
  if (passenger == nullptr ||
      camera.bound_object() != unit.GetGuid().GetRawValue()) {
    return;
  }

  (void)objects;
  passenger->SetMouseYawOverride(world_facing);
}

}
