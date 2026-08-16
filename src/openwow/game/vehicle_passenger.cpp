
#include "openwow/game/vehicle_passenger.h"
#include "openwow/game/player_control_runtime.h"

#include "openwow/core/cobject_heap.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/core/storm_intrusive_list.h"
#include "openwow/data/formats/dbc/dbc_entries_world.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/unit_sound_dispatch.h"
#include "openwow/game/update_fields.h"
#include "openwow/game/unit_vehicle.h"
#include "openwow/game/vehicle_helpers.h"
#include "openwow/game/vehicle.h"
#include "openwow/game/vehicle_runtime_layout.h"
#include "openwow/game/world_session.h"
#include "openwow/net/serialization/cdatastore_ops.h"
#include "openwow/net/serialization/cdatastore_vtable.h"
#include "openwow/render/m2/m2_system.h"
#include "openwow/render/models/animation/m2_attachment_transform.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/script_event_dispatch.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <optional>
#include <unordered_set>

namespace openwow::game {

namespace {

constexpr float kVehicleTransitionEpsilon = 0.000099999997f;
constexpr float kVehicleTransitionMaxDurationSeconds = 10.0f;
constexpr float kVehicleTransitionDefaultDurationSeconds = 1.5f;
constexpr float kVehiclePi = 3.1415927f;
constexpr float kVehicleTau = 6.2831855f;
constexpr std::uint32_t kVehicleInputControlGraceMs = 2000u;
constexpr std::uint32_t kSeatFlagEnterEaseIn = 0x20u;
constexpr std::uint32_t kSeatFlagEnterEaseOut = 0x40u;
constexpr std::uint32_t kSeatFlagExitEaseIn = 0x80u;
constexpr std::uint32_t kSeatFlagExitEaseOut = 0x100u;

constexpr std::array<std::int32_t, 22> kVehicleSeatM2AttachmentLookup = {
    20, 34, 19, 21, 22, 17, 23, 24, 25, 15, 16,
    37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 0,
};

constexpr std::size_t kVehicleSeatSlotCount = 8u;

using NativeStormLinkWords = openwow::core::StormIntrusiveLinkWords<std::uintptr_t>;
using NativeStormListRootWords =
    openwow::core::StormIntrusiveListRootWords<std::uintptr_t>;

template <typename T>
[[nodiscard]] T LoadVehicleOpaqueField(const void* const base,
                                       const std::size_t offset) {
  T value{};
  if (base == nullptr) {
    return value;
  }

  std::memcpy(&value, static_cast<const std::byte*>(base) + offset, sizeof(T));
  return value;
}

template <typename Fn>
void ForEachSessionVehiclePassengerUnit(const WorldSession& session,
                                        const void* const vehicle_data,
                                        Fn&& fn) {
  if (vehicle_data == nullptr) {
    return;
  }

  const auto node_link_offset = LoadVehicleOpaqueField<std::int32_t>(
      vehicle_data,
      vehicle_runtime_layout::kVehiclePassengerListOffset +
          offsetof(NativeStormListRootWords, node_link_offset));
  if (node_link_offset < 0) {
    return;
  }

  auto node = LoadVehicleOpaqueField<std::uintptr_t>(
      vehicle_data,
      vehicle_runtime_layout::kVehiclePassengerListOffset +
          offsetof(NativeStormListRootWords, head_node));
  while (node != 0 &&
         (node & openwow::core::kStormIntrusiveSentinelBit<std::uintptr_t>) == 0) {
    const auto guid_holder = LoadVehicleOpaqueField<std::uintptr_t>(
        reinterpret_cast<const void*>(node),
        vehicle_runtime_layout::kVehiclePassengerGuidHolderOffset);
    if (guid_holder != 0) {
      const auto guid_raw = LoadVehicleOpaqueField<std::uint64_t>(
          reinterpret_cast<const void*>(guid_holder), 0);
      if (guid_raw != 0) {
        if (const auto* const unit =
                session.objects().GetUnit(ObjectGuid(guid_raw));
            unit != nullptr) {
          fn(*const_cast<CGUnit_C*>(unit));
        }
      }
    }

    node = LoadVehicleOpaqueField<std::uintptr_t>(
        reinterpret_cast<const void*>(node),
        static_cast<std::size_t>(node_link_offset) +
            offsetof(NativeStormLinkWords, next_node));
  }
}

[[nodiscard]] bool HasLiveSessionVehicleRecord(const CGUnit_C& unit) {
  return unit.Vehicle().GetVehicleData() != nullptr && unit.Vehicle().GetVehicleEntry() != nullptr;
}

[[nodiscard]] bool ResolveSessionDirectPassengerSeatIndex(
    const CGUnit_C& vehicle_unit,
    const CGUnit_C& passenger_unit,
    std::uint8_t& out_seat_index) {
  const auto* const passenger = passenger_unit.Vehicle().GetVehiclePassengerComponent();
  if (passenger == nullptr) {
    return false;
  }

  const auto vehicle_guid = vehicle_unit.GetGuid().GetRawValue();
  if (passenger->GetAltVehicleGuid() == vehicle_guid) {
    out_seat_index = passenger->GetAltSeatIndex();
    return out_seat_index < kVehicleSeatSlotCount;
  }

  if (passenger->GetPrimaryVehicleGuid() == vehicle_guid) {
    out_seat_index = passenger->GetPrimarySeatIndex();
    return out_seat_index < kVehicleSeatSlotCount;
  }

  out_seat_index = passenger->GetAltVehicleGuid() != 0 ? passenger->GetAltSeatIndex()
                                                       : passenger->GetPrimarySeatIndex();
  return out_seat_index < kVehicleSeatSlotCount;
}

void CollectSessionNestedChildVehicles(
    const WorldSession& session,
    const CGUnit_C& vehicle_unit,
    std::array<const CGUnit_C*, kVehicleSeatSlotCount>& out) {
  ForEachSessionVehiclePassengerUnit(
      session, vehicle_unit.Vehicle().GetVehicleData(),
      [&](CGUnit_C& passenger_unit) {
        if (!HasLiveSessionVehicleRecord(passenger_unit) ||
            passenger_unit.IsPlayer()) {
          return;
        }

        std::uint8_t seat_index = 0;
        if (!ResolveSessionDirectPassengerSeatIndex(vehicle_unit, passenger_unit,
                                                    seat_index)) {
          return;
        }

        out[seat_index] = &passenger_unit;
      });
}

[[nodiscard]] const CGUnit_C* ResolveSessionVehicleRootOrAncestor(
    const WorldSession& session,
    const CGUnit_C& unit,
    const CGUnit_C* const stop_at) {
  if (&unit == stop_at && HasLiveSessionVehicleRecord(unit)) {
    return &unit;
  }

  const auto* const initial_passenger =
      unit.Vehicle().GetVehiclePassengerComponent();
  auto vehicle_guid =
      initial_passenger != nullptr ? initial_passenger->GetVehicleUnitGuid() : 0;
  if (!HasVehicleTransitionTargetGuid(vehicle_guid)) {
    return HasLiveSessionVehicleRecord(unit) ? &unit : nullptr;
  }

  std::unordered_set<const CGUnit_C*> visited;
  visited.insert(&unit);
  const CGUnit_C* current =
      session.objects().GetUnit(ObjectGuid(vehicle_guid));
  while (current != nullptr) {
    if (!visited.insert(current).second) {
      return nullptr;
    }

    if (current == stop_at) {
      return current;
    }

    const auto* const passenger = current->Vehicle().GetVehiclePassengerComponent();
    vehicle_guid = passenger != nullptr ? passenger->GetVehicleUnitGuid() : 0;
    if (!HasVehicleTransitionTargetGuid(vehicle_guid)) {
      return current;
    }

    current = session.objects().GetUnit(ObjectGuid(vehicle_guid));
  }

  return nullptr;
}

[[nodiscard]] bool FindSessionExpandedVehicleSeatRecursive(
    const WorldSession& session,
    const CGUnit_C& vehicle_unit,
    int& remaining,
    const CGUnit_C*& out_vehicle_unit,
    std::uint8_t& out_seat_index) {
  const auto* const vehicle_entry = vehicle_unit.Vehicle().GetVehicleEntry();
  if (vehicle_entry == nullptr) {
    return false;
  }

  std::array<const CGUnit_C*, kVehicleSeatSlotCount> child_vehicles{};
  CollectSessionNestedChildVehicles(session, vehicle_unit, child_vehicles);

  for (std::size_t seat_index = 0; seat_index < child_vehicles.size(); ++seat_index) {
    if (const auto* const child_vehicle = child_vehicles[seat_index];
        child_vehicle != nullptr) {
      if (FindSessionExpandedVehicleSeatRecursive(
              session, *child_vehicle, remaining, out_vehicle_unit,
              out_seat_index)) {
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

[[nodiscard]] bool FindSessionExpandedVehicleSeat(
    const WorldSession& session,
    const CGUnit_C& vehicle_unit,
    const int seat_ordinal,
    const CGUnit_C*& out_vehicle_unit,
    std::uint8_t& out_seat_index) {
  if (seat_ordinal < 0) {
    return false;
  }

  int remaining = seat_ordinal;
  out_vehicle_unit = nullptr;
  out_seat_index = 0;
  return FindSessionExpandedVehicleSeatRecursive(
      session, vehicle_unit, remaining, out_vehicle_unit, out_seat_index);
}

[[nodiscard]] const CGUnit_C* FindSessionDirectVehiclePassengerBySeatIndex(
    const WorldSession& session,
    const CGUnit_C& vehicle_unit,
    const std::uint8_t seat_index) {
  if (vehicle_unit.Vehicle().GetVehicleData() == nullptr) {
    return nullptr;
  }

  const CGUnit_C* result = nullptr;
  ForEachSessionVehiclePassengerUnit(
      session, vehicle_unit.Vehicle().GetVehicleData(),
      [&](CGUnit_C& passenger_unit) {
        if (result != nullptr) {
          return;
        }

        std::uint8_t passenger_seat_index = 0;
        if (!ResolveSessionDirectPassengerSeatIndex(
                vehicle_unit, passenger_unit, passenger_seat_index)) {
          return;
        }

        if (passenger_seat_index == seat_index) {
          result = &passenger_unit;
        }
      });
  return result;
}

CGUnit_C *ResolveVehicleUnitByGuid(const CGUnit_C* owner,
                                   const std::uint64_t guid_raw) {
  if (owner == nullptr || owner->object_manager() == nullptr || guid_raw == 0) {
    return nullptr;
  }

  const auto *unit = owner->object_manager()->GetUnit(ObjectGuid(guid_raw));
  return const_cast<CGUnit_C *>(unit);
}

void TransformVec3ByRowMajorAffine(const float* const matrix, Vec3& position) {
  const float x = position.x;
  const float y = position.y;
  const float z = position.z;
  position.x = x * matrix[0] + y * matrix[4] + z * matrix[8] + matrix[12];
  position.y = x * matrix[1] + y * matrix[5] + z * matrix[9] + matrix[13];
  position.z = x * matrix[2] + y * matrix[6] + z * matrix[10] + matrix[14];
}

[[nodiscard]] Vec3 ToVec3(const Position &position) {
  return {position.x, position.y, position.z};
}

[[nodiscard]] bool QueryUnitM2AttachmentPosition(
    const CGUnit_C &unit, const std::uint32_t attachment_lookup_index, float *const out_position,
    const std::optional<openwow::render::RenderVec3> &local_offset = std::nullopt) {
  const std::uint32_t instance_id = unit.GetPrimaryM2InstanceId();
  if (instance_id == 0u || out_position == nullptr) {
    return false;
  }
  auto* const m2_system = unit.m2_system();
  if (m2_system == nullptr) {
    return false;
  }

  const auto query = m2_system->QueryAttachmentPosition(
      instance_id, attachment_lookup_index, local_offset);
  if (query.status != openwow::render::m2::M2ResultStatus::kReady) {
    return false;
  }

  out_position[0] = query.position[0];
  out_position[1] = query.position[1];
  out_position[2] = query.position[2];
  return true;
}

[[nodiscard]] bool QueryUnitM2AttachmentTransform(
    const CGUnit_C &unit, const std::uint32_t attachment_lookup_index, float *const out_matrix) {
  const std::uint32_t instance_id = unit.GetPrimaryM2InstanceId();
  if (instance_id == 0u || out_matrix == nullptr) {
    return false;
  }
  auto* const m2_system = unit.m2_system();
  if (m2_system == nullptr) {
    return false;
  }

  const auto query = m2_system->QueryAttachmentTransformMatrix(
      instance_id, attachment_lookup_index);
  if (query.status != openwow::render::m2::M2ResultStatus::kReady) {
    return false;
  }

  std::copy(query.matrix.begin(), query.matrix.end(), out_matrix);
  return true;
}

[[nodiscard]] bool QueryUnitM2ModelWorldPoint(const CGUnit_C &unit, float *const out_position) {
  const std::uint32_t instance_id = unit.GetPrimaryM2InstanceId();
  if (instance_id == 0u || out_position == nullptr) {
    return false;
  }
  auto* const m2_system = unit.m2_system();
  if (m2_system == nullptr) {
    return false;
  }

  const auto query = m2_system->QueryModelWorldPoint(instance_id);
  if (query.status != openwow::render::m2::M2ResultStatus::kReady) {
    return false;
  }

  out_position[0] = query.position[0];
  out_position[1] = query.position[1];
  out_position[2] = query.position[2];
  return true;
}

[[nodiscard]] bool QueryUnitM2ModelWorldTransform(const CGUnit_C &unit, float *const out_matrix) {
  const std::uint32_t instance_id = unit.GetPrimaryM2InstanceId();
  if (instance_id == 0u || out_matrix == nullptr) {
    return false;
  }
  auto* const m2_system = unit.m2_system();
  if (m2_system == nullptr) {
    return false;
  }

  const auto query = m2_system->QueryModelWorldTransformMatrix(instance_id);
  if (query.status != openwow::render::m2::M2ResultStatus::kReady) {
    return false;
  }

  std::copy(query.matrix.begin(), query.matrix.end(), out_matrix);
  return true;
}

[[nodiscard]] float Distance(const Vec3 &lhs, const Vec3 &rhs) {
  const auto dx = lhs.x - rhs.x;
  const auto dy = lhs.y - rhs.y;
  const auto dz = lhs.z - rhs.z;
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

[[nodiscard]] bool HasReachedTick(const std::uint32_t now,
                                  const std::uint32_t deadline) {
  return static_cast<std::int32_t>(now - deadline) >= 0;
}

[[nodiscard]] std::uint32_t DurationSecondsToMs(const float duration_seconds) {
  return static_cast<std::uint32_t>(std::max(duration_seconds, 0.0f) * 1000.0f);
}

void StorePassengerVisualTransform(CGUnit_C &owner, const Vec3 &position, const float facing,
                                   const float scale, const Vec3 &up_vector) {
  const openwow::render::RenderVec3 position_values{position.x, position.y, position.z};
  const openwow::render::RenderVec3 up_values{up_vector.x, up_vector.y, up_vector.z};
  const openwow::render::RenderMatrix4x4 matrix =
      openwow::render::BuildM2AttachmentTransformMatrix(
          openwow::render::RenderVec3View{position_values}, facing, scale,
          openwow::render::RenderVec3View{up_values},
          openwow::render::AttachmentOrientationMode::kNone);
  owner.SetVisualModelWorldTransform(matrix.data());
}

[[nodiscard]] float ClampTransitionDuration(const float duration_seconds,
                                           const float min_duration_seconds,
                                           float max_duration_seconds) {
  const auto minimum = std::max(min_duration_seconds, 0.0f);
  if (max_duration_seconds < kVehicleTransitionEpsilon) {
    max_duration_seconds = kVehicleTransitionDefaultDurationSeconds;
  } else if (max_duration_seconds > kVehicleTransitionMaxDurationSeconds) {
    max_duration_seconds = kVehicleTransitionMaxDurationSeconds;
  }

  if (max_duration_seconds < minimum) {
    max_duration_seconds = minimum;
  }
  return std::clamp(duration_seconds, minimum, max_duration_seconds);
}

[[nodiscard]] bool UsesExitTransitionProfile(
    const CGUnit_C &owner, const openwow::data::dbc::VehicleSeatEntry &seat_entry) {
  const auto owner_bits = owner.GetUInt32(UNIT_FIELD_BYTES_1);
  const auto required_seat_flag = (owner_bits & 0x40u) != 0u ? 0x8u : 0x8000u;
  return (seat_entry.flags & required_seat_flag) != 0u;
}

const openwow::data::dbc::VehicleSeatEntry *
ResolveTransitionSeatEntry(const CGUnit_C *owner, const std::uint64_t target_guid,
                           const std::uint8_t target_seat) {
  if (owner == nullptr || owner->object_manager() == nullptr ||
      owner->dbc_loader() == nullptr) {
    return nullptr;
  }

  const auto *const dbc = owner->dbc_loader();
  const auto *vehicle_unit =
      owner->object_manager()->GetUnit(ObjectGuid(target_guid));
  if (HasVehicleTransitionTargetGuid(target_guid) && vehicle_unit != nullptr &&
      vehicle_unit->Vehicle().GetVehicleEntry() != nullptr && target_seat < 8u) {
    const auto seat_record_id = vehicle_unit->Vehicle().GetVehicleEntry()->seat_id[target_seat];
    if (seat_record_id != 0u) {
      return dbc->vehicle_seat().LookupEntry(seat_record_id);
    }
  }

  const auto *const passenger = owner->Vehicle().GetVehiclePassengerComponent();
  const auto *const current_vehicle =
      passenger != nullptr ? ResolveVehicleUnitByGuid(owner,
                                                       passenger->GetVehicleUnitGuid())
                           : nullptr;
  if (current_vehicle == nullptr || current_vehicle->Vehicle().GetVehicleEntry() == nullptr) {
    return nullptr;
  }

  const auto current_seat = passenger->GetAltVehicleGuid() != 0u
                                ? passenger->GetAltSeatIndex()
                                : passenger->GetPrimarySeatIndex();
  if (current_seat >= 8u) {
    return nullptr;
  }
  const auto seat_record_id = current_vehicle->Vehicle().GetVehicleEntry()->seat_id[current_seat];
  return seat_record_id != 0u
             ? dbc->vehicle_seat().LookupEntry(seat_record_id)
             : nullptr;
}

void FireVehicleSeatDetailedEvent(
    const std::uint64_t player_guid,
    const openwow::data::dbc::VehicleEntry *vehicle_entry,
    const openwow::data::dbc::VehicleSeatEntry *seat_entry,
    const char *event_name) {
  auto &dispatch = ui::game::ScriptEventDispatch::Get();
  if (!dispatch.IsInitialized()) {
    return;
  }

  const bool has_control_seat =
      seat_entry != nullptr && (seat_entry->flags & 0x20000000u) != 0u;

  static constexpr const char *kLocomotionTypes[] = {"Natural", "Mechanical"};
  std::string locomotion_type;
  if (seat_entry != nullptr && seat_entry->temporary_portrait_type < 2u) {
    locomotion_type = kLocomotionTypes[seat_entry->temporary_portrait_type];
  }

  const std::string vehicle_name;

  const bool has_vehicle_actions =
      seat_entry != nullptr && (seat_entry->flags & 0x800u) != 0u;

  const int vehicle_ui_indicator_id =
      vehicle_entry != nullptr
          ? static_cast<int>(vehicle_entry->vehicle_ui_indicator_id)
          : 0;

  dispatch.FirePerUnitEventWithArgs(
      event_name, player_guid,
      {ui::game::EventArg{has_control_seat},
       ui::game::EventArg{locomotion_type},
       ui::game::EventArg{vehicle_name},
       ui::game::EventArg{has_vehicle_actions},
       ui::game::EventArg{vehicle_ui_indicator_id}});
}

void FireVehicleSeatEvents(
    const std::uint64_t player_guid,
    const openwow::data::dbc::VehicleEntry *vehicle_entry,
    const openwow::data::dbc::VehicleSeatEntry *seat_entry,
    const std::uint32_t event_flags) {
  if ((event_flags & 1u) != 0u) {
    FireVehicleSeatDetailedEvent(
        player_guid, vehicle_entry, seat_entry,
        ui::game::events::UNIT_ENTERING_VEHICLE);
  }
  if ((event_flags & 2u) != 0u) {
    FireVehicleSeatDetailedEvent(
        player_guid, vehicle_entry, seat_entry,
        ui::game::events::UNIT_ENTERED_VEHICLE);
  }
  if ((event_flags & 4u) != 0u) {
    auto &dispatch = ui::game::ScriptEventDispatch::Get();
    if (dispatch.IsInitialized()) {
      dispatch.FirePerUnitEvent(
          ui::game::events::UNIT_EXITING_VEHICLE, player_guid);
    }
  }
  if ((event_flags & 8u) != 0u) {
    auto &dispatch = ui::game::ScriptEventDispatch::Get();
    if (dispatch.IsInitialized()) {
      dispatch.FirePerUnitEventWithArgs(
          ui::game::events::UNIT_EXITED_VEHICLE, player_guid,
          {ui::game::EventArg{std::string{}}});
    }
  }
}

}

std::int32_t ResolveVehicleSeatM2AttachmentLookup(
    const std::int32_t seat_attachment_id) noexcept {
  if (seat_attachment_id < 0 ||
      static_cast<std::size_t>(seat_attachment_id) >= kVehicleSeatM2AttachmentLookup.size()) {
    return -1;
  }

  return kVehicleSeatM2AttachmentLookup[static_cast<std::size_t>(seat_attachment_id)];
}

namespace {

constexpr std::uint32_t kVehicleSeatTransitionFlagCanEjectPassenger = 0x20u;

struct VehicleSeatOccupantSelection {
  const CGUnit_C* active_mover = nullptr;
  const CGUnit_C* seat_vehicle = nullptr;
  const CGUnit_C* passenger = nullptr;
  std::uint8_t physical_seat_index = 0;
};

[[nodiscard]] std::optional<VehicleSeatOccupantSelection>
ResolveVehicleSeatOccupant(const WorldSession& session,
                           const int one_based_seat_index) {
  if (one_based_seat_index <= 0) {
    return std::nullopt;
  }

  const auto* const mover = session.objects().GetUnit(
      session.player_control_runtime().ActiveMoverGuid());
  if (mover == nullptr) {
    return std::nullopt;
  }

  const auto* const root_vehicle =
      ResolveSessionVehicleRootOrAncestor(session, *mover, nullptr);
  if (root_vehicle == nullptr ||
      !HasLiveSessionVehicleRecord(*root_vehicle)) {
    return std::nullopt;
  }

  const CGUnit_C* seat_vehicle = nullptr;
  std::uint8_t physical_seat_index = 0;
  if (!FindSessionExpandedVehicleSeat(
          session, *root_vehicle, one_based_seat_index - 1, seat_vehicle,
          physical_seat_index) ||
      seat_vehicle == nullptr) {
    return std::nullopt;
  }

  const auto* const passenger =
      FindSessionDirectVehiclePassengerBySeatIndex(
          session, *seat_vehicle, physical_seat_index);
  if (passenger == nullptr) {
    return std::nullopt;
  }

  return VehicleSeatOccupantSelection{
      mover, seat_vehicle, passenger, physical_seat_index};
}

}

std::optional<VehicleSeatEjectionTarget> ResolveVehicleSeatEjectionTarget(
    const WorldSession& session,
    const int one_based_seat_index) {
  const auto selection =
      ResolveVehicleSeatOccupant(session, one_based_seat_index);
  if (!selection.has_value()) {
    return std::nullopt;
  }

  if (ResolveSessionVehicleRootOrAncestor(
          session, *selection->passenger, selection->active_mover) !=
      selection->active_mover) {
    return std::nullopt;
  }

  const auto* const seat_entry =
      LookupVehicleSeatEntryForVehicleSeat(
          session, *selection->seat_vehicle, selection->physical_seat_index);
  return VehicleSeatEjectionTarget{
      selection->passenger->GetGuid(),
      seat_entry != nullptr &&
          (seat_entry->transition_flags & kVehicleSeatTransitionFlagCanEjectPassenger) != 0u};
}

bool CanEjectPassengerFromSeat(
    const WorldSession& session,
    const int one_based_seat_index) {
  const auto target = ResolveVehicleSeatEjectionTarget(session, one_based_seat_index);
  return target.has_value() && target->can_eject;
}

bool EjectPassengerFromSeat(
    const WorldSession& session,
    const int one_based_seat_index) {
  const auto selection =
      ResolveVehicleSeatOccupant(session, one_based_seat_index);
  if (!selection.has_value() || selection->passenger->GetGuid().IsEmpty()) {
    return false;
  }

  const auto passenger_guid = selection->passenger->GetGuid();
  vehicle::SendEjectPassengerPacket(
      static_cast<int>(passenger_guid.GetLowPart()),
      static_cast<int>(passenger_guid.GetHighPart()));
  return true;
}

bool VehiclePassengerC::s_initialized_ = false;
std::vector<VehiclePassengerQueueEntry> VehiclePassengerC::s_queued_transitions_;
std::uint32_t VehiclePassengerC::s_registered_type_id_ = 0;
std::vector<VehiclePassengerC *> VehiclePassengerC::s_active_passengers_;

VehiclePassengerC::VehiclePassengerC() {
  RegisterActive();
}

VehiclePassengerC::~VehiclePassengerC() {
  UnregisterActive();
  ClearTransitionData();
}

bool VehiclePassengerC::IsEntering() const {
  auto state = static_cast<std::uint32_t>(transition_state_);
  return state == 1 || state == 2;
}

bool VehiclePassengerC::IsExiting() const {
  auto state = static_cast<std::uint32_t>(transition_state_);
  return state == 2 || state == 5;
}

std::uint64_t VehiclePassengerC::GetVehicleUnitGuid() const {
  if (alt_vehicle_guid_ != 0) {
    return alt_vehicle_guid_;
  }
  return primary_vehicle_guid_;
}

CGUnit_C *VehiclePassengerC::GetVehicleUnit() const {
  if (auto *alt_vehicle = ResolveVehicleUnitByGuid(owner_, alt_vehicle_guid_)) {
    return alt_vehicle;
  }

  return ResolveVehicleUnitByGuid(owner_, primary_vehicle_guid_);
}

CGUnit_C *VehiclePassengerC::GetVehicleObject() const {
  if (transition_state_ != VehiclePassengerTransitionType::kExit) {
    if (auto* rescue_vehicle =
            ResolveVehicleUnitByGuid(owner_, rescue_vehicle_guid_)) {
      return rescue_vehicle;
    }
  }

  if (owner_ == nullptr) {
    return nullptr;
  }

  return static_cast<CGUnit_C *>(UnitVehicle_FindRootVehicle(owner_, nullptr));
}

double VehiclePassengerC::GetVehiclePitch() const {
  if (HasFlag(VehiclePassengerFlag::kMouseYawOverride)) {
    return static_cast<double>(mouse_yaw_override_);
  }
  if (owner_) {
    return static_cast<double>(owner_->GetFlyHeight());
  }
  return 0.0;
}

const openwow::data::dbc::VehicleSeatEntry *VehiclePassengerC::GetSeatEntry() const {
  if (seat_entry_ != nullptr) {
    return seat_entry_;
  }

  if (owner_ == nullptr) {
    return nullptr;
  }

  return ResolveTransitionSeatEntry(owner_, GetVehicleUnitGuid(), alt_seat_index_);
}

Vec3 VehiclePassengerC::GetPassengerPosition(const Vec3 &offset,
                                             const Vec3 &base_position) const {
  std::uint64_t vehicle_guid = prev_vehicle_guid_;
  if (vehicle_guid == 0) {
    return base_position;
  }

  const auto* const objects = owner_ != nullptr ? owner_->object_manager() : nullptr;
  if (objects == nullptr) {
    return base_position;
  }

  const auto* const vehicle_object =
      CGObject_HasFlags(*objects, vehicle_guid, kTypeMaskObject);
  if (vehicle_object == nullptr) {
    return base_position;
  }

  if ((vehicle_object->GetTypeMask() & kTypeMaskUnit) != 0u) {
    return ToVec3(vehicle_object->GetPosition()) + offset;
  }

  Vec3 result = offset;
  float vehicle_matrix[16]{};
  vehicle_object->GetWorldMatrix(vehicle_matrix);
  TransformVec3ByRowMajorAffine(vehicle_matrix, result);
  return result;
}

Vec3 VehiclePassengerC::GetScaledSeatOffset(const float *attachment_data) {
  if (!attachment_data)
    return {};
  float scale = attachment_data[35];
  return {
      attachment_data[25] * scale,
      attachment_data[26] * scale,
      attachment_data[27] * scale,
  };
}

void VehiclePassengerC::UpdateBoneAttachmentOffset(
    const openwow::data::dbc::VehicleSeatEntry *seat_entry) {
  if (seat_entry == nullptr || owner_ == nullptr) {
    return;
  }

  const std::int32_t attachment_id = seat_entry->passenger_attachment_id;
  float bone_pos[3]{};
  if (attachment_id >= 0 &&
      QueryUnitM2AttachmentPosition(*owner_, static_cast<std::uint32_t>(attachment_id),
                                    bone_pos)) {
    cached_bone_offset_ = {-bone_pos[0], -bone_pos[1], -bone_pos[2]};
    SetFlag(VehiclePassengerFlag::kHasBoneOffset);
    SetFlag(VehiclePassengerFlag::kBlendAnims);
  } else {
    cached_bone_offset_ = {};
    ClearFlag(VehiclePassengerFlag::kHasBoneOffset);
    SetFlag(VehiclePassengerFlag::kBlendAnims);
  }
}

void VehiclePassengerC::ComputeSeatWorldPosition(
    CGUnit_C *vehicle_unit,
    const openwow::data::dbc::VehicleSeatEntry *seat_entry,
    Vec3 &out_position) const {
  if (vehicle_unit == nullptr || seat_entry == nullptr) {
    return;
  }

  if (!HasFlag(VehiclePassengerFlag::kBlendAnims)) {
    const_cast<VehiclePassengerC *>(this)->UpdateBoneAttachmentOffset(seat_entry);
  }

  const std::int32_t bone_lookup =
      ResolveVehicleSeatM2AttachmentLookup(seat_entry->attachment_id);

  if (HasFlag(VehiclePassengerFlag::kBlendAnims) &&
      HasFlag(VehiclePassengerFlag::kHasBoneOffset)) {
    out_position = {
        seat_entry->attachment_offset_x + cached_bone_offset_.x,
        seat_entry->attachment_offset_y + cached_bone_offset_.y,
        seat_entry->attachment_offset_z + cached_bone_offset_.z,
    };
  } else {
    out_position = {
        seat_entry->attachment_offset_x,
        seat_entry->attachment_offset_y,
        seat_entry->attachment_offset_z,
    };
  }

  if (bone_lookup >= 0) {
    float bone_matrix[16]{};
    if (QueryUnitM2AttachmentTransform(*vehicle_unit, static_cast<std::uint32_t>(bone_lookup),
                                       bone_matrix)) {
      TransformVec3ByRowMajorAffine(bone_matrix, out_position);
      return;
    }
  }

  float world_matrix[16]{};
  if (QueryUnitM2ModelWorldTransform(*vehicle_unit, world_matrix) ||
      vehicle_unit->Presentation().ModelToWorldMatrix(world_matrix) != nullptr) {
    TransformVec3ByRowMajorAffine(world_matrix, out_position);
  }
}

Vec3 VehiclePassengerC::GetSeatPosition() const {
  const auto *seat = seat_entry_;

  if (transition_state_ != VehiclePassengerTransitionType::kExit
      && alt_vehicle_guid_ != 0) {
    auto *vehicle_unit = ResolveVehicleUnitByGuid(owner_, alt_vehicle_guid_);
    if (vehicle_unit != nullptr) {
      float model_origin[3]{};
      if (seat != nullptr && QueryUnitM2ModelWorldPoint(*vehicle_unit, model_origin)) {
        Vec3 result{};
        const_cast<VehiclePassengerC *>(this)->ComputeSeatWorldPosition(
            vehicle_unit, seat, result);
        return result;
      }
    }
  }

  if (owner_ == nullptr) {
    return {};
  }
  return ToVec3(owner_->GetPosition());
}

void VehiclePassengerC::ClearTransitionData() {
  if (transition_data_) {
    net::CDataStore_DeleteDtor(transition_data_, 1);
    transition_data_ = nullptr;
  }
  pending_monster_move_.reset();
}

void VehiclePassengerC::CreateTransitionData(net::CDataStore& source) {
  ClearTransitionData();

  auto* store = static_cast<net::CDataStore*>(
      core::SMemAlloc(sizeof(net::CDataStore),
                      ".\\VehiclePassenger_C.cpp", 0x430, 0));
  if (store == nullptr) {
    return;
  }

  store->data = nullptr;
  store->window_base = 0;
  store->window_size = 0xFFFFFFFFu;
  store->write_pos = 0;
  store->read_pos = 0;
  store->vtable = net::CDataStore_BaseVTable();
  transition_data_ = store;

  if (source.read_pos > source.write_pos) {
    net::CDataStore_ResetReadPos(*store);
    return;
  }

  const std::uint32_t remaining = source.write_pos - source.read_pos;
  const std::uint8_t* source_bytes = nullptr;
  if (!net::CDataStore_GetReadSpanPointer(source, source_bytes, remaining)) {
    net::CDataStore_ResetReadPos(*store);
    return;
  }

  net::CDataStore_PutBytes(*store, source_bytes, remaining);
  net::CDataStore_ResetReadPos(*store);
}

void VehiclePassengerC::HandleTransition(WorldSession& session,
                                         double ,
                                         VehiclePassengerTransitionType type,
                                         std::uint64_t vehicle_guid, std::uint8_t seat_index,
                                         std::uint32_t timing) {
  if (HasFlag(VehiclePassengerFlag::kSeatAttached)) {
    ClearFlag(VehiclePassengerFlag::kSeatAttached);
    if (primary_seat_index_ < 8) {
      auto *unit = ResolveVehicleUnitByGuid(owner_, primary_vehicle_guid_);
      if (unit) {
        auto *vdata = unit->Vehicle().GetVehicleData();
        if (vdata) {
          vehicle::VehiclePassenger_ClearSeatBit(vdata, primary_seat_index_);
        }
      }
    }
  }

  if (transition_state_ == VehiclePassengerTransitionType::kAttached &&
      rescue_vehicle_guid_ != 0u) {
    if (owner_ != nullptr && seat_entry_ != nullptr) {
      auto* const root_vehicle =
          ResolveVehicleUnitByGuid(owner_, rescue_vehicle_guid_);
      if (root_vehicle != nullptr && root_vehicle->Vehicle().GetVehicleData() != nullptr) {
        const auto owner_guid = owner_->GetGuid().GetRawValue();
        (void)vehicle::VehiclePassenger_HandleTransition(
            session, root_vehicle->Vehicle().GetVehicleData(),
            static_cast<std::uint32_t>(seat_entry_->vehicle_ride_anim_loop_bone),
            static_cast<int>(owner_guid),
            static_cast<int>(owner_guid >> 32u));
      }
    }
    rescue_vehicle_guid_ = 0u;
  }

  auto prev_state = transition_state_;

  switch (type) {
  case VehiclePassengerTransitionType::kEnterWithPos:
  case VehiclePassengerTransitionType::kTransferWithPos:
  case VehiclePassengerTransitionType::kEnterCaptured:
  case VehiclePassengerTransitionType::kEject: {

    if (type == VehiclePassengerTransitionType::kEnterWithPos ||
        type == VehiclePassengerTransitionType::kEnterCaptured) {
      prev_vehicle_guid_ = primary_vehicle_guid_;
    }

    if (owner_ != nullptr) {
      saved_position = ToVec3(owner_->GetPosition());
      const auto owner_facing = owner_->GetOrientation();
      switch (prev_state) {
      case VehiclePassengerTransitionType::kExit:
      case VehiclePassengerTransitionType::kAttached:
        transition_facing_blended_ = owner_facing;
        transition_facing_from_ = owner_facing;
        break;
      default:
        transition_facing_from_ = transition_facing_blended_;
        break;
      }
      transition_facing_current_ = owner_facing;
    }
    break;
  }

  case VehiclePassengerTransitionType::kAttached: {

    alt_vehicle_guid_ = vehicle_guid;
    primary_vehicle_guid_ = vehicle_guid;
    alt_seat_index_ = seat_index;
    primary_seat_index_ = seat_index;
    break;
  }

  default:
    break;
  }

  if (type == VehiclePassengerTransitionType::kAttached) {
    AttachToSeat();
  }

  ClearFlag(VehiclePassengerFlag::kHasInputControl);

  ClearFlag(VehiclePassengerFlag::kBlendAnims);

  if (type == VehiclePassengerTransitionType::kExit) {

    ClearFlag(VehiclePassengerFlag::kModelHidden);
  }

  if (prev_state == VehiclePassengerTransitionType::kEject &&
      type == VehiclePassengerTransitionType::kExit && owner_ != nullptr) {
    const auto *seat_entry = seat_entry_ != nullptr ? seat_entry_
                                                    : ResolveTransitionSeatEntry(owner_,
                                                                                 vehicle_guid,
                                                                                 seat_index);
    auto exit_animation_id = seat_entry != nullptr ? seat_entry->exit_anim_end : -1;
    if (exit_animation_id == 39 && owner_->Animation().GetStandState() != 0u) {
      exit_animation_id = 187;
    }

    Vehicle_PlayTransitionEmote(*owner_, seat_entry, exit_animation_id);
  }

  transition_state_ = type;
  timing_param_ = timing;
  transition_deadline_ms_ = timing;
  ClearFlag(VehiclePassengerFlag::kActive);

  if (type == VehiclePassengerTransitionType::kAttached) {
    SetFlag(VehiclePassengerFlag::kSameVehicleTransfer);
  } else {
    ClearFlag(VehiclePassengerFlag::kSameVehicleTransfer);
  }

  constexpr std::uint32_t kSeatFlagVehicleRideAnimation = 0x00020000u;
  if (type == VehiclePassengerTransitionType::kAttached && owner_ != nullptr &&
      seat_entry_ != nullptr &&
      (seat_entry_->flags & kSeatFlagVehicleRideAnimation) != 0u &&
      seat_entry_->vehicle_ride_anim_loop >= 0 &&
      seat_entry_->vehicle_ride_anim_loop < 506) {
    auto* const vehicle_unit = ResolveVehicleUnitByGuid(owner_, vehicle_guid);
    auto* const root_vehicle = vehicle_unit != nullptr
                                   ? const_cast<CGUnit_C*>(
                                         ResolveRootVehicleUnit(*vehicle_unit))
                                   : nullptr;
    void* const root_data =
        root_vehicle != nullptr ? root_vehicle->Vehicle().GetVehicleData() : nullptr;
    if (root_data != nullptr && vehicle::Vehicle_C_StartSeatAnimation(
                                    root_data,
                                    static_cast<std::uint32_t>(
                                        seat_entry_->vehicle_ride_anim_loop_bone),
                                    static_cast<std::uint32_t>(
                                        seat_entry_->vehicle_ride_anim_loop))) {
      const auto owner_guid = owner_->GetGuid().GetRawValue();
      (void)vehicle::Vehicle_C_AddPendingSeatTransition(
          root_data, static_cast<int>(owner_guid),
          static_cast<int>(owner_guid >> 32u),
          static_cast<std::uint32_t>(seat_entry_->vehicle_ride_anim_loop_bone),
          vehicle::PendingSeatTransitionPolicy::kKeepAnimationActive);
      rescue_vehicle_guid_ = root_vehicle->GetGuid().GetRawValue();
    }
  }

}

void VehiclePassengerC::UpdateSeatState(
    WorldSession& session, double timestamp, std::uint64_t target_guid,
    std::uint8_t target_seat, bool allow_transition_profile) {

  ProcessPendingSeatChange(session);

  const auto *seat_entry = ResolveTransitionSeatEntry(owner_, target_guid, target_seat);
  seat_entry_ = seat_entry;
  const bool has_target = HasVehicleTransitionTargetGuid(target_guid);

  switch (transition_state_) {
  case VehiclePassengerTransitionType::kExit:
  case VehiclePassengerTransitionType::kEject:
    prev_vehicle_guid_ = 0;
    prev_seat_index_ = 0xFF;
    break;
  case VehiclePassengerTransitionType::kTransferWithPos:
  case VehiclePassengerTransitionType::kAttached:
    prev_vehicle_guid_ = alt_vehicle_guid_;
    prev_seat_index_ = alt_seat_index_;
    break;
  default:
    break;
  }

  alt_vehicle_guid_ = has_target ? target_guid : 0;
  alt_seat_index_ = target_seat;
  ClearFlag(VehiclePassengerFlag::kActive);

  VehiclePassengerTransitionType trans_type = has_target ? VehiclePassengerTransitionType::kAttached
                                                         : VehiclePassengerTransitionType::kExit;

  if (allow_transition_profile && seat_entry != nullptr) {
    if (has_target) {
      if ((seat_entry->flags & 0x1u) != 0u &&
          seat_entry->enter_speed > kVehicleTransitionEpsilon) {
        trans_type = seat_entry->enter_pre_delay > kVehicleTransitionEpsilon
                         ? VehiclePassengerTransitionType::kEnterWithPos
                         : VehiclePassengerTransitionType::kTransferWithPos;
      }
    } else if (owner_ != nullptr && UsesExitTransitionProfile(*owner_, *seat_entry) &&
               seat_entry->exit_speed > kVehicleTransitionEpsilon) {
      trans_type = seat_entry->exit_pre_delay > kVehicleTransitionEpsilon
                       ? VehiclePassengerTransitionType::kEnterCaptured
                       : VehiclePassengerTransitionType::kEject;
    }
  }

  if (!has_target) {

    target_guid = prev_vehicle_guid_;
  }

  HandleTransition(session, timestamp, trans_type, target_guid, target_seat,
                   openwow::core::GameClock::GetTickCount32());
}

void VehiclePassengerC::HandleVehicleAssignmentChange(
    WorldSession& session, double timestamp,
                                                      const std::uint64_t target_guid,
                                                      const std::uint8_t target_seat,
                                                      CGUnit_C* const target_unit,
                                                      const bool from_update) {
  if (target_guid != 0 && target_unit != nullptr) {
    seat_entry_ =
        LookupVehicleSeatEntryForVehicleSeat(session, *target_unit, target_seat);
  } else {
    seat_entry_ = nullptr;
  }

  std::uint64_t rescue_guid = 0;
  if (target_unit != nullptr) {
    if (auto* const root_vehicle =
            static_cast<CGUnit_C*>(UnitVehicle_FindRootVehicle(target_unit, nullptr));
        root_vehicle != nullptr) {
      rescue_guid = root_vehicle->GetGuid().GetRawValue();
    }
  }
  CascadeVehicleGuid(rescue_guid);

  const bool allow_seat_profile = target_guid == 0 || ObjectGuid(target_guid).IsVehicle();
  const bool transition_profile_enabled =
      owner_ != nullptr &&
      VehicleTransitionProfileEnabled(*owner_, seat_entry_, allow_seat_profile);

  if (primary_vehicle_guid_ != 0 && target_unit == nullptr &&
      VehiclePassenger_IsUpdatePending()) {
    if (owner_ != nullptr) {
      const auto owner_guid = owner_->GetGuid().GetRawValue();
      VehiclePassengerQueueEntry entry{};
      entry.guid_lo = static_cast<std::uint32_t>(owner_guid & 0xFFFFFFFFu);
      entry.guid_hi = static_cast<std::uint32_t>(owner_guid >> 32);
      entry.target_lo = static_cast<std::uint32_t>(target_guid & 0xFFFFFFFFu);
      entry.target_hi = static_cast<std::uint32_t>(target_guid >> 32);
      entry.seat = target_seat;
      entry.use_transition = transition_profile_enabled ? 1u : 0u;
      QueueTransition(entry);
    }
    return;
  }

  if (!from_update) {
    UpdateSeatState(session, timestamp, target_guid, target_seat,
                    transition_profile_enabled);
  }
}

void VehiclePassengerC::ResetPositionVectors() {
  position_offset = {};
  seat_attachment_offset_ = {};
  exit_saved_position_ = {};
  exit_facing_offset_ = {};
  cached_bone_offset_ = {};
}

void VehiclePassengerC::AttachToSeat() {
  last_attachment_animation_status_ = openwow::render::m2::M2ResultStatus::kNotReady;

  if (alt_seat_index_ >= vehicle::kDbcSeatCount) {
    last_attachment_animation_status_ = openwow::render::m2::M2ResultStatus::kFailed;
    return;
  }

  auto *const vehicle_unit = ResolveVehicleUnitByGuid(owner_, alt_vehicle_guid_);
  if (vehicle_unit == nullptr) {
    last_attachment_animation_status_ = openwow::render::m2::M2ResultStatus::kFailed;
    return;
  }

  auto *const vehicle_data = vehicle_unit->Vehicle().GetVehicleData();
  if (vehicle_data == nullptr) {
    last_attachment_animation_status_ = openwow::render::m2::M2ResultStatus::kFailed;
    return;
  }

  vehicle::VehiclePassenger_SetSeatBit(vehicle_data, alt_seat_index_);
  SetFlag(VehiclePassengerFlag::kSeatAttached);

  if (owner_ == nullptr) {
    return;
  }

  const auto vehicle_instance_id = vehicle_unit->GetPrimaryM2InstanceId();
  const auto passenger_instance_id = owner_->GetPrimaryM2InstanceId();
  if (vehicle_instance_id == 0u || passenger_instance_id == 0u) {
    return;
  }
  auto* const m2_system = owner_->m2_system();
  if (m2_system == nullptr) {
    return;
  }

  last_attachment_animation_status_ =
      m2_system->CopyActiveAnimationState(vehicle_instance_id,
                                          passenger_instance_id);
}

void VehiclePassengerC::DetachFromSeat() {
  if (HasFlag(VehiclePassengerFlag::kSeatAttached)) {
    ClearFlag(VehiclePassengerFlag::kSeatAttached);
    if (primary_seat_index_ < 8) {
      auto *unit = ResolveVehicleUnitByGuid(owner_, primary_vehicle_guid_);
      if (unit) {
        auto *vehicle_data = unit->Vehicle().GetVehicleData();
        if (vehicle_data) {
          vehicle::VehiclePassenger_ClearSeatBit(vehicle_data, primary_seat_index_);
        }
      }
    }
  }

  auto *vehicle_unit = GetVehicleUnit();
  void *vehicle_system = vehicle_unit ? vehicle_unit->Vehicle().GetVehicleData() : nullptr;
  if (vehicle_system) {
    vehicle::Vehicle_C_ComputeAvailableSeatMask(vehicle_system);
  }

  UnregisterActive();
}

void VehiclePassengerC::OnVehicleGuidUpdate(WorldSession& session,
                                            double timestamp,
                                            std::uint64_t new_guid) {
  if (owner_ == nullptr) {
    return;
  }

  if (!UnitVehicle_TryAttachPassengerFromUpdate(
          session, *owner_, timestamp, new_guid,
          owner_->Movement().Data().GetTransportSeat())) {

    if (!owner_->IsPlayer()) {
      return;
    }

    if (transition_state_ == VehiclePassengerTransitionType::kExit) {
      return;
    }

    const std::uint64_t owner_guid = owner_->GetGuid().GetRawValue();
    auto &dispatch = ui::game::ScriptEventDispatch::Get();
    if (dispatch.IsInitialized()) {

      dispatch.FirePerUnitEventWithArgs(
          ui::game::events::UNIT_EXITED_VEHICLE, owner_guid,
          {ui::game::EventArg{std::string{}}});
    }
  }
}

void VehiclePassengerC::ProcessPendingSeatChange(WorldSession& session) {
  if (!HasFlag(VehiclePassengerFlag::kPendingSeatChange)) {
    return;
  }

  ClearFlag(VehiclePassengerFlag::kPendingSeatChange);

  if (owner_ != nullptr && pending_root_vehicle_guid_ != 0u) {
    if (auto* const root =
            ResolveVehicleUnitByGuid(owner_, pending_root_vehicle_guid_);
        root != nullptr && root->Vehicle().GetVehicleData() != nullptr) {
      const auto owner_guid = owner_->GetGuid().GetRawValue();
      vehicle::Vehicle_C_RemovePendingSeatTransition(
          root->Vehicle().GetVehicleData(), static_cast<int>(owner_guid),
          static_cast<int>(owner_guid >> 32u));
    }
  }

  if (pending_monster_move_.has_value()) {
    session.FeedSplineManager(*pending_monster_move_);
  }

  ClearTransitionData();
  pending_root_vehicle_guid_ = 0u;
  pending_target_guid_ = 0u;
  pending_target_seat_index_ = 0xFFu;
}

namespace {

[[nodiscard]] bool MatchesSeatSuffix(const std::uint32_t fourcc,
                                     const std::uint8_t seat_index) {
  const auto suffix = static_cast<std::uint8_t>(fourcc >> 24u);
  if (suffix == static_cast<std::uint8_t>('0')) {
    return true;
  }
  return suffix >= static_cast<std::uint8_t>('1') &&
         suffix <= static_cast<std::uint8_t>('8') &&
         seat_index == static_cast<std::uint8_t>(suffix - '1');
}

}

void VehiclePassengerC::BeginPendingSeatChange(
    const std::uint64_t root_vehicle_guid,
    const std::uint64_t target_guid,
    const std::uint8_t target_seat,
    const openwow::data::dbc::VehicleSeatEntry& seat_entry,
    const bool entering,
    const std::uint32_t current_tick_ms) {
  SetFlag(VehiclePassengerFlag::kPendingSeatChange);
  pending_root_vehicle_guid_ = root_vehicle_guid;
  pending_target_guid_ = target_guid;
  pending_target_seat_index_ = target_seat;

  const std::uint32_t timing_flag = entering ? 0x10000000u : 0x08000000u;
  if ((seat_entry.flags & timing_flag) != 0u) {
    const float seconds = entering ? seat_entry.vehicle_enter_anim_delay
                                   : seat_entry.vehicle_exit_anim_delay;
    const auto duration_ms = static_cast<std::uint32_t>(
        std::clamp(seconds * 1000.0f, 0.0f, 3000.0f));
    SetFlag(VehiclePassengerFlag::kTransitionInProgress);
    pending_seat_change_deadline_ms_ = current_tick_ms + duration_ms;
    return;
  }

  ClearFlag(VehiclePassengerFlag::kTransitionInProgress);
  if (entering) {
    SetFlag(VehiclePassengerFlag::kEnterDirection);
  } else {
    ClearFlag(VehiclePassengerFlag::kEnterDirection);
  }
  pending_seat_change_deadline_ms_ = current_tick_ms + 3000u;
}

bool VehiclePassengerC::AcceptsPendingEnterAnimation(
    const std::uint32_t fourcc) const {
  return HasFlag(VehiclePassengerFlag::kPendingSeatChange) &&
         !HasFlag(VehiclePassengerFlag::kTransitionInProgress) &&
         HasFlag(VehiclePassengerFlag::kEnterDirection) &&
         MatchesSeatSuffix(fourcc, pending_target_seat_index_);
}

bool VehiclePassengerC::AcceptsPendingExitAnimation(
    const std::uint32_t fourcc) const {
  if (!HasFlag(VehiclePassengerFlag::kPendingSeatChange) ||
      HasFlag(VehiclePassengerFlag::kTransitionInProgress) ||
      HasFlag(VehiclePassengerFlag::kEnterDirection) || owner_ == nullptr) {
    return false;
  }

  const auto current_seat = static_cast<std::uint8_t>(
      owner_->Movement().Data().GetTransportSeat());
  return MatchesSeatSuffix(fourcc, current_seat);
}

void VehiclePassengerC::HandleActivePlayerSeatChange(
    const WorldSession& session) {

  auto *player = session.objects().GetActivePlayer();
  if (player == nullptr) {
    return;
  }

  auto *passenger = player->Vehicle().GetVehiclePassengerComponent();
  if (passenger == nullptr) {
    return;
  }

  auto state = static_cast<std::uint32_t>(passenger->GetTransitionState());
  if (state == 0) {
    return;
  }

  auto *vehicle_unit = passenger->GetVehicleUnit();
  if (vehicle_unit == nullptr) {
    return;
  }

  const auto *vehicle_entry = vehicle_unit->Vehicle().GetVehicleEntry();
  if (vehicle_entry == nullptr) {
    return;
  }

  const auto seat_index = passenger->GetAltSeatIndex();
  if (seat_index >= 8) {
    return;
  }

  const auto *seat_entry =
      LookupVehicleSeatEntryForVehicleSeat(session, *vehicle_unit, seat_index);
  if (seat_entry == nullptr) {
    return;
  }

  state = static_cast<std::uint32_t>(passenger->GetTransitionState());
  if (state > 0) {
    const auto player_guid = player->GetGuid().GetRawValue();
    if (state <= 2) {

      FireVehicleSeatEvents(player_guid, vehicle_entry, seat_entry, 1);
    } else if (state == 3) {

      FireVehicleSeatEvents(player_guid, vehicle_entry, seat_entry, 3);
    }
  }
}

void VehiclePassengerC::Initialize() {
  if (s_initialized_)
    return;

  s_queued_transitions_.clear();
  s_queued_transitions_.reserve(32);
  s_active_passengers_.clear();
  s_registered_type_id_ =
      core::CObjectHeapList::Instance().RegisterType(
          0xD4u, 16u, "Vehicle Passenger", true);
  s_initialized_ = true;
}

void VehiclePassengerC::Shutdown() {
  if (!s_initialized_)
    return;

  s_queued_transitions_.clear();
  s_active_passengers_.clear();
  s_registered_type_id_ = 0;
  s_initialized_ = false;
}

void VehiclePassengerC::ProcessQueuedTransitions(WorldSession& session,
                                                 double timestamp) {
  if (s_queued_transitions_.empty()) {
    VehiclePassenger_ClearUpdateFlag();
    return;
  }

  auto entries = std::move(s_queued_transitions_);
  s_queued_transitions_.clear();

  for (const auto& entry : entries) {
    const auto owner_guid =
        (static_cast<std::uint64_t>(entry.guid_hi) << 32) | entry.guid_lo;
    auto* const owner = session.objects().GetMutableUnit(ObjectGuid(owner_guid));
    if (owner == nullptr) {
      continue;
    }

    auto* const passenger = owner->Vehicle().GetVehiclePassengerComponent();
    if (passenger == nullptr) {
      continue;
    }

    const auto target_guid =
        (static_cast<std::uint64_t>(entry.target_hi) << 32) | entry.target_lo;
    passenger->UpdateSeatState(session, timestamp, target_guid, entry.seat,
                               entry.use_transition != 0u);
  }

  VehiclePassenger_ClearUpdateFlag();
}

void VehiclePassengerC::QueueTransition(const VehiclePassengerQueueEntry &entry) {
  s_queued_transitions_.push_back(entry);
}

void VehiclePassengerC::UpdateAll(WorldSession& session,
                                  const std::uint32_t current_tick_ms) {

  auto snapshot = s_active_passengers_;
  for (auto *passenger : snapshot) {
    passenger->Update(session, static_cast<double>(current_tick_ms),
                      static_cast<int>(current_tick_ms));
  }
}

void VehiclePassengerC::RegisterActive() {
  if (std::find(s_active_passengers_.begin(), s_active_passengers_.end(), this) ==
      s_active_passengers_.end()) {
    s_active_passengers_.insert(s_active_passengers_.begin(), this);
  }
}

void VehiclePassengerC::UnregisterActive() {
  auto it = std::find(s_active_passengers_.begin(),
                      s_active_passengers_.end(), this);
  if (it != s_active_passengers_.end()) {
    s_active_passengers_.erase(it);
  }
}

void VehiclePassengerRescueTransition::Resize(std::uint32_t new_capacity) {
  if (new_capacity == capacity_)
    return;

  entries_.resize(new_capacity);
  if (count_ > new_capacity) {
    count_ = new_capacity;
  }
  capacity_ = new_capacity;
}

VehiclePassengerRescueEntry *VehiclePassengerRescueTransition::GetEntry(std::uint32_t index) {
  if (index >= count_)
    return nullptr;
  return &entries_[index];
}

const VehiclePassengerRescueEntry *
VehiclePassengerRescueTransition::GetEntry(std::uint32_t index) const {
  if (index >= count_)
    return nullptr;
  return &entries_[index];
}

bool VehiclePassengerC::PerFrameUpdate(
    WorldSession& session, CGUnit_C *vehicle_unit,
    const openwow::data::dbc::VehicleSeatEntry *seat_entry,
    int current_time) {
  auto state = static_cast<std::uint32_t>(transition_state_);
  Vec3 effective_position{};
  if (state != 0u) {
    UpdatePosition(vehicle_unit, seat_entry, current_time, effective_position);
  }

  state = static_cast<std::uint32_t>(transition_state_);
  if (state != 0u && state != 3u &&
      Distance(effective_position, saved_position) > 150.0f) {
    const auto terminal_state = state == 1u || state == 2u
                                    ? VehiclePassengerTransitionType::kAttached
                                    : VehiclePassengerTransitionType::kExit;
    const auto vehicle_guid =
        vehicle_unit != nullptr ? vehicle_unit->GetGuid().GetRawValue() : 0u;
    HandleTransition(session, static_cast<double>(current_time), terminal_state,
                     vehicle_guid,
                     terminal_state == VehiclePassengerTransitionType::kAttached
                         ? alt_seat_index_
                         : 0xFFu,
                     static_cast<std::uint32_t>(current_time));
    SetFlag(VehiclePassengerFlag::kActive);
  }

  previous_transition_state_ = transition_state_;
  if (vehicle_unit != nullptr && vehicle_unit->Vehicle().GetVehicleData() != nullptr) {
    vehicle::Vehicle_C_ComputeAvailableSeatMask(vehicle_unit->Vehicle().GetVehicleData());
  }

  return transition_state_ != VehiclePassengerTransitionType::kExit ||
         HasFlag(VehiclePassengerFlag::kPendingSeatChange);
}

bool VehiclePassengerC::CheckTransitionTimers(
    WorldSession& session, double timestamp, CGUnit_C *vehicle_unit,
    const openwow::data::dbc::VehicleSeatEntry *seat_entry, int current_time) {
  if (!HasFlag(VehiclePassengerFlag::kActive) &&
      !PerFrameUpdate(session, vehicle_unit, seat_entry, current_time)) {
    return false;
  }

  const auto vehicle_guid = vehicle_unit != nullptr ? vehicle_unit->GetGuid().GetRawValue() : 0ull;
  const auto now = static_cast<std::uint32_t>(current_time);
  switch (transition_state_) {
  case VehiclePassengerTransitionType::kEnterWithPos:
    if (HasReachedTick(now, transition_deadline_ms_)) {
      HandleTransition(session, timestamp, VehiclePassengerTransitionType::kTransferWithPos, vehicle_guid,
                       alt_seat_index_, current_time);
      if (!PerFrameUpdate(session, vehicle_unit, seat_entry, current_time)) {
        return false;
      }
    }
    break;
  case VehiclePassengerTransitionType::kTransferWithPos:
    if (!HasReachedTick(now, transition_deadline_ms_)) {
      UpdateInterpolatedFacing();
    } else {
      HandleTransition(session, timestamp, VehiclePassengerTransitionType::kAttached, vehicle_guid,
                       alt_seat_index_, current_time);
    }
    break;
  case VehiclePassengerTransitionType::kEnterCaptured:
    if (HasReachedTick(now, transition_deadline_ms_)) {
      HandleTransition(session, timestamp, VehiclePassengerTransitionType::kEject, vehicle_guid, 0xFFu,
                       current_time);
      if (!PerFrameUpdate(session, vehicle_unit, seat_entry, current_time)) {
        return false;
      }
    }
    break;
  case VehiclePassengerTransitionType::kEject:
    if (!HasReachedTick(now, transition_deadline_ms_)) {
      UpdateInterpolatedFacing();
    } else {
      HandleTransition(session, timestamp, VehiclePassengerTransitionType::kExit, vehicle_guid, 0xFFu,
                       current_time);
    }
    break;
  default:
    break;
  }

  if (HasFlag(VehiclePassengerFlag::kHasInputControl) &&
      HasReachedTick(now,
                     input_control_grace_started_at_ms_ +
                         kVehicleInputControlGraceMs)) {
    ClearFlag(VehiclePassengerFlag::kHasInputControl);
  }
  return true;
}

void VehiclePassengerC::UpdatePosition(
    CGUnit_C *vehicle_unit,
    const openwow::data::dbc::VehicleSeatEntry *seat_entry,
    int , Vec3 &effective_position) {
  SetFlag(VehiclePassengerFlag::kActive);
  if (owner_ == nullptr) {
    effective_position = {};
    return;
  }

  effective_position = ToVec3(owner_->GetPosition());
  if (seat_entry == nullptr) {
    return;
  }

  if (vehicle_unit != nullptr) {
    Vec3 seat_position{};
    ComputeSeatWorldPosition(vehicle_unit, seat_entry, seat_position);
    if (transition_state_ == VehiclePassengerTransitionType::kAttached) {
      position_offset = seat_position;
    }
    if (transition_state_ == VehiclePassengerTransitionType::kTransferWithPos) {
      effective_position = seat_position;
    }
  }

  if (transition_state_ == VehiclePassengerTransitionType::kAttached) {
    return;
  }

  transition_facing_current_ = owner_->GetOrientation();
  while (transition_facing_current_ + kVehiclePi < transition_facing_blended_) {
    transition_facing_current_ += kVehicleTau;
  }
  while (transition_facing_current_ - kVehiclePi > transition_facing_blended_) {
    transition_facing_current_ -= kVehicleTau;
  }

  RefreshTransitionDeadline(vehicle_unit, seat_entry);

  float gravity = 0.0f;
  float minimum_arc_height = 0.0f;
  float maximum_arc_height = 0.0f;
  if (transition_state_ == VehiclePassengerTransitionType::kTransferWithPos) {
    gravity = seat_entry->enter_gravity;
    minimum_arc_height = seat_entry->enter_min_arc_height;
    maximum_arc_height = seat_entry->enter_max_arc_height;
  } else if (transition_state_ == VehiclePassengerTransitionType::kEject) {
    gravity = seat_entry->exit_gravity;
    minimum_arc_height = seat_entry->exit_min_arc_height;
    maximum_arc_height = seat_entry->exit_max_arc_height;
  }

  acceleration_gravity_ = gravity;
  arc_height_ratio_ = 1.0f;
  const float duration_seconds =
      static_cast<float>(transition_deadline_ms_ - timing_param_) * 0.001f;
  const float natural_arc_height =
      gravity * 0.125f * duration_seconds * duration_seconds;
  if (duration_seconds > kVehicleTransitionEpsilon &&
      std::fabs(natural_arc_height) > kVehicleTransitionEpsilon) {
    if (natural_arc_height < minimum_arc_height) {
      arc_height_ratio_ = minimum_arc_height / natural_arc_height;
    } else if (natural_arc_height > maximum_arc_height) {
      arc_height_ratio_ = maximum_arc_height / natural_arc_height;
    }
  }
}

void VehiclePassengerC::Update(WorldSession& session, double timestamp,
                               int current_time) {
  (void)Vehicle_ProcessPendingSeatTimer(session, this);
  auto *const vehicle_unit = GetVehicleUnit();
  const auto *const seat_entry = GetSeatEntry();
  UpdateTransitionBlendFactor(current_time, seat_entry);
  if (CheckTransitionTimers(session, timestamp, vehicle_unit, seat_entry,
                            current_time)) {
    return;
  }

  DetachFromSeat();
  if (owner_ != nullptr) {
    UnitVehicle_ReleasePassengerForUnit(*owner_);
  }
}

void VehiclePassengerC::UpdateTransitionBlendFactor(
    const int current_time, const openwow::data::dbc::VehicleSeatEntry *seat_entry) {
  if (seat_entry == nullptr || !HasFlag(VehiclePassengerFlag::kActive)) {
    transition_blend_factor_ = 0.0f;
    return;
  }

  const auto duration_seconds =
      static_cast<float>(transition_deadline_ms_ - timing_param_) * 0.001f;
  if (duration_seconds < kVehicleTransitionEpsilon) {
    transition_blend_factor_ = 1.0f;
    return;
  }

  auto blend = static_cast<float>(current_time - static_cast<int>(timing_param_)) * 0.001f /
               duration_seconds;
  blend = std::clamp(blend, 0.0f, 1.0f);

  std::uint32_t ease_in_flag = 0;
  std::uint32_t ease_out_flag = 0;
  if (transition_state_ == VehiclePassengerTransitionType::kTransferWithPos) {
    ease_in_flag = kSeatFlagEnterEaseIn;
    ease_out_flag = kSeatFlagEnterEaseOut;
  } else if (transition_state_ == VehiclePassengerTransitionType::kEject) {
    ease_in_flag = kSeatFlagExitEaseIn;
    ease_out_flag = kSeatFlagExitEaseOut;
  } else {
    transition_blend_factor_ = blend;
    return;
  }

  if ((seat_entry->flags & ease_in_flag) != 0u && (seat_entry->flags & ease_out_flag) != 0u) {
    transition_blend_factor_ = 0.5f - std::cos(blend * kVehiclePi) * 0.5f;
  } else if ((seat_entry->flags & ease_in_flag) != 0u) {
    transition_blend_factor_ = blend * blend;
  } else if ((seat_entry->flags & ease_out_flag) != 0u) {
    const auto inverse = 1.0f - blend;
    transition_blend_factor_ = 1.0f - inverse * inverse;
  } else {
    transition_blend_factor_ = blend;
  }
}

void VehiclePassengerC::RefreshTransitionDeadline(
    CGUnit_C *vehicle_unit, const openwow::data::dbc::VehicleSeatEntry *seat_entry) {
  if (seat_entry == nullptr || owner_ == nullptr) {
    transition_deadline_ms_ = timing_param_;
    return;
  }

  switch (transition_state_) {
  case VehiclePassengerTransitionType::kEnterWithPos:
    transition_deadline_ms_ =
        timing_param_ + DurationSecondsToMs(std::min(seat_entry->enter_pre_delay,
                                                     kVehicleTransitionMaxDurationSeconds));
    return;
  case VehiclePassengerTransitionType::kEnterCaptured:
    transition_deadline_ms_ =
        timing_param_ + DurationSecondsToMs(std::min(seat_entry->exit_pre_delay,
                                                     kVehicleTransitionMaxDurationSeconds));
    return;
  case VehiclePassengerTransitionType::kTransferWithPos: {
    const auto vehicle_position =
        vehicle_unit != nullptr ? ToVec3(vehicle_unit->GetPosition()) : saved_position;
    const Vec3 seat_offset{seat_entry->attachment_offset_x, seat_entry->attachment_offset_y,
                           seat_entry->attachment_offset_z};
    const auto target_position = vehicle_position + seat_offset;
    const auto distance = Distance(target_position, saved_position);
    if (distance <= kVehicleTransitionEpsilon) {
      transition_deadline_ms_ = timing_param_;
      return;
    }

    auto effective_speed = seat_entry->enter_speed;
    const auto owner_distance = Distance(ToVec3(owner_->GetPosition()), saved_position);
    if (effective_speed > kVehicleTransitionEpsilon) {
      effective_speed -= std::min(owner_distance, effective_speed);
    }

    const auto unclamped_duration =
        effective_speed <= kVehicleTransitionEpsilon ? seat_entry->enter_max_duration
                                                     : distance / effective_speed;
    transition_deadline_ms_ =
        timing_param_ +
        DurationSecondsToMs(ClampTransitionDuration(unclamped_duration,
                                                    seat_entry->enter_min_duration,
                                                    seat_entry->enter_max_duration));
    return;
  }
  case VehiclePassengerTransitionType::kEject: {
    const auto vehicle_position =
        vehicle_unit != nullptr ? ToVec3(vehicle_unit->GetPosition()) : saved_position;
    const auto distance = Distance(vehicle_position, saved_position);
    const auto unclamped_duration =
        seat_entry->exit_speed <= kVehicleTransitionEpsilon ? seat_entry->exit_max_duration
                                                            : distance / seat_entry->exit_speed;
    transition_deadline_ms_ =
        timing_param_ +
        DurationSecondsToMs(ClampTransitionDuration(unclamped_duration,
                                                    seat_entry->exit_min_duration,
                                                    seat_entry->exit_max_duration));
    return;
  }
  default:
    transition_deadline_ms_ = timing_param_;
    return;
  }
}

void VehiclePassengerC::UpdateInterpolatedFacing() {
  if (owner_ == nullptr) {
    return;
  }

  transition_facing_current_ = owner_->GetOrientation();
  while (transition_facing_current_ + kVehiclePi < transition_facing_blended_) {
    transition_facing_current_ += kVehicleTau;
  }
  while (transition_facing_current_ - kVehiclePi > transition_facing_blended_) {
    transition_facing_current_ -= kVehicleTau;
  }
  transition_facing_blended_ =
      (1.0f - transition_blend_factor_) * transition_facing_from_ +
      transition_facing_current_ * transition_blend_factor_;
}

void VehiclePassengerC::CascadeVehicleGuid(std::uint64_t guid) {
  rescue_vehicle_guid_ = guid;

  if (owner_ == nullptr) {
    return;
  }

  const auto recursive_guid = guid != 0 ? guid : owner_->GetGuid().GetRawValue();
  if (recursive_guid == 0) {
    return;
  }

  vehicle::Vehicle_C_ForEachPassengerUnit(owner_->Vehicle().GetVehicleData(),
                                              [&](CGUnit_C& passenger_unit) {
                                                auto* const passenger =
                                                    passenger_unit.Vehicle().GetVehiclePassengerComponent();
                                                if (passenger == nullptr) {
                                                  return;
                                                }

                                                passenger->CascadeVehicleGuid(recursive_guid);
                                              });
}

void VehiclePassengerC::RenderAttachment() {
  const auto state = transition_state_;
  if ((state == VehiclePassengerTransitionType::kTransferWithPos ||
       state == VehiclePassengerTransitionType::kEject) &&
      !HasFlag(VehiclePassengerFlag::kActive)) {
    return;
  }

  if (owner_ == nullptr) {
    return;
  }

  const auto alt_lo = static_cast<std::uint32_t>(alt_vehicle_guid_);
  const auto alt_hi = static_cast<std::uint32_t>(alt_vehicle_guid_ >> 32);
  CGUnit_C *vehicle_unit = nullptr;
  if ((alt_hi | alt_lo) != 0) {
    vehicle_unit = ResolveVehicleUnitByGuid(owner_, alt_vehicle_guid_);
  } else {
    vehicle_unit = ResolveVehicleUnitByGuid(owner_, primary_vehicle_guid_);
  }

  const auto *const seat = seat_entry_;

  ClearFlag(VehiclePassengerFlag::kModelHidden);

  if (state == VehiclePassengerTransitionType::kExit) {

    const Vec3 up_vector{0.0f, 0.0f, 1.0f};

    const float facing = owner_->GetFacing();
    const float scale = owner_->GetScale();
    const auto pos = owner_->GetPosition();

    StorePassengerVisualTransform(*owner_, ToVec3(pos), facing, scale, up_vector);
    return;
  }

  Vec3 render_position;
  if (state == VehiclePassengerTransitionType::kAttached ||
      prev_vehicle_guid_ == 0) {
    render_position = position_offset;
  } else {
    render_position = GetPassengerPosition(seat_attachment_offset_,
                                           position_offset);
  }

  Vec3 out_position{};
  Vec3 out_facing_offset{};
  float out_facing = 0.0f;
  float out_scale = 0.0f;
  bool use_bone_matrix = false;
  float bone_matrix[16]{};

  if (seat == nullptr || state == VehiclePassengerTransitionType::kExit) {
    const auto owner_pos = owner_->GetPosition();
    out_position = ToVec3(owner_pos);

    out_scale = owner_->GetScale();
    out_facing = owner_->GetFacing();

    out_facing_offset = {0.0f, 0.0f, 1.0f};
    goto apply_transform;
  }

  if (IsExiting()) {

    float vertical_offset = 0.0f;

    if (std::fabs(acceleration_gravity_) > kVehicleTransitionEpsilon) {
      const auto elapsed_ticks =
          static_cast<std::int32_t>(transition_deadline_ms_ - timing_param_);
      const float elapsed_sec = static_cast<float>(elapsed_ticks) * 0.001f;

      const float velocity = transition_blend_factor_ * elapsed_sec;

      const float parabolic = acceleration_gravity_ * velocity * 0.5f;

      vertical_offset =
          (elapsed_sec * parabolic - velocity * parabolic) * arc_height_ratio_;
    }

    Vec3 exit_world_pos = ToVec3(owner_->GetPosition());

    const float blend = transition_blend_factor_;
    const float inv_blend = 1.0f - blend;

    render_position.x = exit_world_pos.x * blend + render_position.x * inv_blend;
    render_position.y = exit_world_pos.y * blend + render_position.y * inv_blend;
    const float blended_z = blend * exit_world_pos.z + inv_blend * render_position.z;
    render_position.z = blended_z + vertical_offset;

    out_position = render_position;

    out_facing = prev_facing_;
    out_scale = owner_->GetScale();

    out_facing_offset = {0.0f, 0.0f, 1.0f};
    goto apply_transform;
  }

  if (state == VehiclePassengerTransitionType::kAttached) {
    if (vehicle_unit == nullptr) {
      return;
    }

    if (!HasFlag(VehiclePassengerFlag::kBlendAnims)) {
      UpdateBoneAttachmentOffset(seat);
    }

    if (seat != nullptr && seat->attachment_id >= 0) {
      const std::int32_t bone_lookup = ResolveVehicleSeatM2AttachmentLookup(seat->attachment_id);
      if (bone_lookup >= 0 &&
          QueryUnitM2AttachmentTransform(*vehicle_unit, static_cast<std::uint32_t>(bone_lookup),
                                         bone_matrix)) {
        Vec3 local_position{
            seat->attachment_offset_x,
            seat->attachment_offset_y,
            seat->attachment_offset_z,
        };
        if (HasFlag(VehiclePassengerFlag::kHasBoneOffset)) {
          local_position = local_position + cached_bone_offset_;
        }
        TransformVec3ByRowMajorAffine(bone_matrix, local_position);
        bone_matrix[12] = local_position.x;
        bone_matrix[13] = local_position.y;
        bone_matrix[14] = local_position.z;
        use_bone_matrix = true;
      }
    }

    if (use_bone_matrix) {
      goto apply_transform;
    }

    ComputeSeatWorldPosition(vehicle_unit, seat, out_position);
    out_scale = owner_->GetScale();
    out_facing = owner_->GetFacing();

    out_facing_offset = {0.0f, 0.0f, 1.0f};
    goto apply_transform;
  }

  {
    if (!HasFlag(VehiclePassengerFlag::kBlendAnims)) {
      UpdateBoneAttachmentOffset(seat);
    }

    if (primary_vehicle_guid_ != 0) {
      auto *const parent_vehicle =
          ResolveVehicleUnitByGuid(owner_, primary_vehicle_guid_);
      if (parent_vehicle != nullptr) {
        if (seat != nullptr) {
          ComputeSeatWorldPosition(parent_vehicle, seat, out_position);
          out_facing = parent_vehicle->GetFacing();
          out_scale = owner_->GetScale();
          out_facing_offset = {0.0f, 0.0f, 1.0f};
          goto apply_transform;
        }
      }
    }

    out_facing = facing_for_render_;
    out_position = render_position;
    out_scale = owner_->GetScale();

    out_facing_offset = {0.0f, 0.0f, 1.0f};
  }

apply_transform:
  if (use_bone_matrix) {
    owner_->SetVisualModelWorldTransform(bone_matrix);
  } else {
    StorePassengerVisualTransform(*owner_, out_position, out_facing, out_scale,
                                  out_facing_offset);
  }

  if ((owner_->Movement().UpdateFlags() & 0x40000u) != 0u) {
    SetFlag(VehiclePassengerFlag::kModelHidden);
  } else {
    ClearFlag(VehiclePassengerFlag::kModelHidden);
  }
}

}
