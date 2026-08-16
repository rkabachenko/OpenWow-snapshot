
#include "openwow/game/unit_vehicle.h"
#include "openwow/game/player_control_runtime.h"

#include "openwow/runtime/time/game_clock.h"
#include "openwow/data/formats/dbc/dbc_entries_world.h"
#include "openwow/game/c_input_control.h"
#include "openwow/game/group_system.h"
#include "openwow/game/movement_callbacks.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/objects/unit/unit_vehicle.h"
#include "openwow/game/vehicle_helpers.h"
#include "openwow/game/vehicle.h"
#include "openwow/game/vehicle_passenger.h"
#include "openwow/game/vehicle_runtime_layout.h"
#include "openwow/game/world_session.h"
#include "openwow/render/api/math/render_matrix_math.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/world/camera/world_camera.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace openwow::game {

namespace {

constexpr std::uint32_t kVehicleExitFallbackSpellId = 117440515u;
constexpr std::uint8_t kVehiclePrevSeatDirection = 0xFFu;
constexpr std::uint8_t kVehicleNextSeatDirection = 0x01u;

constexpr std::uint32_t kSpellStateFlagOnVehicle = 0x80000u;

std::unordered_map<std::uintptr_t, CGUnit_C *> &VehicleDataOwnerRegistry() {
  static std::unordered_map<std::uintptr_t, CGUnit_C *> registry;
  return registry;
}

VehiclePassengerC* AllocatePassengerForUnit(CGUnit_C& unit) {
  return unit.Vehicle().CreateVehiclePassenger(unit);
}

void ReleasePassengerForUnit(CGUnit_C& unit) {
  unit.Vehicle().ReleaseVehiclePassenger(unit);
}

[[nodiscard]] const CGUnit_C* WalkPassengerAltVehicleChain(
    const CGUnit_C& unit,
    const CGUnit_C* stop_at) {
  const auto* const objects = unit.object_manager();
  if (objects == nullptr) {
    return nullptr;
  }

  const auto* passenger = unit.Vehicle().GetVehiclePassengerComponent();
  const std::uint64_t initial_guid =
      passenger != nullptr ? passenger->GetAltVehicleGuid() : 0u;

  if (initial_guid == 0u) {
    return unit.Vehicle().GetVehicleEntry() != nullptr ? &unit : nullptr;
  }

  const CGUnit_C* walker = objects->GetUnit(ObjectGuid(initial_guid));
  if (walker == nullptr) {
    return nullptr;
  }

  std::unordered_set<const CGUnit_C*> visited;
  while (walker != stop_at) {
    if (!visited.insert(walker).second) {
      return nullptr;
    }

    const auto* chain_passenger = walker->Vehicle().GetVehiclePassengerComponent();
    const std::uint64_t next_guid =
        chain_passenger != nullptr ? chain_passenger->GetAltVehicleGuid() : 0u;
    if (next_guid == 0u) {
      return walker;
    }

    walker = objects->GetUnit(ObjectGuid(next_guid));
    if (walker == nullptr) {
      return nullptr;
    }
  }

  return walker;
}

[[nodiscard]] bool PassengerAltVehicleChainContainsUnit(const CGUnit_C& root,
                                                        const CGUnit_C& unit) {
  return WalkPassengerAltVehicleChain(root, &unit) == &unit;
}

void TrySetupVehicleCameraFollowLogic(CGUnit_C& unit,
                                      WorldSession& session) {
  const auto* passenger = unit.Vehicle().GetVehiclePassengerComponent();
  if (passenger == nullptr) {
    return;
  }

  if (passenger->GetTransitionState() == VehiclePassengerTransitionType::kExit) {
    return;
  }

  auto* const active_camera = session.world_camera();
  if (active_camera == nullptr) {
    return;
  }

  if (!UnitVehicle_ShouldSetupVehicleCameraForActiveCamera(
          unit, unit.object_manager() != nullptr
                    ? unit.object_manager()->GetActivePlayer()
                    : nullptr,
          ObjectGuid{active_camera->bound_object()}, nullptr)) {
    return;
  }

  active_camera->SetBoundObject(unit.GetGuid().GetRawValue());

  const auto camera_target_position = unit.GetPosition();
  active_camera->SetTarget(camera_target_position.x, camera_target_position.y,
                           camera_target_position.z);
}

[[nodiscard]] bool IsValidUnitGuidForVehicle(std::uint64_t guid) {
  const auto hi = static_cast<std::uint32_t>(guid >> 32);
  const auto lo = static_cast<std::uint32_t>(guid & 0xFFFFFFFFu);

  if ((hi & 0xF0F00000u) == 0xF0500000u) {
    return true;
  }

  if ((hi & 0xF0000000u) == 0 && ((hi & 0xF07FFFFFu) | lo) != 0) {
    return true;
  }

  return false;
}

[[nodiscard]] bool HasVehiclePassengerTransitionOrPendingSeatChange(
    const VehiclePassengerC* passenger) {
  if (passenger == nullptr) {
    return false;
  }

  const auto transition_state = passenger->GetTransitionState();
  if (transition_state != VehiclePassengerTransitionType::kExit &&
      transition_state != VehiclePassengerTransitionType::kAttached) {
    return true;
  }

  return passenger->HasFlag(VehiclePassengerFlag::kPendingSeatChange);
}

[[nodiscard]] bool HasPassengerInputControlAttachment(
    const VehiclePassengerC* passenger) {
  return passenger != nullptr &&
         passenger->GetTransitionState() !=
             VehiclePassengerTransitionType::kExit &&
         passenger->HasFlag(VehiclePassengerFlag::kHasInputControl);
}

void FirePlayerVehicleDataEvent(const CGUnit_C& unit,
                                const char* event_name,
                                const std::uint32_t vehicle_ui_indicator_id) {
  if (unit.GetGuid() != CGObject_C::GetActivePlayerGuid()) {
    return;
  }

  auto& unit_tokens = ui::game::UnitTokenRegistry::Get();
  const auto tokens = unit_tokens.AllTokensForGuid(unit.GetGuid().GetRawValue());
  if (tokens.empty()) {
    return;
  }

  auto& dispatch = ui::game::ScriptEventDispatch::Get();
  for (const auto& token : tokens) {
    dispatch.FireEventArgs(event_name, {token, static_cast<int>(vehicle_ui_indicator_id)});
  }
}

void FirePlayerVehicleDataLostEvent(const CGUnit_C& unit) {
  if (unit.GetGuid() != CGObject_C::GetActivePlayerGuid()) {
    return;
  }

  auto& unit_tokens = ui::game::UnitTokenRegistry::Get();
  const auto tokens = unit_tokens.AllTokensForGuid(unit.GetGuid().GetRawValue());
  if (tokens.empty()) {
    return;
  }

  auto& dispatch = ui::game::ScriptEventDispatch::Get();
  for (const auto& token : tokens) {
    dispatch.FireEventArgs(ui::game::events::PLAYER_LOSES_VEHICLE_DATA, {token});
  }
}

[[nodiscard]] bool IsActivePlayerVehicleMover(const CGUnit_C& mover_unit,
                                              const CGUnit_C& active_player,
                                              const PlayerControlRuntime& player_control) {
  if (mover_unit.GetGuid() !=
      player_control.ActiveMoverGuid()) {
    return false;
  }

  const auto* const passenger = active_player.Vehicle().GetVehiclePassengerComponent();
  return passenger != nullptr &&
         passenger->GetVehicleUnitGuid() == mover_unit.GetGuid().GetRawValue();
}

void PrimeActivePlayerVehicleInputControl(CGUnit_C& active_player,
                                          const std::uint32_t tick_count) {
  auto* const passenger = active_player.Vehicle().GetVehiclePassengerComponent();
  if (passenger == nullptr ||
      passenger->GetTransitionState() != VehiclePassengerTransitionType::kAttached) {
    return;
  }

  if (auto* const input = GetInputControlSingleton(); input != nullptr) {
    input->ReapplyDirectionalControlState(tick_count);
  }

  passenger->SetFlag(VehiclePassengerFlag::kHasInputControl);
  passenger->SetInputControlGraceStartedAtMs(tick_count);
}

[[nodiscard]] bool TryHandleLocalVehicleExit(CGUnit_C& mover_unit,
                                             CGUnit_C& active_player,
                                             const WorldSession& session,
                                             const std::uint32_t tick_count) {
  const auto* const seat_entry = ResolveUnitVehicleSeatEntry(session, active_player);
  if (seat_entry == nullptr ||
      (seat_entry->flags & VehicleControlSeatFlag::kCanExit) == 0u) {
    return false;
  }

  if (active_player.Vehicle().GetVehiclePassengerComponent() == nullptr) {
    return false;
  }

  PrimeActivePlayerVehicleInputControl(active_player, tick_count);
  Vehicle_ForwardDismount(&mover_unit, static_cast<int>(tick_count));
  return true;
}

bool TryHandleLocalVehicleSeatSwitch(CGUnit_C& mover_unit,
                                     CGUnit_C& active_player,
                                     const std::uint32_t tick_count,
                                     const std::uint8_t seat_direction) {
  PrimeActivePlayerVehicleInputControl(active_player, tick_count);
  Vehicle_ForwardSeatSwitch(&mover_unit, static_cast<int>(tick_count), 0, 0,
                            seat_direction);
  return true;
}

}

UnitVehicleComponent::UnitVehicleComponent() = default;

UnitVehicleComponent::~UnitVehicleComponent() = default;

void UnitVehicleComponent::Cleanup(CGUnit_C &owner) noexcept {
  ReleaseVehiclePassenger(owner);
  if (data_ != nullptr) {
    auto &registry = VehicleDataOwnerRegistry();
    const auto existing =
        registry.find(reinterpret_cast<std::uintptr_t>(data_));
    if (existing != registry.end() && existing->second == &owner) {
      registry.erase(existing);
    }
  }
  data_ = nullptr;
  owned_data_.reset();
}

void *UnitVehicleComponent::GetVehicleData() const noexcept {
  return data_;
}

void UnitVehicleComponent::SetVehicleData(CGUnit_C &owner,
                                          WorldSession &session,
                                          void *vehicle_data) {
  if (data_ != nullptr && data_ != vehicle_data) {
    vehicle::Vehicle_C_ResetSeatAnimations(session, data_);
  }
  if (data_ != nullptr) {
    auto &registry = VehicleDataOwnerRegistry();
    const auto existing =
        registry.find(reinterpret_cast<std::uintptr_t>(data_));
    if (existing != registry.end() && existing->second == &owner) {
      registry.erase(existing);
    }
  }
  if (owned_data_ != nullptr && vehicle_data != owned_data_.get()) {
    owned_data_.reset();
  }
  data_ = vehicle_data;
  if (data_ != nullptr) {
    VehicleDataOwnerRegistry()[reinterpret_cast<std::uintptr_t>(data_)] = &owner;
  }
}

void UnitVehicleComponent::SetOwnedVehicleData(
    CGUnit_C &owner, WorldSession &session,
    std::unique_ptr<std::uint8_t[]> vehicle_data) {
  SetVehicleData(owner, session, vehicle_data.get());
  owned_data_ = std::move(vehicle_data);
}

void UnitVehicleComponent::ResetOwnedVehicleData(CGUnit_C &owner,
                                                 WorldSession &session) {
  if (owned_data_ == nullptr) {
    return;
  }
  if (data_ == owned_data_.get()) {
    SetVehicleData(owner, session, nullptr);
    return;
  }
  owned_data_.reset();
}

bool UnitVehicleComponent::OwnsVehicleData(const void *vehicle_data) const noexcept {
  return owned_data_.get() == vehicle_data;
}

CGUnit_C *UnitVehicleComponent::ResolveVehicleDataOwner(const void *vehicle_data) {
  if (vehicle_data == nullptr) {
    return nullptr;
  }
  const auto &registry = VehicleDataOwnerRegistry();
  const auto existing =
      registry.find(reinterpret_cast<std::uintptr_t>(vehicle_data));
  return existing != registry.end() ? existing->second : nullptr;
}

CGUnit_C *UnitVehicleComponent::GetVehicleUnit() const {
  auto *const passenger = GetVehiclePassengerComponent();
  return passenger != nullptr ? passenger->GetVehicleUnit() : nullptr;
}

bool UnitVehicleComponent::HasValidVehicleUnitGuid() const {
  const auto *const passenger = GetVehiclePassengerComponent();
  return passenger != nullptr &&
         HasVehicleTransitionTargetGuid(passenger->GetVehicleUnitGuid());
}

CGUnit_C *UnitVehicleComponent::GetVehicleObject(const CGUnit_C &owner) const {
  auto *const passenger = GetVehiclePassengerComponent();
  return passenger != nullptr
             ? passenger->GetVehicleObject()
             : static_cast<CGUnit_C *>(UnitVehicle_FindRootVehicle(
                   const_cast<CGUnit_C *>(&owner), nullptr));
}

const data::dbc::VehicleEntry *UnitVehicleComponent::GetVehicleEntry() const {
  return vehicle_runtime_layout::ResolveVehicleEntryPointerField(
      GetVehicleData());
}

bool UnitVehicleComponent::HasAttachedVehiclePassenger() const {
  const auto *const passenger = GetVehiclePassengerComponent();
  return passenger != nullptr && passenger->IsAttachedToVehicle();
}

bool UnitVehicleComponent::IsUsingVehicle() const {
  const auto *const passenger = GetVehiclePassengerComponent();
  return passenger != nullptr &&
         (passenger->GetPrimaryVehicleGuid() != 0u ||
          passenger->GetAltVehicleGuid() != 0u);
}

VehiclePassengerC *UnitVehicleComponent::GetVehiclePassengerComponent() const noexcept {
  return passenger_;
}

void UnitVehicleComponent::SetVehiclePassengerComponent(
    CGUnit_C &owner, VehiclePassengerC *passenger) {
  auto *const current = GetVehiclePassengerComponent();
  if (current == passenger) {
    if (passenger != nullptr) {
      passenger->BindOwner(&owner);
    }
    return;
  }
  if (current != nullptr && current->GetOwner() == &owner) {
    current->BindOwner(nullptr);
  }
  passenger_ = passenger;
  if (passenger != nullptr) {
    passenger->BindOwner(&owner);
  }
}

VehiclePassengerC *
UnitVehicleComponent::CreateVehiclePassenger(CGUnit_C &owner) {
  if (owned_passenger_ != nullptr) {
    owned_passenger_->RegisterActive();
    SetVehiclePassengerComponent(owner, owned_passenger_.get());
    return owned_passenger_.get();
  }
  owned_passenger_ = std::make_unique<VehiclePassengerC>();
  owned_passenger_->ResetPositionVectors();
  SetVehiclePassengerComponent(owner, owned_passenger_.get());
  return owned_passenger_.get();
}

void UnitVehicleComponent::ReleaseVehiclePassenger(CGUnit_C &owner) {
  SetVehiclePassengerComponent(owner, nullptr);
  owned_passenger_.reset();
}

ObjectGuid
UnitVehicleComponent::ResolveCameraTargetGuid(const CGUnit_C &owner) const {
  return owner.GetTransportGUID();
}

const data::dbc::VehicleSeatEntry *
UnitVehicleComponent::GetVehiclePassengerSeatEntry() const {
  const auto *const passenger = GetVehiclePassengerComponent();
  return passenger != nullptr ? passenger->GetSeatEntry() : nullptr;
}

bool UnitVehicleComponent::HasVehicleSeatMovementBlock(
    const CGUnit_C &owner) const {
  const auto *const seat = GetVehiclePassengerSeatEntry();
  if (seat == nullptr) {
    return false;
  }
  const bool is_swimming_or_flying =
      (owner.GetMovementInfo().flags & (kMoveFlagSwimming | kMoveFlagFlying)) !=
      0u;
  constexpr std::uint32_t kSeatBlockGround = 0x80u;
  constexpr std::uint32_t kSeatBlockAir = 0x100u;
  return (seat->flags &
          (is_swimming_or_flying ? kSeatBlockAir : kSeatBlockGround)) != 0u;
}

bool UnitVehicleComponent::VehicleSuppressesTransitionAnimation(
    const CGUnit_C &owner) const {
  const auto *const vehicle_data =
      static_cast<const std::uint32_t *>(GetVehicleData());
  if (vehicle_data != nullptr && (vehicle_data[22] & 0x04000000u) != 0u) {
    return true;
  }
  const auto *const vehicle_entry = GetVehicleEntry();
  return vehicle_entry != nullptr &&
         (vehicle_entry->flags & 0x00010000u) != 0u &&
           ((owner.Animation().GetEmoteInternalFlags() & 0x400u) != 0u ||
            owner.Animation().HasSpellVisualAnimationLatch());
}

const data::dbc::VehicleSeatEntry *
UnitVehicleComponent::ResolveAttachedVehicleSeatEntry(
    const CGUnit_C &owner, const WorldSession &session) const {
  const auto *const passenger = GetVehiclePassengerComponent();
  if (passenger == nullptr ||
      passenger->GetTransitionState() !=
          VehiclePassengerTransitionType::kAttached ||
      owner.State().GetHealth() == 0u) {
    return nullptr;
  }
  if (const auto *const seat = GetVehiclePassengerSeatEntry(); seat != nullptr) {
    return seat;
  }
  return ResolveUnitVehicleSeatEntry(session, owner);
}

bool UnitVehicleComponent::VehicleSeatOwnsEmoteState(
    const CGUnit_C &owner, const WorldSession &session,
    const std::uint32_t emote_state) const {
  const auto *const passenger = GetVehiclePassengerComponent();
  const auto *const seat = ResolveAttachedVehicleSeatEntry(owner, session);
  if (passenger == nullptr || seat == nullptr) {
    return false;
  }
  const bool use_loop_animation =
      (passenger->GetFlags() & VehiclePassengerFlag::kPositionDirty) != 0u;
  if ((seat->flags & 0x2u) != 0u) {
    const auto selected_animation =
        !use_loop_animation && seat->ride_anim_start != -1
            ? seat->ride_anim_start
            : seat->ride_anim_loop;
    if (selected_animation != -1) {
      return static_cast<std::int32_t>(emote_state) == seat->ride_anim_start ||
             static_cast<std::int32_t>(emote_state) == seat->ride_anim_loop;
    }
  }
  return (seat->flags & 0x4u) != 0u &&
         (static_cast<std::int32_t>(emote_state) ==
              seat->ride_upper_anim_start ||
          static_cast<std::int32_t>(emote_state) == seat->ride_upper_anim_loop);
}

float UnitVehicleComponent::GetVehicleChainWorldFacing(
    const CGUnit_C &owner) const {
  float total = 0.0f;
  const CGUnit_C *unit = &owner;
  while (unit != nullptr) {
    total += unit->Movement().BodyFacing();
    if (!unit->Vehicle().HasValidVehicleUnitGuid()) {
      break;
    }
    const auto *const objects = owner.object_manager();
    const auto *const vehicle =
        objects != nullptr ? objects->GetUnit(unit->GetTransportGUID()) : nullptr;
    if (vehicle == nullptr) {
      break;
    }
    unit = vehicle;
  }
  const auto *const objects = owner.object_manager();
  return objects == nullptr
             ? total
             : Movement_TransformLocalFacingToWorld(
                   *objects, unit->GetTransportGUID().GetRawValue(), total);
}

UnitVehicleComponent::LocalTransformFrame
UnitVehicleComponent::GetLocalTransformFrame(const CGUnit_C &owner) const {
  LocalTransformFrame frame{};
  const auto &update = owner.GetMovementUpdate();
  frame.x = update.GetX();
  frame.y = update.GetY();
  frame.z = update.GetZ();
  frame.facing = owner.Movement().BodyFacing();
  if (update.IsLiving() && !update.movement.transport.guid.IsEmpty()) {
    frame.parent_guid = update.movement.transport.guid;
    frame.x = update.movement.transport.offset_x;
    frame.y = update.movement.transport.offset_y;
    frame.z = update.movement.transport.offset_z;
  } else if (update.HasUpdateFlag(kUpdateFlagPosition) &&
             !update.transport_guid.IsEmpty()) {
    frame.parent_guid = update.transport_guid;
    frame.x = update.transport_offset_x;
    frame.y = update.transport_offset_y;
    frame.z = update.transport_offset_z;
  }
  return frame;
}

void UnitVehicleComponent::BuildWorldMatrixWithVehicle(
    const CGUnit_C &owner, float *out_matrix) const {
  if (out_matrix == nullptr) {
    return;
  }
  const auto *const objects = owner.object_manager();
  if (auto *const data = GetVehicleData();
      data != nullptr && GetVehicleEntry() != nullptr && objects != nullptr) {
    vehicle::Vehicle_C_UpdateTransformFromParent(*objects, data);
    if (vehicle::Vehicle_C_TryCopyTransformMatrix(data, out_matrix)) {
      return;
    }
  }
  const auto frame = GetLocalTransformFrame(owner);
  auto world = render::BuildRotationMatrix4x4Z(frame.facing);
  world[12] = frame.x;
  world[13] = frame.y;
  world[14] = frame.z;
  if (frame.parent_guid.IsEmpty() || objects == nullptr) {
    std::copy(world.begin(), world.end(), out_matrix);
    return;
  }
  render::RenderMatrix4x4 parent{render::kRenderIdentityMatrix4x4};
  if (const auto *const parent_unit = objects->GetUnit(frame.parent_guid);
      parent_unit != nullptr) {
    parent_unit->Vehicle().BuildWorldMatrixWithVehicle(*parent_unit,
                                                       parent.data());
  } else {
    Movement_GetObjectTransform(*objects, frame.parent_guid.GetRawValue(),
                                parent.data());
  }
  world = render::MultiplyMatrix4x4(world, parent);
  std::copy(world.begin(), world.end(), out_matrix);
}

void UnitVehicleComponent::BuildPositionWithVehicle(
    const CGUnit_C &owner, float *out_position) const {
  if (out_position == nullptr) {
    return;
  }
  float world[16];
  BuildWorldMatrixWithVehicle(owner, world);
  out_position[0] = world[12];
  out_position[1] = world[13];
  out_position[2] = world[14];
}

void UnitVehicle_ReleasePassengerForUnit(CGUnit_C& unit) {
  ReleasePassengerForUnit(unit);
}

bool UnitVehicle_ShouldSetupVehicleCameraForActiveCamera(
    const CGUnit_C& unit,
    const CGUnit_C* active_player,
    const ObjectGuid active_camera_bound_guid,
    const CGUnit_C* active_camera_vehicle_owner) {
  if (unit.IsActivePlayer()) {
    return true;
  }

  if (!active_camera_bound_guid.IsEmpty() &&
      active_camera_bound_guid == unit.GetGuid()) {
    return true;
  }

  if (active_player != nullptr &&
      PassengerAltVehicleChainContainsUnit(*active_player, unit)) {
    return true;
  }

  if (active_camera_vehicle_owner == nullptr ||
      active_camera_vehicle_owner == active_player) {
    return false;
  }

  return PassengerAltVehicleChainContainsUnit(*active_camera_vehicle_owner, unit);
}

void Unit_SetVehicleSeatTransferPacketBit(void* unit, const int enabled) {
  if (unit == nullptr) {
    return;
  }

  auto* cg_unit = static_cast<CGUnit_C*>(unit);
  cg_unit->Movement().Data().SetVehicleSeatTransferPacketBit(enabled != 0);
}

void UnitVehicle_EnsureVehicleData(WorldSession& session, CGUnit_C& unit,
                                   const std::uint32_t vehicle_record_id) {
  if (auto* const vehicle_data = unit.Vehicle().GetVehicleData(); vehicle_data != nullptr) {
    vehicle::Vehicle_C_UpdateVehicleEntry(vehicle_data, vehicle_record_id);
    return;
  }

  auto owned_vehicle_data = std::unique_ptr<std::uint8_t[]>(
      static_cast<std::uint8_t*>(
          vehicle::Vehicle_C_CreateRuntimeData(unit, vehicle_record_id)));
  unit.Vehicle().SetOwnedVehicleData(unit, session,
                                     std::move(owned_vehicle_data));

  const auto* const vehicle_entry = unit.Vehicle().GetVehicleEntry();
  const auto vehicle_ui_indicator_id =
      vehicle_entry != nullptr ? vehicle_entry->vehicle_ui_indicator_id : 0u;
  FirePlayerVehicleDataEvent(unit, ui::game::events::PLAYER_GAINS_VEHICLE_DATA,
                             vehicle_ui_indicator_id);
}

void UnitVehicle_ClearVehicleData(WorldSession& session, CGUnit_C& unit,
                                  const bool detach_passengers) {
  auto* const vehicle_data = unit.Vehicle().GetVehicleData();
  if (vehicle_data == nullptr) {
    return;
  }

  if (detach_passengers) {
    vehicle::Vehicle_C_DetachAllPassengers(session, vehicle_data);
  }

  vehicle::Vehicle_C_ResetSeatAnimations(session, vehicle_data);

  const bool owns_vehicle_data = unit.Vehicle().OwnsVehicleData(vehicle_data);
  unit.Vehicle().SetVehicleData(unit, session, nullptr);
  if (owns_vehicle_data) {
    unit.Vehicle().ResetOwnedVehicleData(unit, session);
  }

  FirePlayerVehicleDataLostEvent(unit);
}

bool UnitVehicle_HasNonStaticSeat(const WorldSession& session,
                                   std::uint64_t vehicle_guid_lo,
                                   std::uint64_t vehicle_guid_hi,
                                   std::uint8_t seat_index) {
  if (seat_index >= 8) {
    return false;
  }

  const auto raw_guid =
      (static_cast<std::uint64_t>(static_cast<std::uint32_t>(vehicle_guid_hi)) << 32) |
      static_cast<std::uint32_t>(vehicle_guid_lo);
  if (!HasVehicleTransitionTargetGuid(raw_guid)) {
    return false;
  }

  const auto* vehicle_unit = session.objects().GetUnit(ObjectGuid(raw_guid));
  if (vehicle_unit == nullptr) {
    return false;
  }

  const auto* seat_entry =
      LookupVehicleSeatEntryForVehicleSeat(session, *vehicle_unit, seat_index);
  return seat_entry != nullptr && (seat_entry->flags & 0x400u) == 0u;
}

bool UnitVehicle_IsActivePlayerInVehicle(const void* unit) {
  const auto* const mover = static_cast<const CGUnit_C*>(unit);
  if (mover == nullptr || !mover->Movement().CanControlCharacter()) {
    return false;
  }

  const auto* const mover_passenger = mover->Vehicle().GetVehiclePassengerComponent();
  if (HasVehiclePassengerTransitionOrPendingSeatChange(mover_passenger)) {
    return true;
  }

  const auto transport_guid = mover->GetTransportGUID();
  if (!transport_guid.IsEmpty()) {
    if (const auto* const objects = mover->object_manager();
        objects != nullptr) {
      if (const auto* const transport_unit = objects->GetUnit(transport_guid);
          transport_unit != nullptr) {
        const auto* const transport_passenger =
            transport_unit->Vehicle().GetVehiclePassengerComponent();
        return HasVehiclePassengerTransitionOrPendingSeatChange(
                   transport_passenger) ||
               HasPassengerInputControlAttachment(transport_passenger);
      }
    }
  }

  if (((transport_guid.GetRawValue() >> 56) & 0x1u) == 0u &&
      mover->GetGuid() != (mover->object_manager() != nullptr
                               ? mover->object_manager()->GetActivePlayerGuid()
                               : ObjectGuid{})) {
    return true;
  }

  return false;
}

bool UnitVehicle_CanTransferToSeat(const WorldSession& session,
                                    const void* unit,
                                    std::uint32_t target_guid_lo,
                                    std::uint32_t target_guid_hi,
                                    std::uint8_t seat_index) {
  const auto* cg_unit = static_cast<const CGUnit_C*>(unit);
  if (cg_unit == nullptr) {
    return false;
  }

  const auto* passenger = cg_unit->Vehicle().GetVehiclePassengerComponent();
  std::uint64_t current_vehicle_guid = 0;
  std::uint8_t current_seat_index = 0xFF;
  if (passenger != nullptr) {
    current_vehicle_guid = passenger->GetVehicleUnitGuid();
    current_seat_index = passenger->GetPrimarySeatIndex();
  }

  const auto target_guid =
      (static_cast<std::uint64_t>(target_guid_hi) << 32) | target_guid_lo;
  if (target_guid == current_vehicle_guid && seat_index == current_seat_index) {
    return false;
  }

  const bool is_external = HasVehicleTransitionTargetGuid(target_guid);

  std::uint64_t effective_guid;
  if (is_external) {
    effective_guid = target_guid;
  } else {

    if (!cg_unit->Vehicle().HasValidVehicleUnitGuid()) {
      return false;
    }
    effective_guid = current_vehicle_guid;
  }

  const auto* target_unit = session.objects().GetUnit(ObjectGuid(effective_guid));
  if (target_unit == nullptr) {
    return false;
  }

  if (target_unit->Vehicle().GetVehicleData() == nullptr) {
    return false;
  }

  const std::uint8_t effective_seat =
      is_external ? seat_index : current_seat_index;
  const auto* seat_entry =
      LookupVehicleSeatEntryForVehicleSeat(session, *target_unit, effective_seat);
  if (seat_entry == nullptr) {
    return false;
  }

  const auto* root_vehicle = ResolveRootVehicleUnit(*target_unit);
  const bool root_has_scene_model =
      root_vehicle != nullptr && root_vehicle->GetPrimaryM2InstanceId() != 0u;

  constexpr std::uint32_t kMaxAnimationId = 505u;

  if (is_external) {
    if ((seat_entry->flags & 0x00400000u) == 0u) {
      return false;
    }
    const auto anim_id = static_cast<std::uint32_t>(seat_entry->vehicle_enter_anim);
    if (anim_id > kMaxAnimationId) {
      return false;
    }
    return root_has_scene_model;
  }

  const bool has_transfer_bit =
      cg_unit->Movement().Data().HasVehicleSeatTransferPacketBit();
  const std::uint32_t required_flag =
      has_transfer_bit ? 0x00040000u : 0x00080000u;
  if ((seat_entry->flags & required_flag) == 0u) {
    return false;
  }
  const auto anim_id = static_cast<std::uint32_t>(seat_entry->vehicle_exit_anim);
  if (anim_id > kMaxAnimationId) {
    return false;
  }
  return root_has_scene_model;
}

bool UnitVehicle_TransferToSeat(WorldSession& session,
                                const void* const unit,
                                const double timestamp,
                                const std::uint64_t target_guid,
                                const std::uint8_t seat_index) {
  auto* const passenger_unit = const_cast<CGUnit_C*>(
      static_cast<const CGUnit_C*>(unit));
  if (passenger_unit == nullptr) {
    return false;
  }

  const auto target_lo = static_cast<std::uint32_t>(target_guid);
  const auto target_hi = static_cast<std::uint32_t>(target_guid >> 32u);
  if (!UnitVehicle_CanTransferToSeat(session, passenger_unit, target_lo,
                                     target_hi,
                                     seat_index)) {
    return false;
  }

  auto* passenger = passenger_unit->Vehicle().GetVehiclePassengerComponent();
  if (passenger != nullptr) {
    passenger->ProcessPendingSeatChange(session);
  }

  const bool entering = HasVehicleTransitionTargetGuid(target_guid);
  const std::uint64_t effective_vehicle_guid =
      entering ? target_guid
               : (passenger != nullptr ? passenger->GetVehicleUnitGuid() : 0u);
  const std::uint8_t effective_seat =
      entering ? seat_index
               : (passenger != nullptr ? passenger->GetPrimarySeatIndex() : 0xFFu);

  auto* const vehicle_unit =
      session.objects().GetMutableUnit(ObjectGuid(effective_vehicle_guid));
  const auto* const seat_entry =
      vehicle_unit != nullptr
          ? LookupVehicleSeatEntryForVehicleSeat(session, *vehicle_unit,
                                                 effective_seat)
          : nullptr;
  auto* const root_vehicle = vehicle_unit != nullptr
                                 ? const_cast<CGUnit_C*>(
                                       ResolveRootVehicleUnit(*vehicle_unit))
                                 : nullptr;
  void* const root_vehicle_data =
      root_vehicle != nullptr ? root_vehicle->Vehicle().GetVehicleData() : nullptr;
  if (seat_entry == nullptr || root_vehicle == nullptr ||
      root_vehicle_data == nullptr) {
    return false;
  }

  if (passenger == nullptr) {
    passenger = AllocatePassengerForUnit(*passenger_unit);
  }

  const auto animation_id = entering ? seat_entry->vehicle_enter_anim
                                     : seat_entry->vehicle_exit_anim;
  const auto animation_group = entering ? seat_entry->vehicle_enter_anim_bone
                                        : seat_entry->vehicle_exit_anim_bone;
  if (animation_id < 0 || animation_id > 505) {
    return false;
  }

  if (!vehicle::Vehicle_C_StartSeatAnimation(
          root_vehicle_data, static_cast<std::uint32_t>(animation_group),
          static_cast<std::uint32_t>(animation_id))) {
    return false;
  }

  const auto passenger_guid = passenger_unit->GetGuid().GetRawValue();
  (void)vehicle::Vehicle_C_AddPendingSeatTransition(
      root_vehicle_data, static_cast<int>(passenger_guid),
      static_cast<int>(passenger_guid >> 32u),
      static_cast<std::uint32_t>(animation_group),
      vehicle::PendingSeatTransitionPolicy::kCompletePassenger);
  if (entering && (seat_entry->transition_flags & 0x00800000u) != 0u) {
    vehicle::Vehicle_C_SetPendingTransfer(
        root_vehicle_data, passenger_guid,
        static_cast<std::uint32_t>(animation_group));
  }

  const auto transition_start_tick =
      timestamp > 0.0 ? static_cast<std::uint32_t>(timestamp)
                      : core::GameClock::GetTickCount32();
  passenger->BeginPendingSeatChange(
      root_vehicle->GetGuid().GetRawValue(), target_guid, seat_index,
      *seat_entry, entering, transition_start_tick);
  return true;
}

void UnitVehicle_ConditionalExitOnInteraction(WorldSession& session,
                                               CGUnit_C& unit) {
  const auto* const passenger = unit.Vehicle().GetVehiclePassengerComponent();
  if (passenger == nullptr) {
    return;
  }

  const auto* const seat_entry = passenger->GetSeatEntry();
  if (seat_entry == nullptr) {
    return;
  }

  constexpr std::uint32_t kSeatTransitionFlagExitOnInteraction = 0x100000u;
  if ((seat_entry->transition_flags & kSeatTransitionFlagExitOnInteraction) == 0u) {
    return;
  }

  const auto unit_guid = unit.GetGuid();
  if (unit_guid == session.player_control_runtime().ActiveMoverGuid()) {
    const auto active_player_guid = CGObject_C::GetActivePlayerGuid();
    if (active_player_guid == unit_guid) {
      return;
    }
  }

  UnitVehicle_RequestExit(session, &unit);
}

void UnitVehicle_RequestExit(WorldSession& session, const void* unit) {
  auto* const mover_unit = static_cast<CGUnit_C*>(const_cast<void*>(unit));
  if (mover_unit == nullptr) {
    return;
  }

  if (auto* const active_player = session.objects().GetActivePlayer();
       active_player != nullptr &&
       IsActivePlayerVehicleMover(*mover_unit, *active_player,
                                  session.player_control_runtime())) {
    const auto tick_count = openwow::core::GameClock::GetTickCount32();
    if (TryHandleLocalVehicleExit(*mover_unit, *active_player, session,
                                  tick_count)) {
      return;
    }

    session.interaction().SendCastSpell(kVehicleExitFallbackSpellId, 0,
                                         mover_unit->GetGuid().GetRawValue());
    return;
  }

  session.interaction().SendRequestVehicleExit();
}

void UnitVehicle_RequestPrevSeat(WorldSession& session,
                                 const void* unit) {
  auto* const mover_unit = static_cast<CGUnit_C*>(const_cast<void*>(unit));
  if (mover_unit == nullptr) {
    return;
  }

  if (auto* const active_player = session.objects().GetActivePlayer();
       active_player != nullptr &&
       IsActivePlayerVehicleMover(*mover_unit, *active_player,
                                  session.player_control_runtime())) {
    TryHandleLocalVehicleSeatSwitch(*mover_unit, *active_player,
                                    openwow::core::GameClock::GetTickCount32(),
                                    kVehiclePrevSeatDirection);
    return;
  }

  session.interaction().SendRequestVehiclePrevSeat();
}

void UnitVehicle_RequestNextSeat(WorldSession& session,
                                 const void* unit) {
  auto* const mover_unit = static_cast<CGUnit_C*>(const_cast<void*>(unit));
  if (mover_unit == nullptr) {
    return;
  }

  if (auto* const active_player = session.objects().GetActivePlayer();
       active_player != nullptr &&
       IsActivePlayerVehicleMover(*mover_unit, *active_player,
                                  session.player_control_runtime())) {
    TryHandleLocalVehicleSeatSwitch(*mover_unit, *active_player,
                                    openwow::core::GameClock::GetTickCount32(),
                                    kVehicleNextSeatDirection);
    return;
  }

  session.interaction().SendRequestVehicleNextSeat();
}

bool UnitVehicle_RequestSwitchToSeat(WorldSession& session,
                                     const void* unit,
                                     const void* root_vehicle,
                                     const int expanded_seat) {
  auto* const this_unit = static_cast<CGUnit_C*>(const_cast<void*>(unit));
  auto* const vehicle_unit =
      static_cast<CGUnit_C*>(const_cast<void*>(root_vehicle));
  if (this_unit == nullptr || vehicle_unit == nullptr) {
    return false;
  }

  if (vehicle_unit->Vehicle().GetVehicleData() == nullptr ||
      vehicle_unit->Vehicle().GetVehicleEntry() == nullptr) {
    return false;
  }

  const CGUnit_C* resolved_vehicle = nullptr;
  std::uint8_t resolved_seat = 0;
  if (!FindExpandedVehicleSeat(*vehicle_unit, expanded_seat,
                               resolved_vehicle, resolved_seat)) {
    return false;
  }

  if (resolved_vehicle == nullptr) {
    return false;
  }

  const auto* seat_entry =
      LookupVehicleSeatEntryForVehicleSeat(session, *resolved_vehicle,
                                           resolved_seat);
  if (seat_entry == nullptr) {
    return false;
  }

  if (FindDirectVehiclePassengerBySeatIndex(*resolved_vehicle,
                                            resolved_seat) != nullptr) {
    return false;
  }

  if ((seat_entry->flags & VehicleControlSeatFlag::kCanSwitch) == 0u) {
    return false;
  }

  if (GetInputControlSingleton() == nullptr) {
    return false;
  }
  if (!CanUseVehicleControlAction(session, VehicleControlSeatFlag::kCanSwitch)) {
    return false;
  }

  const auto active_mover_guid =
      session.player_control_runtime().ActiveMoverGuid();
  if (this_unit->GetGuid() != active_mover_guid) {

    session.interaction().SendRequestVehicleSwitchSeat(
        resolved_vehicle->GetGuid().GetRawValue(), resolved_seat);
    return true;
  }

  auto* const active_mover =
      const_cast<CGUnit_C*>(session.objects().GetUnit(active_mover_guid));
  if (active_mover == nullptr) {
    return true;
  }

  auto* const passenger = this_unit->Vehicle().GetVehiclePassengerComponent();
  if (passenger != nullptr) {
    PrimeActivePlayerVehicleInputControl(
        *this_unit, openwow::core::GameClock::GetTickCount32());
  }

  const auto vehicle_guid = resolved_vehicle->GetGuid().GetRawValue();
  Vehicle_ForwardSeatSwitch(
      active_mover,
      static_cast<int>(openwow::core::GameClock::GetTickCount32()),
      static_cast<int>(static_cast<std::uint32_t>(vehicle_guid)),
      static_cast<int>(static_cast<std::uint32_t>(vehicle_guid >> 32)),
      resolved_seat);
  return true;
}

void UnitVehicle_UpdateSeatUI(
    const CGUnit_C* unit,
    const data::dbc::VehicleEntry* vehicle_entry) {

  constexpr std::uint32_t kVehicleFlagAngleShow = 0x400u;
  constexpr std::uint32_t kVehicleFlagPowerShow = 0x800u;

  auto& dispatch = ui::game::ScriptEventDispatch::Get();

  const bool in_transition =
      (unit == nullptr || UnitVehicle_IsActivePlayerInVehicle(unit));

  const bool show_angle =
      !in_transition && vehicle_entry != nullptr &&
      (vehicle_entry->flags & kVehicleFlagAngleShow) != 0u;
  if (show_angle) {
    dispatch.FireEventArgs(ui::game::events::VEHICLE_ANGLE_SHOW, {1});
  } else {
    dispatch.FireEvent(ui::game::events::VEHICLE_ANGLE_SHOW);
  }

  const bool show_power =
      !in_transition && vehicle_entry != nullptr &&
      (vehicle_entry->flags & kVehicleFlagPowerShow) != 0u;
  if (show_power) {
    dispatch.FireEventArgs(ui::game::events::VEHICLE_POWER_SHOW, {1});
  } else {
    dispatch.FireEvent(ui::game::events::VEHICLE_POWER_SHOW);
  }

  if (unit != nullptr) {
    InputControl_UpdatePitchEvent(unit->GetMovementInfo().pitch);
  }
}

void* UnitVehicle_FindRootVehicle(void* unit, void* stop_at) {
  auto* current = static_cast<CGUnit_C*>(unit);
  auto* stop = static_cast<CGUnit_C*>(stop_at);
  if (current == nullptr) {
    return nullptr;
  }

  std::unordered_set<const CGUnit_C*> visited;
  while (current != nullptr) {
    if (!visited.insert(current).second) {
      return nullptr;
    }

    if (current == stop && current->Vehicle().GetVehicleEntry() != nullptr) {
      return current;
    }

    auto* passenger = current->Vehicle().GetVehiclePassengerComponent();
    if (passenger == nullptr) {
      return current->Vehicle().GetVehicleEntry() != nullptr ? current : nullptr;
    }

    const auto vehicle_guid = passenger->GetVehicleUnitGuid();
    const auto* objects = current->object_manager();
    auto* next = vehicle_guid != 0 && objects != nullptr
                     ? const_cast<CGUnit_C*>(objects->GetUnit(ObjectGuid(vehicle_guid)))
                     : nullptr;
    if (next == nullptr) {
      return current->Vehicle().GetVehicleEntry() != nullptr ? current : nullptr;
    }

    if (next == stop) {
      return next;
    }

    current = next;
  }

  return nullptr;
}

void* UnitVehicle_WalkPassengerChain(void* unit, void* stop_at) {
  auto* current = static_cast<CGUnit_C*>(unit);
  auto* stop = static_cast<CGUnit_C*>(stop_at);
  if (current == nullptr) {
    return nullptr;
  }

  auto* passenger = current->Vehicle().GetVehiclePassengerComponent();

  const std::uint64_t initial_guid =
      passenger != nullptr ? passenger->GetAltVehicleGuid() : 0;

  if (initial_guid == 0) {

    return current->Vehicle().GetVehicleEntry() != nullptr ? current : nullptr;
  }

  const auto* objects = current->object_manager();
  if (objects == nullptr) {
    return nullptr;
  }
  auto* walker = const_cast<CGUnit_C*>(
      objects->GetUnit(ObjectGuid(initial_guid)));
  if (walker == nullptr) {
    return nullptr;
  }

  std::unordered_set<const CGUnit_C*> visited;
  while (walker != stop) {
    if (!visited.insert(walker).second) {
      return nullptr;
    }

    auto* wp = walker->Vehicle().GetVehiclePassengerComponent();
    const std::uint64_t next_guid =
        wp != nullptr ? wp->GetAltVehicleGuid() : 0;
    if (next_guid == 0) {

      return walker;
    }

    auto* next = const_cast<CGUnit_C*>(objects->GetUnit(ObjectGuid(next_guid)));
    if (next == nullptr) {
      return nullptr;
    }
    walker = next;
  }

  return walker;
}

void UnitVehicle_DestroyCamera(WorldSession& session, void* unit) {
  auto* unit_obj = static_cast<CGUnit_C*>(unit);
  if (unit_obj == nullptr) {
    return;
  }

  auto* const camera = session.world_camera();
  if (camera != nullptr &&
      camera->bound_object() == unit_obj->GetGuid().GetRawValue()) {
    camera->SetBoundObject(0);
  }
}

void UnitVehicle_ProcessSeatChange(WorldSession& session, void* unit,
                                    double timestamp,
                                    std::uint64_t target_guid,
                                    std::uint8_t seat_index,
                                    bool from_update) {
  auto* unit_obj = static_cast<CGUnit_C*>(unit);
  if (unit_obj == nullptr) {
    return;
  }

  if ((unit_obj->State().GetSpellStateFlags() & kSpellStateFlagOnVehicle) == 0 &&
      !from_update) {
    return;
  }

  auto* passenger = unit_obj->Vehicle().GetVehiclePassengerComponent();
  CGUnit_C* vehicle_root = nullptr;
  if (passenger != nullptr) {
    vehicle_root = passenger->GetVehicleObject();
  } else {
    vehicle_root =
        static_cast<CGUnit_C*>(UnitVehicle_FindRootVehicle(unit_obj, nullptr));
  }

  if (vehicle_root != nullptr) {
    vehicle_root->Presentation().SetUnitAlpha(1.0f);
  }

  CGUnit_C* target_unit = nullptr;
  if (IsValidUnitGuidForVehicle(target_guid)) {
    target_unit = const_cast<CGUnit_C*>(
        session.objects().GetUnit(ObjectGuid(target_guid)));
    if (target_unit == nullptr) {

      seat_index = 0xFFu;
      target_guid = 0;
      passenger = unit_obj->Vehicle().GetVehiclePassengerComponent();

      if (passenger == nullptr || passenger->GetAltVehicleGuid() == 0u) {
        return;
      }
    }
  }

  passenger = unit_obj->Vehicle().GetVehiclePassengerComponent();
  if (passenger == nullptr) {
    if (target_guid == 0) {
      return;
    }
    passenger = AllocatePassengerForUnit(*unit_obj);
  }

  passenger->HandleVehicleAssignmentChange(session, timestamp, target_guid,
                                           seat_index,
                                           target_unit, from_update);

  TrySetupVehicleCameraFollowLogic(*unit_obj, session);
}

bool UnitVehicle_TryAttachPassengerFromUpdate(
    WorldSession& session, CGUnit_C& unit, const double timestamp,
    const std::uint64_t target_guid, const std::uint8_t seat_index) {
  if (!IsValidUnitGuidForVehicle(target_guid)) {
    return false;
  }

  std::uint64_t effective_target_guid = target_guid;
  std::uint8_t effective_seat_index = seat_index;
  if (session.objects().GetUnit(ObjectGuid(target_guid)) == nullptr) {
    const auto* const passenger = unit.Vehicle().GetVehiclePassengerComponent();
    if (passenger == nullptr || passenger->GetAltVehicleGuid() == 0u) {

      return true;
    }
    effective_target_guid = 0u;
    effective_seat_index = 0xFFu;
  }

  UnitVehicle_ProcessSeatChange(session, &unit, timestamp,
                                effective_target_guid, effective_seat_index,
                                true);

  if (auto* const passenger =
          unit.Vehicle().GetVehiclePassengerComponent();
      passenger != nullptr) {
    if (effective_target_guid == 0u) {

      passenger->UpdateSeatState(session, timestamp, 0u, 0xFFu, false);
    } else {
      passenger->HandleTransition(
          session, timestamp, VehiclePassengerTransitionType::kAttached,
          effective_target_guid, effective_seat_index,
          core::GameClock::GetTickCount32());
    }
  }
  return true;
}

void UnitVehicle_RebuildCreatePassengerAttachment(
    WorldSession& session, CGUnit_C& unit, const double timestamp) {
  if (unit.Vehicle().GetVehiclePassengerComponent() != nullptr) {
    ReleasePassengerForUnit(unit);
  }

  (void)UnitVehicle_TryAttachPassengerFromUpdate(
      session, unit, timestamp, unit.GetTransportGUID().GetRawValue(),
      unit.Movement().Data().GetTransportSeat());
  TrySetupVehicleCameraFollowLogic(unit, session);
}

bool TryVehicleSeatTransfer(WorldSession& session, CGUnit_C& unit,
                            const double timestamp,
                            net::CDataStore* const packet_remainder,
                            const std::uint64_t target_guid,
                            const std::uint8_t seat_index) {

  auto* passenger = unit.Vehicle().GetVehiclePassengerComponent();
  if (passenger != nullptr) {
    passenger->ClearTransitionData();
    passenger->ProcessPendingSeatChange(session);
  }

  const auto target_lo = static_cast<std::uint32_t>(target_guid);
  const auto target_hi = static_cast<std::uint32_t>(target_guid >> 32);
  if (!UnitVehicle_CanTransferToSeat(session, &unit, target_lo, target_hi,
                                     seat_index)) {
    return false;
  }

  passenger = unit.Vehicle().GetVehiclePassengerComponent();
  if (passenger == nullptr) {
    passenger = AllocatePassengerForUnit(unit);
  }

  if (packet_remainder != nullptr) {
    passenger->CreateTransitionData(*packet_remainder);
  }

  if (!UnitVehicle_TransferToSeat(session, &unit, timestamp, target_guid,
                                  seat_index)) {
    passenger->ClearTransitionData();
    return false;
  }

  passenger->SetPendingMonsterMove(session.monster_move().last_move());

  return true;
}

bool UnitVehicleComponent::IsPartyRaidPlayerVehicle(
    const CGUnit_C &owner, const CGUnit_C &interactor) const {
  if (!owner.IsType(TypeMask::kTypeMaskPlayer)) {
    return false;
  }
  const auto *const vehicle_data =
      static_cast<const std::byte *>(GetVehicleData());
  if (vehicle_data == nullptr ||
      vehicle_runtime_layout::ResolveVehicleEntryPointerField(vehicle_data) ==
          nullptr) {
    return false;
  }
  std::uint8_t available_seats = 0u;
  std::memcpy(&available_seats, vehicle_data + 365, sizeof(available_seats));
  if (available_seats == 0u) {
    return false;
  }

  if (interactor.Movement().Data().GetTransportGuid() == owner.GetGuid().GetRawValue()) {
    return false;
  }
  const auto *const objects = owner.object_manager();
  const auto &groups = GroupSystem::Get();
  const auto guid = owner.GetGuid().GetRawValue();
  return objects != nullptr &&
         (groups.IsPartyUnitGuid(*objects, guid) ||
          groups.IsRaidUnitGuid(*objects, guid));
}

}
