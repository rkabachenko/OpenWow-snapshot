#include "openwow/game/objects/unit/unit_movement_runtime.h"

#include "openwow/game/objects/cgunit.h"

#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/core/client_misc.h"
#include "openwow/core/client_init.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/game/c_input_control.h"
#include "openwow/game/commentator_state.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/game/game_misc_utils.h"
#include "openwow/game/ground_walk.h"
#include "openwow/game/interaction_sender.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cggameobject.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/objects/unit/unit_presentation_runtime.h"
#include "openwow/game/missile_trajectory.h"
#include "openwow/game/player_control_runtime.h"
#include "openwow/game/movement_callbacks.h"
#include "openwow/game/movement_controller.h"
#include "openwow/game/movement/retail_fall_kinematics.h"
#include "openwow/game/passenger_movement.h"
#include "openwow/game/spell_cast_runtime.h"
#include "openwow/game/spell_query_bridge.h"
#include "openwow/game/tutorial_system.h"
#include "openwow/game/transport_manager.h"
#include "openwow/game/unit_path_utils.h"
#include "openwow/game/unit_sound_dispatch.h"
#include "openwow/game/unit_vehicle.h"
#include "openwow/game/vehicle.h"
#include "openwow/game/vehicle_passenger.h"
#include "openwow/game/vehicle_system.h"
#include "openwow/game/world_session.h"
#include "openwow/render/api/math/render_matrix_math.h"
#include "openwow/render/m2/m2_system.h"
#include "openwow/ui/game/api/game_lua_api_action.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/foundation/math/angle_normalize.h"
#include "openwow/foundation/math/copysign_float.h"
#include "openwow/foundation/math/planar_facing_angle.h"
#include "openwow/foundation/math/row_major_mat4x4.h"
#include "openwow/net/client_services_packet_sender.h"
#include "openwow/net/wotlk/movement.h"
#include "openwow/net/wotlk/protocol/packet_sender.h"
#include "openwow/world/camera/world_camera.h"
#include "openwow/world/movement/movement_spline.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <optional>
#include <utility>

namespace openwow::game {

namespace {

constexpr float kMillisecondsToSeconds = 0.001f;
constexpr std::uint32_t kRemoteMovementStepClampMs = 250u;
constexpr std::uint32_t kCanFlyGroundContactThrottleMs = 2000u;
constexpr float kCanFlyGroundProximityAllowance = 2.0f;
constexpr float kCanFlyVerticalClearanceThreshold = 1.5f;
constexpr float kCanFlyListenerVolumeScale = 0.65f;
constexpr std::uint32_t kCommentatorSpectatorFlags2 = 0x00080000u;
constexpr std::uint32_t kCommentatorAdminFlags2 = 0x00400000u;
constexpr std::uint32_t kCommentatorArenaMapType = 4u;
constexpr std::uint32_t kVehicleFlagBoundedYaw = 0x00200000u;
constexpr std::uint32_t kInvalidTerrainTypeId = 0xFFFFFFFFu;

constexpr std::uint32_t kMovementUpdateSteeringTurnInputBit = 0x1u;

constexpr float kStrafeBodyOffsetPureRadians = 1.5707964f;
constexpr float kStrafeBodyOffsetDiagonalRadians = 0.7853982f;

constexpr float kBodyYawBlendRatePerSecond = 2.5f;

constexpr float kBodyYawArrivedEpsilon = 0.001f;

constexpr float kBodyYawRootHandoffRadians = 1.5707964f;

constexpr float kBodyYawCatchUpTurnRateScale = 8.0f;

constexpr float kBodyYawSpringOmega = 20.0f;
constexpr float kBodyYawSpringPadeSquare = 0.48f;
constexpr float kBodyYawSpringPadeCube = 0.235f;

constexpr float kBodyYawMaxLagRadians = 1.5704823f;

constexpr std::uint32_t kVehicleFlagsPinRenderedBodyYaw = 0x00001200u;

constexpr std::uint32_t kAnimationTwistBoneFlags = 0x00000180u;
constexpr std::uint32_t kAnimationTwistBoneSpineFlag = 0x00000080u;
constexpr std::uint32_t kAnimationTwistBoneHeadFlag = 0x00000100u;

constexpr float kBodyTwistPerBoneClampRadians = 0.7853982f;
constexpr float kBodyTwistClearEpsilon = 1e-05f;
constexpr std::uint32_t kKeyBoneLookupSpineLow = 4u;
constexpr std::uint32_t kKeyBoneLookupHead = 6u;

constexpr std::uint32_t kVehicleSeatFlagSpinePitch = 0x00000200u;

constexpr std::uint32_t kBodyYawTurnLatchSuppressMask = 0x02e0100fu;

constexpr float kRetailFallApexGravityDivisor = -19.291103f;

[[nodiscard]] bool IsRisingBeforeFallApex(const MovementInfo &movement) {
  return movement.HasFallingLaunchVelocity() &&
         static_cast<float>(movement.fall_time) * kMillisecondsToSeconds <
             movement.jump.z_speed / kRetailFallApexGravityDivisor;
}

constexpr std::array<bool, 4> kWaterRippleSplashModeTable = {false, false, true,
                                                             false};

constexpr std::uint32_t kTerrainGapFallBlockedMovementFlags =
    kMoveFlagOnTransport | kMoveFlagRoot;

constexpr std::uint32_t kParentMovementAllowStopFlag = 0x400u;

constexpr float kNonPlayerStepAllowance = 2.0f;

[[nodiscard]] bool AllowsTerrainGapFallingFromMovementFlags(
    const std::uint32_t runtime_flags) {
  return (runtime_flags & kTerrainGapFallBlockedMovementFlags) == 0u;
}
constexpr std::uint32_t kVehicleFlagCustomPitch = 0x00000040u;
constexpr float kFullCircleRadians = 6.2831855f;
constexpr std::uint32_t kSpellAttrEx4SuppressBodyFacingTargetTracking = 0x80000u;
constexpr std::uint32_t kSpellAttrEx1Channeled = 0x4000u;
constexpr std::uint32_t kChannelInterruptFlagTurning = 0x10u;
constexpr std::uint32_t kMovementUpdateVehiclePostBit = 0x00800000u;
constexpr std::uint32_t kUnitTypeMask = 8u;
constexpr std::uint32_t kJumpQueueSeatFlag0x400 = 0x00000400u;
constexpr std::uint32_t kJumpMovingGateSeatFlag0x200 = 0x00000200u;
constexpr std::uint32_t kJumpMovingGateSeatFlag0x2000 = 0x00002000u;

constexpr std::uint32_t kSpellStateDirectJumpQueue = 0x01000000u;

constexpr std::uint32_t kSpellStateWireAnnouncedFalling = 0x80u;

constexpr std::uint16_t kSplineStopInterpolationFlags2Mask =
    movement::CMovementData::kInterpolatedFlags2Mask;
constexpr std::uint32_t kDirectionalLocomotionMask =
    kMoveFlagForward | kMoveFlagBackward |
    kMoveFlagStrafeLeft | kMoveFlagStrafeRight |
    kMoveFlagTurnLeft | kMoveFlagTurnRight;
constexpr std::uint32_t kLocomotionStateMask =
    kDirectionalLocomotionMask |
    kMoveFlagPitchUp | kMoveFlagPitchDown |
    kMoveFlagPendingStop | kMoveFlagPendingStrafeStop |
    kMoveFlagPendingForward | kMoveFlagPendingBackward |
    kMoveFlagPendingStrafeLeft | kMoveFlagPendingStrafeRight |
    kMoveFlagAscending | kMoveFlagDescending |
    kMoveFlagSplineEnabled;
std::uint32_t g_last_can_fly_fall_start_timestamp = 0u;
bool g_can_fly_landing_deadline_initialized = false;
std::uint32_t g_can_fly_landing_deadline = 0u;
JumpLiquidSurfaceHeightCallback g_jump_liquid_surface_height_callback = nullptr;
void *g_jump_liquid_surface_height_context = nullptr;
WaterRippleSpawnCallback g_water_ripple_spawn_callback = nullptr;
void* g_water_ripple_spawn_context = nullptr;

bool HasPendingVehicleSeatChange(const CGUnit_C& unit) {
  const auto* const passenger =
      unit.Vehicle().GetVehiclePassengerComponent();
  return passenger != nullptr &&
         passenger->HasFlag(VehiclePassengerFlag::kPendingSeatChange);
}

bool HasJumpMovementFlags(const MovementInfo& movement_info) {
  return (movement_info.flags &
          (kMoveFlagForward | kMoveFlagBackward |
           kMoveFlagStrafeLeft | kMoveFlagStrafeRight)) != 0u;
}

bool HasJumpTurnFlags(const MovementInfo& movement_info) {
  return (movement_info.flags & (kMoveFlagTurnLeft | kMoveFlagTurnRight)) != 0u;
}

bool HasJumpQueueSeatFlag(const CGUnit_C& unit) {
  const auto* const seat_entry = unit.Vehicle().GetVehiclePassengerSeatEntry();
  if (seat_entry == nullptr) {
    return false;
  }

  return (seat_entry->flags & kJumpQueueSeatFlag0x400) != 0u;
}

void SendRawMountedJumpPacket() {
  (void)openwow::net::ClientServices__SendPacket(
      openwow::net::wotlk::WorldPacket(
          openwow::net::wotlk::Opcode::CMSG_MOUNTSPECIAL_ANIM));
}

bool AllowsMountedJumpWhileMoving(const CGUnit_C& unit,
                                  const MovementInfo& movement_info) {
  if (const auto* const seat_entry = unit.Vehicle().GetVehiclePassengerSeatEntry();
      seat_entry != nullptr) {
    const auto seat_flags = seat_entry->flags;
    if ((seat_flags & kJumpQueueSeatFlag0x400) == 0u &&
        (seat_flags & kJumpMovingGateSeatFlag0x200) != 0u) {
      return false;
    }

    if ((seat_flags & kJumpQueueSeatFlag0x400) == 0u &&
        (seat_flags & kJumpMovingGateSeatFlag0x2000) != 0u) {
      return true;
    }
  }

  return (movement_info.flags2 & kMoveFlag2Unknown4) != 0u ||
         (movement_info.flags & kMoveFlagDisableGravity) != 0u;
}

[[nodiscard]] std::optional<float> ResolveJumpLiquidSurfaceHeight(
    const CGUnit_C &unit) {
  if (g_jump_liquid_surface_height_callback == nullptr) {
    return std::nullopt;
  }

  return g_jump_liquid_surface_height_callback(
      unit, g_jump_liquid_surface_height_context);
}

struct VehicleFacingDescriptorView {
  std::array<std::byte, 80> prefix{};
  float facing_anchor = 0.0f;
};

static_assert(offsetof(VehicleFacingDescriptorView, facing_anchor) == 80u);

float WrapVehicleYaw(float angle, const float min_yaw, const float max_yaw) {
  for (; min_yaw > angle; angle += kFullCircleRadians) {
  }
  if (max_yaw > angle) {
    return angle;
  }
  float wrapped_high = angle;
  float high_delta = angle - max_yaw;
  float wrapped_low = angle - kFullCircleRadians;
  while (wrapped_low > max_yaw) {
    wrapped_high = wrapped_low;
    high_delta = wrapped_high - max_yaw;
    wrapped_low -= kFullCircleRadians;
  }
  return wrapped_low <= min_yaw && high_delta < min_yaw - wrapped_low
             ? wrapped_high
             : wrapped_low;
}

float ClampVehicleYaw(float angle, const float min_yaw, const float max_yaw) {
  return std::clamp(WrapVehicleYaw(angle, min_yaw, max_yaw), min_yaw,
                    max_yaw);
}

bool TryGetVehicleYawBounds(const CGUnit_C &unit, float &min_yaw,
                            float &max_yaw) {
  const auto *const data =
      static_cast<const VehicleFacingDescriptorView *>(unit.Vehicle().GetVehicleData());
  const auto *const entry = unit.Vehicle().GetVehicleEntry();
  if (data == nullptr || entry == nullptr ||
      (entry->flags & kVehicleFlagBoundedYaw) == 0u) {
    return false;
  }
  min_yaw = data->facing_anchor - entry->yaw_left_limit;
  max_yaw = data->facing_anchor + entry->yaw_right_limit;
  return true;
}

bool TryGetVehiclePitchBounds(const CGUnit_C &unit, float &min_pitch,
                              float &max_pitch) {
  const auto *const entry = unit.Vehicle().GetVehicleEntry();
  if (entry == nullptr || (entry->flags & kVehicleFlagCustomPitch) == 0u) {
    return false;
  }
  min_pitch = entry->pitch_min;
  max_pitch = entry->pitch_max;
  return true;
}

void SyncAuthoritativeSpline(WorldSession &session, const CGUnit_C &unit,
                             const MovementUpdate &update) {
  std::optional<float> target_facing;
  if (update.spline.facing_type == SplineFacing::kTarget) {
    const auto *const target = session.objects().Get(update.spline.facing_target);
    if (target != nullptr) {
      const auto source = unit.GetPosition();
      const auto destination = target->GetPosition();
      target_facing =
          std::atan2(destination.y - source.y, destination.x - source.x);
      if (update.movement.IsOnTransport() &&
          !update.movement.transport.guid.IsEmpty()) {
        const auto *const transport =
            session.transport_mgr().GetTransport(update.movement.transport.guid);
        if (transport != nullptr) {
          *target_facing -= transport->GetFacing();
        }
      }
    }
  }
  session.movement_spline_mgr().SyncAuthoritativeSpline(
      unit.GetGuid().GetRawValue(), update, target_facing);
}

bool HasSignedTimestampReached(const std::uint32_t timestamp,
                               const std::uint32_t deadline) {
  return static_cast<std::int32_t>(timestamp - deadline) >= 0;
}

bool CanUseCommentatorMovementControls(const CGUnit_C &unit) {
  if (!unit.IsPlayer()) {
    return false;
  }
  const auto flags2 = unit.State().GetUnitFlags2();
  if ((flags2 & kCommentatorSpectatorFlags2) == 0u) {
    return false;
  }
  if ((flags2 & kCommentatorAdminFlags2) != 0u) {
    return true;
  }
  const auto *const dbc = unit.dbc_loader();
  const auto *const objects = unit.object_manager();
  if (dbc == nullptr || objects == nullptr) {
    return false;
  }
  const auto *const map = dbc->map().LookupEntry(objects->GetMapId());
  return map != nullptr && map->map_type == kCommentatorArenaMapType;
}

std::uint32_t ResolveCanFlyLandingSoundKitId(const CGUnit_C &unit) {
  const auto sound_data_id = unit.Sound().ActiveCreatureSoundDataId();
  const auto *const dbc = unit.dbc_loader();
  if (sound_data_id == 0u || dbc == nullptr) {
    return 0u;
  }
  const auto *const sound =
      dbc->creature_sound_data().LookupEntry(sound_data_id);
  return sound != nullptr ? sound->sound_exertion_id : 0u;
}

void PlayCanFlyLandingSound(const CGUnit_C &unit,
                            const std::uint32_t sound_kit_id,
                            const Position &position) {
  if (sound_kit_id == 0u) {
    return;
  }
  float sound_position[3] = {position.x, position.y, position.z};
  auto &sound = unit.sound_runtime();
  if (unit.GetGuid() == CGObject_C::GetActivePlayerGuid()) {
    audio::SoundKitPlaybackOptions options;
    options.playback_priority = 110u;
    if (sound.IsListenerAtCharacter()) {
      options.volume_scale = kCanFlyListenerVolumeScale;
      static_cast<void>(sound.PlaySoundKit(sound_kit_id, nullptr, nullptr,
                                           options));
      return;
    }
    static_cast<void>(
        sound.PlaySoundKit(sound_kit_id, sound_position, nullptr, options));
    return;
  }
  static_cast<void>(sound.PlaySoundKit(sound_kit_id, sound_position));
}

class ScopedMovementInteractionFlag {
 public:
  ScopedMovementInteractionFlag(PlayerControlRuntime &control,
                                const std::uint32_t flag)
      : control_(control), flag_(flag),
        was_set_((control.movement_interaction_flags & flag) != 0u) {
    control_.movement_interaction_flags |= flag_;
  }

  ~ScopedMovementInteractionFlag() {
    if (!was_set_) {
      control_.movement_interaction_flags &= ~flag_;
    }
  }

  ScopedMovementInteractionFlag(const ScopedMovementInteractionFlag&) = delete;
  ScopedMovementInteractionFlag& operator=(const ScopedMovementInteractionFlag&) = delete;

 private:
  PlayerControlRuntime &control_;
  std::uint32_t flag_;
  bool was_set_;
};

[[nodiscard]] PlayerMovementGateState BuildInputControlStopForwardGateState(
    const CGUnit_C& unit) {
  PlayerMovementGateState state;
  state.health = static_cast<std::int32_t>(unit.State().GetHealth());
  state.has_knockdown_animation = unit.Mount().HasKnockdownAnimation(unit);
  state.is_active_player = unit.IsActivePlayer();
  state.vehicle_control_allows_free_movement = true;
  state.is_in_vehicle_transition = UnitVehicle_IsActivePlayerInVehicle(&unit);
  return state;
}

void SeedMovementTransportState(movement::CMovementData &movement_data,
                                const MovementInfo &movement_info) {
  movement_data.SeedAuthoritativeTransportState(movement_info);
}

[[nodiscard]] std::uint32_t TransportMovementTimestamp() {

  return core::CMovementRuntime_GetMovementTimestamp();
}

MovementInfo MovementInfoFromRuntimeData(
    const movement::CMovementData &data, const MovementInfo &base,
    const ObjectManager &objects, std::uint32_t timestamp,
    bool consume_pending_transport_time2);

MovementInfo BuildTransportMovementInfo(const MovementInfo &current_movement,
                                         const movement::CMovementData &movement_data) {
  MovementInfo updated = current_movement;
  updated.flags &= ~static_cast<std::uint32_t>(kMoveFlagSplineEnabled);
  if (movement_data.HasParentMovement() &&
      (movement_data.GetParentMovementFlags() &
       movement::CMovementData::kParentAllowStopFlag) == 0u) {
    updated.flags |= kMoveFlagSplineEnabled;
  }
  if (!movement_data.IsOnTransport()) {
    updated.flags &= ~static_cast<std::uint32_t>(kMoveFlagOnTransport);
    updated.transport = {};
    const auto &world_position = movement_data.GetTransformPosition();
    updated.x = world_position[0];
    updated.y = world_position[1];
    updated.z = world_position[2];
    updated.orientation = movement_data.GetScalarFacing();
    return updated;
  }

  updated.flags |= static_cast<std::uint32_t>(kMoveFlagOnTransport);
  updated.transport.guid = ObjectGuid(movement_data.GetTransportGuid());
  updated.transport.offset_x = movement_data.GetTransformPosition()[0];
  updated.transport.offset_y = movement_data.GetTransformPosition()[1];
  updated.transport.offset_z = movement_data.GetTransformPosition()[2];
  if (!movement_data.UsesPackedOrientation()) {
    updated.transport.offset_o = movement_data.GetScalarFacing();
  }
  updated.transport.seat = static_cast<std::int8_t>(movement_data.GetTransportSeat());
  updated.transport.time = TransportMovementTimestamp();

  updated.transport.time2 = 0u;
  return updated;
}

bool SendActiveMoverTransportChangePacket(const WorldSession &session,
                                           const CGUnit_C &unit,
                                           const movement::CMovementData &data,
                                           const MovementInfo &movement_info) {
  if (!unit.IsActivePlayer()) {
    return false;
  }

  MovementInfo packet_movement = MovementInfoFromRuntimeData(
      data, movement_info, session.objects(), session.CurrentClientTimeMs(),
      false);
  packet_movement.time = session.CurrentClientTimeMs();
  std::uint32_t transport_time2 = 0u;
  if (core::CMovementRuntime_TakePendingTransportTime2(transport_time2)) {
    packet_movement.flags2 |= kMoveFlag2InterpolatedMovement;
    packet_movement.transport.time2 = transport_time2;
  }
  if (!packet_movement.IsOnTransport()) {

    packet_movement.flags |= kMoveFlagOnTransport;
    const auto &position = data.GetTransformPosition();
    packet_movement.transport.guid = ObjectGuid(0u);
    packet_movement.transport.offset_x = position[0];
    packet_movement.transport.offset_y = position[1];
    packet_movement.transport.offset_z = position[2];
    packet_movement.transport.offset_o = data.GetScalarFacing();
    packet_movement.transport.seat =
        static_cast<std::int8_t>(data.GetTransportSeat());
    packet_movement.transport.time = TransportMovementTimestamp();
  }

  return openwow::net::ClientServices__SendPacket(openwow::net::wotlk::PacketSender::BuildMovement(
      openwow::net::wotlk::Opcode::CMSG_MOVE_CHNG_TRANSPORT, unit.GetGuid(), packet_movement));
}

MovementInfo MovementInfoFromRuntimeData(
    const movement::CMovementData &data, const MovementInfo &base,
    const ObjectManager &objects, const std::uint32_t timestamp,
    const bool consume_pending_transport_time2 = false) {
  MovementInfo result = base;
  result.flags = data.GetRuntimeFlags() & 0x7FFFFFFFu;
  result.flags &= ~static_cast<std::uint32_t>(kMoveFlagSplineEnabled);
  if (data.HasParentMovement() &&
      (data.GetParentMovementFlags() &
       movement::CMovementData::kParentAllowStopFlag) == 0u) {
    result.flags |= kMoveFlagSplineEnabled;
  }
  result.flags2 = data.GetRuntimeFlags2();
  result.time = timestamp;
  result.pitch = data.GetRuntimePitch();
  result.fall_time = data.GetRuntimeFallTime();
  result.jump.z_speed = data.GetRuntimeJumpZSpeed();
  result.jump.sin_angle = data.GetRuntimeJumpSinAngle();
  result.jump.cos_angle = data.GetRuntimeJumpCosAngle();
  result.jump.xy_speed = data.GetRuntimeJumpXYSpeed();

  const auto &position = data.GetTransformPosition();
  if (data.IsOnTransport()) {
    result.flags |= kMoveFlagOnTransport;
    result.transport.guid = ObjectGuid(data.GetTransportGuid());
    result.transport.seat = static_cast<std::int8_t>(data.GetTransportSeat());
    result.transport.offset_x = position[0];
    result.transport.offset_y = position[1];
    result.transport.offset_z = position[2];
    result.transport.offset_o = data.GetScalarFacing();
    result.transport.time = TransportMovementTimestamp();
    result.transport.time2 = 0u;
    if (consume_pending_transport_time2) {
      std::uint32_t transport_time2 = 0u;
      if (core::CMovementRuntime_TakePendingTransportTime2(
              transport_time2)) {
        result.flags2 |= kMoveFlag2InterpolatedMovement;
        result.transport.time2 = transport_time2;
      }
    }
    float world_position[3]{};
    data.GetPassengerWorldPosition(objects, world_position);
    result.x = world_position[0];
    result.y = world_position[1];
    result.z = world_position[2];
    result.orientation = data.GetPassengerWorldFacing(objects);
  } else {
    result.flags &= ~kMoveFlagOnTransport;
    result.transport = {};
    result.x = position[0];
    result.y = position[1];
    result.z = position[2];
    result.orientation = data.GetScalarFacing();
  }
  return result;
}

[[nodiscard]] bool IsGravityForceMovementAck(
    const std::uint16_t opcode) noexcept {
  using Op = net::wotlk::Opcode;

  return opcode == static_cast<std::uint16_t>(
                       Op::CMSG_MOVE_GRAVITY_DISABLE_ACK) ||
         opcode == static_cast<std::uint16_t>(
                       Op::CMSG_MOVE_GRAVITY_ENABLE_ACK);
}

void UpdateWireAnnouncedFallingLatch(CGUnit_C &owner,
                                     const std::uint32_t runtime_flags) {
  if ((runtime_flags & kMoveFlagOnTransport) != 0u) {
    return;
  }
  if ((runtime_flags & kMoveFlagFalling) != 0u) {
    owner.State().AddSpellStateFlags(kSpellStateWireAnnouncedFalling);
  } else {
    owner.State().ClearSpellStateFlags(kSpellStateWireAnnouncedFalling);
  }
}

bool SendQueuedMovementEvent(WorldSession &session, const CGUnit_C &owner,
                             const movement::CMovementData &data,
                             const std::uint16_t opcode,
                             const std::uint32_t timestamp,
                             const movement::CPlayerMoveEvent &event) {
  using Op = net::wotlk::Opcode;
  const auto movement_opcode = static_cast<Op>(opcode);
  if (!owner.IsActiveMover() && !IsGravityForceMovementAck(opcode)) {
    return false;
  }

  const auto event_type =
      static_cast<movement::MoveEventType>(event.event_type);
  const bool unacknowledged_server_value =
      (event_type >= movement::MoveEventType::kSetRunSpeed &&
       event_type <= movement::MoveEventType::kSetPitchRate) ||
      event_type == movement::MoveEventType::kSetCollisionHeight;
  if (!event.needs_ack && unacknowledged_server_value) {

    return false;
  }

  const auto mover = owner.GetGuid();
  const auto movement_info = MovementInfoFromRuntimeData(
      data, owner.GetMovementInfo(), session.objects(), timestamp,
      true);

  switch (movement_opcode) {
    case Op::CMSG_FORCE_MOVE_ROOT_ACK:
      return session.Send(net::wotlk::PacketSender::BuildForceMoveRootAck(
          mover, event.auxiliary_u32, movement_info));
    case Op::CMSG_FORCE_MOVE_UNROOT_ACK:
      return session.Send(net::wotlk::PacketSender::BuildForceMoveUnrootAck(
          mover, event.auxiliary_u32, movement_info));
    case Op::CMSG_MOVE_FEATHER_FALL_ACK:
      return session.Send(net::wotlk::PacketSender::BuildMoveFeatherFallAck(
          mover, event.auxiliary_u32, movement_info,
          (movement_info.flags & kMoveFlagFallingSlow) != 0u));
    case Op::CMSG_MOVE_WATER_WALK_ACK:
      return session.Send(net::wotlk::PacketSender::BuildMoveWaterWalkAck(
          mover, event.auxiliary_u32, movement_info,
          (movement_info.flags & kMoveFlagWaterwalking) != 0u));
    case Op::CMSG_MOVE_HOVER_ACK:
      return session.Send(net::wotlk::PacketSender::BuildMoveHoverAck(
          mover, event.auxiliary_u32, movement_info,
          (movement_info.flags & kMoveFlagHover) != 0u));
    case Op::CMSG_MOVE_SET_CAN_FLY_ACK:
      return session.Send(net::wotlk::PacketSender::BuildMoveSetCanFlyAck(
          mover, event.auxiliary_u32, movement_info,
          (movement_info.flags & kMoveFlagCanFly) != 0u));
    case Op::CMSG_MOVE_SET_CAN_TRANSITION_BETWEEN_SWIM_AND_FLY_ACK:
      return session.Send(
          net::wotlk::PacketSender::
              BuildMoveSetCanTransitionBetweenSwimAndFlyAck(
                  mover, event.auxiliary_u32, movement_info,
                  (movement_info.flags2 &
                   kMoveFlag2CanTransitionBetweenSwimAndFly) != 0u));
    case Op::CMSG_MOVE_GRAVITY_DISABLE_ACK:
      return session.Send(net::wotlk::PacketSender::BuildMoveGravityDisableAck(
          mover, event.auxiliary_u32, movement_info));
    case Op::CMSG_MOVE_GRAVITY_ENABLE_ACK:
      return session.Send(net::wotlk::PacketSender::BuildMoveGravityEnableAck(
          mover, event.auxiliary_u32, movement_info));
    case Op::CMSG_FORCE_RUN_SPEED_CHANGE_ACK:
    case Op::CMSG_FORCE_RUN_BACK_SPEED_CHANGE_ACK:
    case Op::CMSG_FORCE_WALK_SPEED_CHANGE_ACK:
    case Op::CMSG_FORCE_SWIM_SPEED_CHANGE_ACK:
    case Op::CMSG_FORCE_SWIM_BACK_SPEED_CHANGE_ACK:
    case Op::CMSG_FORCE_TURN_RATE_CHANGE_ACK:
    case Op::CMSG_FORCE_FLIGHT_SPEED_CHANGE_ACK:
    case Op::CMSG_FORCE_FLIGHT_BACK_SPEED_CHANGE_ACK:
    case Op::CMSG_FORCE_PITCH_RATE_CHANGE_ACK:

      return session.Send(net::wotlk::PacketSender::BuildForceSpeedChangeAck(
          movement_opcode, mover, event.auxiliary_u32, movement_info,
          event.auxiliary_f32));
    case Op::CMSG_MOVE_KNOCK_BACK_ACK:
      return session.Send(net::wotlk::PacketSender::BuildMoveKnockBackAck(
          mover, event.auxiliary_u32, movement_info));
    case Op::CMSG_MOVE_SET_COLLISION_HGT_ACK:
      return session.Send(net::wotlk::PacketSender::BuildMoveSetCollisionHeightAck(
          mover, event.auxiliary_u32, movement_info, event.auxiliary_f32));
    case Op::MSG_MOVE_TELEPORT_ACK: {

      net::wotlk::WorldPacket packet(Op::MSG_MOVE_TELEPORT_ACK);
      net::wotlk::AppendPackedGuid(packet, mover);
      packet.AppendU32(event.auxiliary_u32);
      packet.AppendU32(timestamp);
      return session.Send(packet);
    }
    case Op::CMSG_CHANGE_SEATS_ON_CONTROLLED_VEHICLE: {

      net::wotlk::WorldPacket packet(
          Op::CMSG_CHANGE_SEATS_ON_CONTROLLED_VEHICLE);
      net::wotlk::AppendPackedGuid(packet, mover);
      net::wotlk::WriteMovementInfo(packet, movement_info);
      const ObjectGuid accessory{
          static_cast<std::uint64_t>(event.auxiliary_u32) |
          (static_cast<std::uint64_t>(event.auxiliary_u32_secondary) << 32u)};
      net::wotlk::AppendPackedGuid(packet, accessory);
      packet.AppendU8(event.auxiliary_u8);
      return session.Send(packet);
    }
    default:

      return session.Send(net::wotlk::PacketSender::BuildMovement(
          movement_opcode, mover, movement_info));
  }
}

[[nodiscard]] PlayerMovementGateState BuildTurnTowardFacingGateState(
    const CGUnit_C& unit) {
  PlayerMovementGateState state;
  state.health = static_cast<std::int32_t>(unit.State().GetHealth());
  state.has_knockdown_animation = unit.Mount().HasKnockdownAnimation(unit);
  state.is_active_player = unit.IsActivePlayer();
  state.vehicle_control_allows_free_movement = true;
  state.is_in_vehicle_transition = UnitVehicle_IsActivePlayerInVehicle(&unit);
  return state;
}

[[nodiscard]] bool CommitTurnTowardFacing(WorldSession &session,
                                           CGUnit_C& unit,
                                           const std::uint32_t timestamp,
                                           const float world_facing) {
  GetInputControlSingleton();
  if (!CInputControl::IsPlayerAliveAndFree(
          BuildTurnTowardFacingGateState(unit))) {
    return false;
  }

  const ScopedMovementInteractionFlag suppress_auto_attack_cancel(
      session.player_control_runtime(), 0x1u);

  unit.Movement().SendSetFacing(session, timestamp, world_facing);
  return true;
}

[[nodiscard]] std::uint32_t ResolveBodyFacingSpellId(const CGUnit_C &unit) {
  if (const auto &channel = unit.Casts().GetChannelCast(); channel.spell_id != 0u) {
    return channel.spell_id;
  }
  if (const auto &current = unit.Casts().GetCurrentCast(); current.spell_id != 0u) {
    return current.spell_id;
  }
  return unit.Casts().GetChannelSpellId(unit);
}

[[nodiscard]] bool ActiveSpellSuppressesBodyFacingTargetTracking(const CGUnit_C &unit) {
  const std::uint32_t spell_id = ResolveBodyFacingSpellId(unit);
  if (spell_id == 0u) {
    return false;
  }

  const auto spell = SpellQueryBridge::Get().Query(spell_id);
  return spell.has_value() &&
         (spell->attributesEx4 & kSpellAttrEx4SuppressBodyFacingTargetTracking) != 0u;
}

[[nodiscard]] float ComputeFacingAngleToUnit(const CGUnit_C &source, const CGUnit_C &target) {
  return openwow::math::ComputeRetailPlanarFacingAngle(
      openwow::math::PlanarPoint{source.GetX(), source.GetY()},
      openwow::math::PlanarPoint{target.GetX(), target.GetY()});
}

}

void SetJumpLiquidSurfaceHeightCallback(
    JumpLiquidSurfaceHeightCallback callback, void *context) {
  g_jump_liquid_surface_height_callback = callback;
  g_jump_liquid_surface_height_context = context;
}

void ClearJumpLiquidSurfaceHeightCallback() {
  g_jump_liquid_surface_height_callback = nullptr;
  g_jump_liquid_surface_height_context = nullptr;
}

void SetWaterRippleSpawnCallback(WaterRippleSpawnCallback callback,
                                 void* context) {
  g_water_ripple_spawn_callback = callback;
  g_water_ripple_spawn_context = context;
}

void ClearWaterRippleSpawnCallback() {
  g_water_ripple_spawn_callback = nullptr;
  g_water_ripple_spawn_context = nullptr;
}

bool UnitMovementRuntime::IsInWater() const {
  return in_water_;
}

void PlayerControlRuntime::InstallInitialActiveMover(
    WorldSession &session, ObjectManager &objects,
    UnitMissileTrajectory_C &missile_trajectory,
    const std::uint64_t mover_guid) {
  auto &control = session.player_control_runtime();
  control.active_mover_guid = mover_guid;

  auto pkt = net::wotlk::PacketSender::BuildSetActiveMover(mover_guid);
  (void)net::ClientServices__SendPacket(pkt);

  const auto timestamp = core::GameClock::GetTickCount32();
  if (auto *input = GetInputControlSingleton(); input != nullptr) {
    input->ProcessMovementNow(timestamp, true);
  }

  auto *unit = objects.GetMutableUnit(ObjectGuid(control.active_mover_guid));
  if (unit == nullptr) {
    UnitPresentationRuntime::UpdateCameraTargetAndMissilePreview(
        missile_trajectory, nullptr);
    return;
  }

  ui::game::ScriptEventDispatch::Get().FireEvent(
      ui::game::events::VEHICLE_UPDATE);

  auto& movement_data = unit->Movement().Data();
  if (movement_data.HasActiveMoverMotion()) {
    movement_data.DelayActiveMoverDeadline(timestamp);
    unit->Movement().movement_timeline_.active_mover_deadline =
        timestamp + 500u;
  }

  const auto transport_guid = unit->GetTransportGUID();
  if (!transport_guid.IsEmpty()) {
    auto *transport_obj = CGObject_HasFlags(
        objects,
        transport_guid.GetRawValue(),
        static_cast<std::uint32_t>(kTypeMaskGameObject));
    if (transport_obj) {
      auto *transport_go = static_cast<CGGameObject_C *>(transport_obj);

      openwow::core::CMovementRuntime_SetMovementTimestamp(
          timestamp + transport_go->GetObjectTimeOffsetMs());
    }
  }

  UnitPresentationRuntime::UpdateCameraTargetAndMissilePreview(
      missile_trajectory, unit);
}

void UnitMovementRuntime::FaceTowardObject(const std::uint64_t target_guid,
                                const bool update_visuals) {
  auto* const objects = owner_.object_manager();
  const WorldObject* const target =
      objects != nullptr ? objects->Get(ObjectGuid(target_guid)) : nullptr;
  if (target == nullptr) {
    return;
  }

  const Position my_position = owner_.GetPosition();
  const Position target_position = target->GetPosition();
  const float facing = openwow::math::ComputeRetailPlanarFacingAngle(
      openwow::math::PlanarPoint{my_position.x, my_position.y},
      openwow::math::PlanarPoint{target_position.x, target_position.y});
  data_.SetFacingWithVisualUpdate(*objects, facing, update_visuals);
}

void UnitMovementRuntime::TurnTowardTarget(WorldSession &session) {
  const auto target_guid = owner_.State().GetTarget();
  if (target_guid.IsEmpty()) {
    return;
  }

  const auto* const objects = owner_.object_manager();
  const auto *target = objects != nullptr ? objects->GetUnit(target_guid) : nullptr;
  if (target == nullptr) {
    return;
  }

  (void)CommitTurnTowardFacing(session, owner_,
                               openwow::core::GameClock::GetTickCount32(),
                               ComputeFacingAngleToUnit(owner_, *target));
}

void UnitMovementRuntime::ApplyCreateUpdate(
    const std::uint32_t client_receive_tick_ms) {
  if (owner_.position_.IsLiving()) {
    data_.SyncAuthoritativeMovementInfo(owner_.position_.movement, true);
    data_.ResetServerMovementTimeline(client_receive_tick_ms,
                                      owner_.position_.movement.time);
    for (std::size_t index = 0; index < kMaxSpeeds; ++index) {
      (void)data_.SetSpeed(static_cast<SpeedType>(index),
                           owner_.position_.speeds[index]);
    }
    if (!owner_.IsActiveMover()) {
      data_.RefreshQueuedMovementPreview(client_receive_tick_ms);
    }
  }

}

void UnitMovementRuntime::RefreshCreateRootedGroundContact() {

  if ((data_.GetRuntimeFlags() & kMoveFlagRoot) == 0u ||
      data_.IsOnTransport()) {
    return;
  }

  const auto position = data_.GetTransformPosition();
  const auto ground = owner_.Presentation().QueryGroundSurface(position, 0.2f);
  if (!ground.hit) {
    return;
  }

  data_.SetTransformPosition(position[0], position[1], ground.ground_z);

  owner_.position_.movement.z = ground.ground_z;

  data_.SetGroundSlopeZ(ground.normal_z);
}

void UnitMovementRuntime::RunMovementPostUpdate(
    WorldSession &session, const std::uint32_t timestamp,
    const std::uint32_t frame_delta_ms) {
  const Position position = owner_.GetPosition();
  const float event_position[3] = {position.x, position.y, position.z};

  UnitSoundGroundState ground_state{};
  const bool has_ground_state =
      UnitSound_QueryGroundState(owner_, event_position, ground_state);
  post_update_ground_state_ = ground_state;
  post_update_ground_state_result_ = has_ground_state;
  post_update_ground_state_tick_ = timestamp;
  post_update_ground_state_position_ = {position.x, position.y, position.z};
  post_update_ground_state_stamped_ = true;
  const bool has_liquid = has_ground_state && ground_state.has_liquid_surface;
  const float liquid_depth =
      has_liquid ? ground_state.liquid_surface_z - position.z : 0.0f;

  owner_.Presentation().Footprint().SetTerrainTypeId(
      has_ground_state ? ground_state.terrain_type_id : kInvalidTerrainTypeId);

  ReconcileCanFlyGroundContact(
      session, timestamp,
      ground_state.has_ground_surface
          ? std::optional<float>(ground_state.ground_surface_z)
          : std::nullopt,
      ground_state.has_vertical_clearance
          ? std::optional<float>(ground_state.vertical_clearance)
          : std::nullopt);

  if ((data_.GetRuntimeFlags2() & kMoveFlag2Unknown4) != 0u) {
    return;
  }

  const bool locally_authoritative =
      IsLocallyControlled() && !data_.IsOnTransport();
  const float collision_height = data_.GetCollisionHeightProduct();

  const float swim_equilibrium_depth = collision_height * 0.75f;
  const float swim_exit_depth = swim_equilibrium_depth - (1.0f / 36.0f);
  const bool swimming = IsSwimming();

  if (!swimming) {
    const MovementInfo &movement = owner_.GetMovementInfo();

    if (locally_authoritative && liquid_depth > swim_equilibrium_depth &&
        !IsRisingBeforeFallApex(movement)) {
      StartSwim(timestamp, true);
    }

    const float splash_depth = collision_height * 0.4f;
    if ((liquid_depth > splash_depth) !=
        (previous_liquid_depth_ > splash_depth)) {
      if (const auto *const dbc = owner_.dbc_loader(); dbc != nullptr) {
        const auto *const race =
            dbc->chr_races().LookupEntry(owner_.State().GetRace());
        if (race != nullptr && race->splash_sound_id != 0u) {
          audio::SoundKitPlaybackOptions options;
          options.sound_type = 8u;
          if (owner_.IsActivePlayer()) {
            options.playback_priority = 110u;
            if (owner_.sound_runtime().IsListenerAtCharacter()) {
              options.volume_scale = 0.65f;
              static_cast<void>(owner_.sound_runtime().PlaySoundKit(
                  race->splash_sound_id, nullptr, nullptr, options));
            } else {
              static_cast<void>(owner_.sound_runtime().PlaySoundKit(
                  race->splash_sound_id, event_position, nullptr, options));
            }
          } else {
            static_cast<void>(owner_.sound_runtime().PlaySoundKit(
                race->splash_sound_id, event_position, nullptr, options));
          }
        }
      }

      UpdateWaterRipples(timestamp, 201u);
    }
    previous_liquid_depth_ = liquid_depth;
  } else {
    const MovementInfo &movement = owner_.GetMovementInfo();
    if ((movement.flags & kMoveFlagAscending) != 0u &&
        liquid_depth - swim_equilibrium_depth <= (1.0f / 3.0f) &&
        owner_.IsActivePlayer()) {

      SendJump(session, timestamp);
    }
    if ((data_.GetRuntimeFlags() & kMoveFlagFlying) == 0u &&
        (!has_liquid || liquid_depth < swim_exit_depth ||
         !locally_authoritative)) {

      StopSwim(timestamp, true);
    }

    if (swimming && locally_authoritative && has_liquid &&
        owner_.IsActiveMover() && session.click_to_move().IsActive()) {
      const auto destination = session.click_to_move().GetDestination();
      const float delta_x = destination.x - position.x;
      const float delta_y = destination.y - position.y;
      const float delta_z = destination.z - position.z;
      const float distance_squared = delta_x * delta_x + delta_y * delta_y +
                                     delta_z * delta_z;
      float target_pitch = data_.GetRuntimePitch();
      if (distance_squared > 2.3841858e-07f) {
        const float inverse_distance = 1.0f / std::sqrt(distance_squared);
        const float direction_x = delta_x * inverse_distance;
        const float direction_y = delta_y * inverse_distance;
        const float direction_z = delta_z * inverse_distance;
        if (std::fabs(direction_x) >= 0.001f ||
            std::fabs(direction_y) >= 0.001f) {
          target_pitch = std::asin(std::clamp(direction_z, -1.0f, 1.0f));
        } else {
          target_pitch = delta_z > 0.0f ? 1.5707964f : -1.5707964f;
        }
      }
      const float pitch_step =
          data_.GetSpeed(kSpeedPitchRate) *
          (static_cast<float>(frame_delta_ms) * kMillisecondsToSeconds);
      bool swim_pitch_steep = false;
      static_cast<void>(InterpolateSwimHeight(
          session, ground_state.liquid_surface_z, position.z, target_pitch,
          pitch_step, static_cast<std::int32_t>(timestamp), &swim_pitch_steep));
    }
  }

  const bool previous_in_water = in_water_;
  in_water_ = IsSwimming() ||
              (locally_authoritative && liquid_depth > swim_equilibrium_depth);
  if (owner_.IsActivePlayer() && previous_in_water != in_water_ &&
      ui::game::detail::RefreshAllActionSlotValidation(session)) {
    ui::game::ScriptEventDispatch::Get().FireActionbarUpdateUsable();
  }
}

void UnitMovementRuntime::UpdateWaterRipples(
    const std::uint32_t timestamp, const std::uint32_t explicit_event) {
  if (g_water_ripple_spawn_callback == nullptr) {
    return;
  }

  const Position position = owner_.GetPosition();
  const float query_position[3] = {position.x, position.y, position.z};
  UnitSoundGroundState ground_state{};
  bool has_ground_state;

  if (post_update_ground_state_stamped_ &&
      post_update_ground_state_tick_ == timestamp &&
      post_update_ground_state_position_[0] == position.x &&
      post_update_ground_state_position_[1] == position.y &&
      post_update_ground_state_position_[2] == position.z) {
    has_ground_state = post_update_ground_state_result_;
    ground_state = post_update_ground_state_;
  } else {
    has_ground_state =
        UnitSound_QueryGroundState(owner_, query_position, ground_state);
  }
  if (!has_ground_state || !ground_state.has_liquid_surface ||
      ground_state.liquid_type_id == 0u) {
    return;
  }

  const MovementInfo& movement = owner_.GetMovementInfo();
  const std::uint32_t directional_flags =
      movement.flags & (kMoveFlagForward | kMoveFlagBackward |
                        kMoveFlagStrafeLeft | kMoveFlagStrafeRight);
  const bool explicitly_triggered = explicit_event != 0u;
  const std::uint8_t mode = explicitly_triggered
                                ? (explicit_event == 201u ? 3u : 0u)
                                : (directional_flags != 0u
                                       ? 2u
                                       : ((movement.flags &
                                           (kMoveFlagTurnLeft |
                                            kMoveFlagTurnRight)) != 0u
                                              ? 1u
                                              : 0u));
  if (explicitly_triggered) {
    next_water_ripple_timestamp_ = 0u;
  }

  const float collision_height = data_.GetCollisionHeightProduct();

  const float maximum_depth = std::max(collision_height * 2.0f, 1.0f);
  const float full_strength_depth = maximum_depth * 0.5f;
  const float liquid_depth = ground_state.liquid_surface_z - position.z;
  if (liquid_depth >= maximum_depth ||
      (next_water_ripple_timestamp_ != 0u &&
       static_cast<std::int32_t>(timestamp - next_water_ripple_timestamp_) < 0)) {
    return;
  }

  const bool use_splash_texture = kWaterRippleSplashModeTable[mode];
  const float current_speed = data_.GetCurrentSpeed();
  float spawn_interval_scale = 1.0f;
  float extent_rate_scale = 1.0f;
  if (use_splash_texture && current_speed > 0.0001f) {
    if (current_speed >= 20.0f) {
      spawn_interval_scale = 0.125f;
      extent_rate_scale = 8.0f;
    } else {
      spawn_interval_scale = 2.5f / current_speed;
      extent_rate_scale = 1.0f / spawn_interval_scale;
    }
  }

  auto& random = core::GetClientStartupAdlerSeedState();
  float initial_extent =
      owner_.GetScale() * (1.0f / 3.0f) *
      (0.9f + 0.2f * foundation::hashing::AdlerSeedNextUnitFloat(random));
  initial_extent = std::clamp(initial_extent, 1.0f / 3.0f, 5.0f / 3.0f);
  float duration =
      0.60f + 0.10f * foundation::hashing::AdlerSeedNextUnitFloat(random);
  float extent_rate =
      (1.0f + 0.5f * foundation::hashing::AdlerSeedNextUnitFloat(random)) *
      extent_rate_scale;

  float depth_factor = 1.0f;
  if (liquid_depth > full_strength_depth) {
    depth_factor =
        ((maximum_depth - liquid_depth) /
             (maximum_depth - full_strength_depth)) *
            0.5f +
        0.5f;
    initial_extent *= depth_factor;
    duration *= depth_factor;
  }
  float opacity_base = (1.0f / 6.0f) * depth_factor;

  if (mode == 0u) {
    opacity_base *= 0.8f;
    extent_rate *= 0.25f;
    initial_extent *= 0.6f;
  }

  float rotation = owner_.GetFacing();
  if (!use_splash_texture) {
    rotation = foundation::hashing::AdlerSeedNextUnitFloat(random) *
               kFullCircleRadians;
  } else if ((directional_flags & kMoveFlagStrafeLeft) != 0u) {
    if ((directional_flags & kMoveFlagForward) != 0u) {
      rotation += 0.7853982f;
    } else if ((directional_flags & kMoveFlagBackward) != 0u) {
      rotation += 2.3561945f;
    } else {
      rotation += 1.5707964f;
    }
  } else if ((directional_flags & kMoveFlagStrafeRight) != 0u) {
    if ((directional_flags & kMoveFlagForward) != 0u) {
      rotation -= 0.7853982f;
    } else if ((directional_flags & kMoveFlagBackward) != 0u) {
      rotation -= 2.3561945f;
    } else {
      rotation -= 1.5707964f;
    }
  } else if ((directional_flags & kMoveFlagBackward) != 0u) {
    rotation += 3.1415927f;
  }

  g_water_ripple_spawn_callback(
      UnitWaterRippleSpawn{
          .position = {position.x, position.y, ground_state.liquid_surface_z},

          .rotation_radians = -rotation,
          .initial_extent = initial_extent,
          .duration_seconds = duration,
          .opacity_base = opacity_base,
          .extent_rate = extent_rate,
          .use_splash_texture = use_splash_texture,

          .use_local_player_pool = owner_.IsActiveMover(),
      },
      g_water_ripple_spawn_context);

  if (!use_splash_texture) {
    next_water_ripple_timestamp_ =
        timestamp + 400u +
        foundation::hashing::AdlerSeedNextBoundedValue(50u, random);
  } else {
    const float interval_ms =
        std::max(0.0f, spawn_interval_scale * depth_factor * 250.0f);
    next_water_ripple_timestamp_ =
        timestamp + static_cast<std::uint32_t>(interval_ms);
  }
}

void UnitMovementRuntime::SeedBodyFacing(const float orientation) {
  body_facing_ = Movement_NormalizeFacing0ToTau(orientation);
  body_facing_speed_ = 0.0f;
  body_facing_samples_.fill(0.0f);

  smooth_body_facing_ = body_facing_;
  smooth_body_facing_velocity_ = 0.0f;
  smooth_body_facing_blend_ = 1.0f;
}

void UnitMovementRuntime::UpdateBodyFacing(float *vehicle_facing) {
  constexpr std::uint32_t kMovementUpdateTurnInputBit = 0x2u;
  constexpr std::uint32_t kTurnFacingMask = kMoveFlagTurnLeft | kMoveFlagTurnRight;
  auto mi = owner_.GetMovementInfo();

  if (owner_.IsActiveMover()) {

    body_facing_ = owner_.GetLocalFacing();

    const bool turn_keys_held = (mi.flags & kTurnFacingMask) != 0;
    const auto *const input = GetInputControlSingleton();
    const std::uint32_t now_ms = openwow::core::GameClock::GetTickCount32();

    bool steering_turn_input = turn_keys_held;
    if (!steering_turn_input && input != nullptr) {
      steering_turn_input = (input->GetControlFlags() & kMaskMoveAndSteer) != 0u;
    }
    if (steering_turn_input) {
      update_flags_ |= kMovementUpdateSteeringTurnInputBit;
    } else {
      update_flags_ &= ~kMovementUpdateSteeringTurnInputBit;
      turn_input_idle_tick_ms_ = now_ms;
    }

    bool keep_turn_input = turn_keys_held;
    if (!keep_turn_input && input != nullptr) {
      keep_turn_input = input->HasRecentMouseDeltaAfterDoubleClickElapsed(now_ms);
    }

    if (keep_turn_input) {
      update_flags_ |= kMovementUpdateTurnInputBit;
    } else {
      update_flags_ &= ~kMovementUpdateTurnInputBit;
    }
  } else {

    float target_facing = owner_.GetLocalFacing();
    if (!ActiveSpellSuppressesBodyFacingTargetTracking(owner_)) {
      const auto *body_facing_target = [&]() -> const CGUnit_C * {
        if (owner_.Animation().StandSelectionInteractionTargetGuid() != 0u) {
          const auto* const objects = owner_.object_manager();
          if (const auto *target = objects != nullptr ? objects->GetUnit(
                  ObjectGuid(owner_.Animation().StandSelectionInteractionTargetGuid())) : nullptr;
              target != nullptr) {
            return target;
          }
        }

        if (const auto target_guid = owner_.State().GetTarget(); !target_guid.IsEmpty()) {
          const auto* const objects = owner_.object_manager();
          if (const auto *target = objects != nullptr ? objects->GetUnit(target_guid) : nullptr;
              target != nullptr) {
            return target;
          }
        }

        if (const auto combo_target_guid = owner_.IsActivePlayer()
                                                 ? owner_.Casts().GetComboTarget()
                                                 : ObjectGuid();
            !combo_target_guid.IsEmpty()) {
          const auto* const objects = owner_.object_manager();
          return objects != nullptr ? objects->GetUnit(combo_target_guid) : nullptr;
        }

        return nullptr;
      }();

      if (body_facing_target != nullptr) {

        target_facing = ComputeFacingAngleToUnit(owner_, *body_facing_target);
        if (const auto *const objects = owner_.object_manager(); objects != nullptr) {
          target_facing = Movement_TransformWorldFacingToLocal(
              *objects, data_.GetTransportGuid(), target_facing);
        }
      }
    }

    float delta = target_facing - body_facing_;
    constexpr float kPi = 3.1415927f;
    constexpr float k2Pi = 6.2831855f;

    if (delta > kPi)
      delta -= k2Pi;
    else if (delta < -kPi)
      delta += k2Pi;

    if (std::fabs(delta) <= 0.01f) {
      body_facing_ = target_facing;
      body_facing_samples_.fill(0.0f);
    } else {
      if ((delta >= 0.0f && body_facing_samples_[0] < 0.0f) ||
          (delta < 0.0f && body_facing_samples_[0] > 0.0f)) {
        body_facing_samples_[0] = 0.0f;
      }

      if (body_facing_samples_[0] == 0.0f) {
        body_facing_samples_[0] = delta;
        body_facing_samples_[1] = delta;
        body_facing_samples_[2] = delta;
        body_facing_samples_[3] = delta;
      } else {
        std::memmove(&body_facing_samples_[1], &body_facing_samples_[0], 3 * sizeof(float));
        body_facing_samples_[0] = delta;
        float avg = (body_facing_samples_[0] + body_facing_samples_[1] + body_facing_samples_[2] +
                     body_facing_samples_[3]) *
                    0.25f;
        if (delta > 0.0f) {
          if (avg > delta)
            avg = delta;
        } else {
          if (avg < delta)
            avg = delta;
        }
        delta = avg;
      }

      body_facing_ += delta * 0.5f;
      while (body_facing_ > kPi)
        body_facing_ -= k2Pi;
      while (body_facing_ < -kPi)
        body_facing_ += k2Pi;
    }
  }

  if (auto *const vehicle_data = owner_.Vehicle().GetVehicleData();
      vehicle_data != nullptr && vehicle::Vehicle_C_HasDbcEntry(vehicle_data)) {
    float offset = vehicle_facing != nullptr
                       ? body_facing_ + *vehicle_facing
                       : owner_.Vehicle().GetVehicleChainWorldFacing(owner_);
    vehicle::Vehicle_C_ForEachPassengerUnit(
        vehicle_data, [&offset](CGUnit_C &passenger) {
          passenger.Movement().UpdateBodyFacing(&offset);
        });
  }
}

void UnitMovementRuntime::ApproachSmoothBodyFacing(const float target,
                                                   const float dt_seconds) {
  constexpr float kPi = math::kAnglePi;
  constexpr float kTwoPi = math::kAngleTwoPi;

  float raw = body_facing_;
  float body = smooth_body_facing_;
  if (body > raw + kPi) {
    raw += kTwoPi;
  } else if (body < raw - kPi) {
    raw -= kTwoPi;
  }

  if (raw > body + kBodyYawMaxLagRadians) {
    body = raw - kBodyYawMaxLagRadians;
  } else if (raw < body - kBodyYawMaxLagRadians) {
    body = raw + kBodyYawMaxLagRadians;
  }

  body = math::NormalizeSignedAngle(body);

  float goal = target;
  if (body > goal + kPi) {
    goal += kTwoPi;
  } else if (body < goal - kPi) {
    goal -= kTwoPi;
  }

  const float x = dt_seconds * kBodyYawSpringOmega;
  const float decay =
      1.0f / (1.0f + x + kBodyYawSpringPadeSquare * x * x +
              kBodyYawSpringPadeCube * x * x * x);
  const float change = body - goal;
  const float impulse =
      (change * kBodyYawSpringOmega + smooth_body_facing_velocity_) * dt_seconds;
  body = (change + impulse) * decay + goal;
  smooth_body_facing_velocity_ =
      decay * (smooth_body_facing_velocity_ - impulse * kBodyYawSpringOmega);

  smooth_body_facing_ = smooth_body_facing_blend_ * body +
                        (1.0f - smooth_body_facing_blend_) * goal;
}

namespace {

void SetUnitTwistBoneOverride(CGUnit_C &owner,
                              const std::uint32_t key_bone_lookup,
                              const float angle_radians,
                              const bool pitch_axis) {
  auto *const m2_system = owner.m2_system();
  const std::uint32_t instance_id = owner.GetPrimaryM2InstanceId();
  if (m2_system == nullptr || instance_id == 0u) {
    return;
  }
  const float half_angle = angle_radians * 0.5f;
  const float sine = std::sin(half_angle);
  const float cosine = std::cos(half_angle);
  const render::RenderVec4 rotation =
      pitch_axis ? render::RenderVec4{0.0f, sine, 0.0f, cosine}
                 : render::RenderVec4{0.0f, 0.0f, sine, cosine};
  (void)m2_system->SetKeyBoneBasisOverride(
      instance_id, key_bone_lookup,
      render::BuildRotationMatrix4x4Quaternion(rotation));
}

void ClearUnitTwistBoneOverride(CGUnit_C &owner,
                                const std::uint32_t key_bone_lookup) {
  auto *const m2_system = owner.m2_system();
  const std::uint32_t instance_id = owner.GetPrimaryM2InstanceId();
  if (m2_system == nullptr || instance_id == 0u) {
    return;
  }
  (void)m2_system->ClearKeyBoneBasisOverride(instance_id, key_bone_lookup);
}

}

void UnitMovementRuntime::SettleBodyTwistBoneOverrides() {
  owner_.Animation().SetBodyYawTurnLatches(false, false);
  const auto *const passenger = owner_.Vehicle().GetVehiclePassengerComponent();
  const auto *const seat_entry =
      passenger != nullptr ? passenger->GetSeatEntry() : nullptr;
  const bool seat_spine_pitch =
      seat_entry != nullptr &&
      (seat_entry->flags & kVehicleSeatFlagSpinePitch) != 0u;
  if (seat_spine_pitch) {
    SetUnitTwistBoneOverride(owner_, kKeyBoneLookupSpineLow,
                             -data_.GetRuntimePitch(), true);
    ClearUnitTwistBoneOverride(owner_, kKeyBoneLookupHead);
    body_twist_override_active_ = true;
    return;
  }
  if (!body_twist_override_active_) {
    return;
  }
  ClearUnitTwistBoneOverride(owner_, kKeyBoneLookupSpineLow);
  ClearUnitTwistBoneOverride(owner_, kKeyBoneLookupHead);
  body_twist_override_active_ = false;
}

void UnitMovementRuntime::UpdateSmoothBodyFacing(const float dt_seconds) {
  constexpr std::uint32_t kStrafeMask = kMoveFlagStrafeLeft | kMoveFlagStrafeRight;
  constexpr std::uint32_t kForwardBackwardMask =
      kMoveFlagForward | kMoveFlagBackward;

  constexpr std::uint32_t kLocomotingMask =
      kForwardBackwardMask | kMoveFlagFalling;
  constexpr std::uint32_t kSwimOrFlyMask = kMoveFlagSwimming | kMoveFlagFlying;

  const std::uint32_t flags = owner_.GetMovementInfo().flags;

  const auto *const passenger = owner_.Vehicle().GetVehiclePassengerComponent();
  const auto *const seat_entry =
      passenger != nullptr ? passenger->GetSeatEntry() : nullptr;
  const auto transition_state =
      passenger != nullptr ? passenger->GetTransitionState()
                           : VehiclePassengerTransitionType::kAttached;
  const bool snap_to_raw_facing =
      owner_.GetHealth() == 0u ||
      (passenger != nullptr &&
       (transition_state == VehiclePassengerTransitionType::kTransferWithPos ||
        transition_state == VehiclePassengerTransitionType::kEject)) ||
      (flags & kSwimOrFlyMask) != 0u ||
      (seat_entry != nullptr &&
       (seat_entry->flags & kVehicleFlagsPinRenderedBodyYaw) != 0u) ||
      (owner_.Animation().GetEmoteInternalFlags() & kAnimationTwistBoneFlags) == 0u;

  if (snap_to_raw_facing) {
    smooth_body_facing_ = body_facing_;
    smooth_body_facing_velocity_ = 0.0f;
    smooth_body_facing_blend_ = 0.0f;

    SettleBodyTwistBoneOverrides();
    return;
  }

  if ((flags & kStrafeMask) == 0u) {
    if ((flags & kLocomotingMask) == 0u) {

      if (smooth_body_facing_blend_ < 1.0f) {
        smooth_body_facing_blend_ += dt_seconds * kBodyYawBlendRatePerSecond;
      }
    } else if (smooth_body_facing_blend_ <= 0.0f) {

      smooth_body_facing_ = body_facing_;
      smooth_body_facing_velocity_ = 0.0f;
    } else {

      smooth_body_facing_blend_ -= dt_seconds * kBodyYawBlendRatePerSecond;
      if (smooth_body_facing_blend_ > 1.0f) {
        smooth_body_facing_blend_ = 1.0f;
      }
      ApproachSmoothBodyFacing(body_facing_, dt_seconds);
    }
  } else {

    float offset = (flags & kForwardBackwardMask) == 0u
                       ? kStrafeBodyOffsetPureRadians
                       : kStrafeBodyOffsetDiagonalRadians;

    constexpr std::uint32_t kStrafeSignMask =
        kMoveFlagBackward | kMoveFlagStrafeLeft;
    const std::uint32_t sign_bits = flags & kStrafeSignMask;
    if (sign_bits == kStrafeSignMask || sign_bits == 0u) {
      offset = -offset;
    }

    const float body = smooth_body_facing_;
    smooth_body_facing_blend_ = 1.0f;
    const float target = math::NormalizeSignedAngle(
        math::NormalizeSignedAngle(body_facing_ - body + offset) + body);
    ApproachSmoothBodyFacing(target, dt_seconds);
  }

  const float error = math::NormalizeSignedAngle(body_facing_ - smooth_body_facing_);
  const float magnitude = std::fabs(error);
  if (magnitude < kBodyYawArrivedEpsilon) {

    SettleBodyTwistBoneOverrides();
    return;
  }

  float root_step = 0.0f;
  if (magnitude > kBodyYawRootHandoffRadians) {
    root_step =
        math::CopySignFloat(magnitude - kBodyYawRootHandoffRadians, error);
  }

  if ((flags & kStrafeMask) == 0u &&
      (update_flags_ & kMovementUpdateSteeringTurnInputBit) == 0u) {
    const std::uint32_t elapsed_ms =
        openwow::core::GameClock::GetTickCount32() - turn_input_idle_tick_ms_;
    const float allowance = static_cast<float>(elapsed_ms) *
                            kMillisecondsToSeconds *
                            data_.GetSpeed(kSpeedTurnRate) *
                            kBodyYawCatchUpTurnRateScale;
    root_step +=
        math::CopySignFloat(std::min(allowance, magnitude), error);
  }

  smooth_body_facing_ =
      math::NormalizeSignedAngle(smooth_body_facing_ + root_step);

  if ((flags & kBodyYawTurnLatchSuppressMask) == 0u) {
    owner_.Animation().SetBodyYawTurnLatches(root_step > kBodyTwistClearEpsilon,
                                             root_step < -kBodyTwistClearEpsilon);
  }

  const float remaining =
      math::NormalizeSignedAngle(body_facing_ - smooth_body_facing_);
  float remaining_magnitude = std::fabs(remaining);
  if (remaining_magnitude < kBodyTwistClearEpsilon) {
    SettleBodyTwistBoneOverrides();
    return;
  }
  const std::uint32_t emote_flags = owner_.Animation().GetEmoteInternalFlags();

  const bool mounted = owner_.Mount().OverlayM2InstanceId() != 0u;
  if (!mounted && (emote_flags & kAnimationTwistBoneSpineFlag) != 0u) {
    float amount = remaining_magnitude * 0.5f;
    if (amount > kBodyTwistPerBoneClampRadians) {
      amount = kBodyTwistPerBoneClampRadians;
    }
    SetUnitTwistBoneOverride(owner_, kKeyBoneLookupSpineLow,
                             math::CopySignFloat(amount, remaining),
                             false);
    body_twist_override_active_ = true;
    remaining_magnitude -= amount;
  }
  if ((emote_flags & kAnimationTwistBoneHeadFlag) != 0u) {
    const float amount =
        std::min(remaining_magnitude, kBodyTwistPerBoneClampRadians);
    SetUnitTwistBoneOverride(owner_, kKeyBoneLookupHead,
                             math::CopySignFloat(amount, remaining),
                             false);
    body_twist_override_active_ = true;
  }
}

void UnitMovementRuntime::HandleArrival() {
  owner_.Interaction().HandleMovementArrival();
}

float UnitMovementRuntime::WorldSmoothBodyFacing() const {
  const auto *const objects = owner_.object_manager();
  if (objects == nullptr) {
    return smooth_body_facing_;
  }
  return Movement_TransformLocalFacingToWorld(
      *objects, data_.GetTransportGuid(), smooth_body_facing_);
}

void UnitMovementRuntime::OffsetBodyFacingAngles(float delta) {
  body_facing_ = math::NormalizePositiveAngle(delta + body_facing_);
  smooth_body_facing_ = math::NormalizePositiveAngle(delta + smooth_body_facing_);
}

bool UnitMovementRuntime::ApplyMovementUpdate(const MovementOnlyUpdate &update) {
  const bool parent_allows_transition_gate =
      !data_.HasParentMovement() ||
      (data_.GetParentMovementFlags() & kParentMovementAllowStopFlag) != 0u;
  if (!update.has_resolved_presentation_tick && update.movement.IsLiving() &&
      parent_allows_transition_gate && HasPendingVehicleSeatChange(owner_)) {
    return false;
  }

  if (!update.has_resolved_presentation_tick && update.movement.IsLiving()) {
    (void)data_.ResolveServerMovementTiming(
        update.client_receive_tick_ms, update.movement.movement.time,
        has_movement_update_tick_ ? movement_timeline_.current_tick
                                  : update.client_receive_tick_ms,
        true);
  }

  CommitMovementUpdate(update, MovementUpdateOrigin::kAuthoritative);
  return true;
}

void UnitMovementRuntime::ApplyPredictedMovement(
    const MovementInfo &movement_info) {
  MovementOnlyUpdate update;
  update.guid = owner_.GetGuid();
  update.movement = owner_.GetMovementUpdate();
  update.movement.update_flags |=
      static_cast<std::uint16_t>(kUpdateFlagLiving);
  update.movement.movement = movement_info;
  CommitMovementUpdate(update, MovementUpdateOrigin::kPredicted);
}

void UnitMovementRuntime::CommitMovementUpdate(
    const MovementOnlyUpdate &update, const MovementUpdateOrigin origin) {
  const bool is_authoritative =
      origin == MovementUpdateOrigin::kAuthoritative;
  const MovementInfo previous_movement = owner_.GetMovementInfo();
  const auto previous_movement_flags = previous_movement.flags;
  (void)owner_.CGObject_C::ApplyMovementUpdate(update);

  if (spline_movement_pose_owned_ &&
      (!owner_.position_.IsLiving() || !owner_.position_.movement.HasSplineEnabled() ||
       !owner_.position_.spline.active)) {
    ClearSplineMovementPoseOwnership();
  }

  if (owner_.position_.IsLiving()) {
    if (is_authoritative) {
      data_.SyncAuthoritativeMovementInfo(
          owner_.position_.movement,
          !update.has_resolved_presentation_tick);
    } else {
      data_.SyncPresentedMovementInfo(owner_.position_.movement);
    }
    for (std::size_t index = 0; index < kMaxSpeeds; ++index) {
      (void)data_.SetSpeed(static_cast<SpeedType>(index),
                           owner_.position_.speeds[index]);
    }
    if (is_authoritative && !owner_.IsActiveMover()) {
      data_.RefreshQueuedMovementPreview(update.client_receive_tick_ms);
    }
  }
  owner_.Animation().UpdatePendingFallAnimation(previous_movement_flags,
                                                 owner_.GetMovementInfo().flags);
  owner_.Animation().HandleMovementAnimation(previous_movement_flags,
                                              owner_.GetMovementInfo().flags);

}

void UnitMovementRuntime::AdvanceMovementStep(
    WorldSession &session, const std::uint32_t timestamp,
    const std::uint32_t step_ms, const bool commit_owner) {
  const auto commit_if_requested = [this, &session, timestamp, commit_owner] {
    if (commit_owner) {
      CommitMovementRuntimeState(session, timestamp);
    }
  };

  if (TryDetachTransportParentForMovementStep(session)) {
    commit_if_requested();
    return;
  }

  const auto start = data_.GetTransformPosition();
  const auto start_fall_time = data_.GetRuntimeFallTime();
  data_.AdvanceKinematics(step_ms);
  const auto integrated_transform = data_.GetTransformPosition();

  const auto restore_pre_step_kinematics = [this, &start, start_fall_time] {
    data_.SetTransformPosition(start[0], start[1], start[2]);
    data_.SetRuntimeFallTime(start_fall_time);
  };

  auto integrated = MovementInfoFromRuntimeData(
      data_, owner_.GetMovementInfo(), session.objects(), timestamp);
  const std::shared_ptr<MovementCollisionSolver> source_solver =
      session.GetMovementCollisionSolver();
  if (source_solver == nullptr) {
    collision_solver_source_.reset();
    collision_solver_.reset();
  } else if (collision_solver_source_.lock() != source_solver) {
    collision_solver_source_ = source_solver;
    collision_solver_ = source_solver->CreateIndependentSolver();
  }
  const std::shared_ptr<MovementCollisionSolver>& solver = collision_solver_;
  const float radius = data_.GetCollisionHalfWidth();
  const float height = data_.GetCollisionHeightProduct();

  if (!solver || !solver->IsBound() || data_.HasTransferredMovementControl() ||
      !std::isfinite(radius) || !std::isfinite(height) || radius <= 0.0f ||
      height <= 0.0f) {
    LogTransportSolveBail(session, !solver || !solver->IsBound()
                                       ? "solver_unbound"
                                       : data_.HasTransferredMovementControl()
                                             ? "control_transferred"
                                             : "non_finite_extent");
    restore_pre_step_kinematics();
    commit_if_requested();
    return;
  }

  std::optional<MovementParentTransform> parent_transform;
  if (data_.IsOnTransport()) {
    MovementParentTransform resolved;
    if (!ResolveMovementParentTransform(session.objects(), resolved)) {
      LogTransportSolveBail(session, "parent_transform_unresolved");
      restore_pre_step_kinematics();
      commit_if_requested();
      return;
    }

    if (!session.IsTransportParentCollisionGeometryReady(
            data_.GetTransportGuid())) {
      LogTransportSolveBail(session, "render_asset_not_ready");
      restore_pre_step_kinematics();
      commit_if_requested();
      return;
    }
    parent_transform = resolved;
  } else {
    ForgetMovementParentTransform();
  }

  const C3Vector displacement{
      integrated_transform[0] - start[0],
      integrated_transform[1] - start[1],
      integrated_transform[2] - start[2],
  };

  const std::uint32_t runtime_flags = data_.GetRuntimeFlags();
  const std::uint16_t runtime_flags2 = data_.GetRuntimeFlags2();
  const bool has_parent = data_.HasParentMovement();
  const std::uint32_t parent_flags = data_.GetParentMovementFlags();
  const bool parent_allows_stop =
      !has_parent || (parent_flags & kParentMovementAllowStopFlag) != 0u;
  const auto retail_simple_collision_route = [&]() {
    if (parent_allows_stop) {
      if ((runtime_flags2 & static_cast<std::uint16_t>(kMoveFlag2Unknown4)) !=
              0u ||
          (runtime_flags & kMoveFlagDisableGravity) != 0u ||
          (runtime_flags & (kMoveFlagSwimming | kMoveFlagFlying)) != 0u) {
        return true;
      }
      if ((runtime_flags2 & 0x0080u) == 0u) {
        return has_parent &&
               (parent_flags & kParentMovementAllowStopFlag) == 0u &&
               (parent_flags & movement::CMovementData::kParentFallingSplineFlag) !=
                   0u;
      }
      if (!has_parent) {
        return false;
      }
      if ((parent_flags & kParentMovementAllowStopFlag) != 0u ||
          (parent_flags & movement::CMovementData::kParentParabolicSplineFlag) ==
              0u) {
        return (parent_flags & kParentMovementAllowStopFlag) == 0u &&
               (parent_flags & movement::CMovementData::kParentFallingSplineFlag) !=
                   0u;
      }
      return true;
    }

    if ((parent_flags & movement::CMovementData::kParentFlyingSplineFlag) !=
        0u) {
      return true;
    }
    if ((parent_flags & movement::CMovementData::kParentFallingSplineFlag) !=
        0u) {

    } else if ((runtime_flags2 & static_cast<std::uint16_t>(
                                  kMoveFlag2Unknown4)) != 0u ||
               (runtime_flags & kMoveFlagDisableGravity) != 0u) {
      return true;
    }
    if ((runtime_flags & (kMoveFlagSwimming | kMoveFlagFlying)) != 0u) {
      return true;
    }
    if ((runtime_flags2 & 0x0080u) == 0u) {

      return has_parent &&
             (parent_flags & kParentMovementAllowStopFlag) == 0u &&
             (parent_flags & movement::CMovementData::kParentFallingSplineFlag) !=
                 0u;
    }
    if ((parent_flags & kParentMovementAllowStopFlag) != 0u ||
        (parent_flags & movement::CMovementData::kParentParabolicSplineFlag) ==
            0u) {
      return (parent_flags & kParentMovementAllowStopFlag) == 0u &&
             (parent_flags & movement::CMovementData::kParentFallingSplineFlag) !=
                 0u;
    }
    return true;
  };
  const bool simple_collision = retail_simple_collision_route();

  MovementCollisionMode mode = MovementCollisionMode::kGround;
  if (simple_collision) {
    mode = MovementCollisionMode::kSimpleCollision;
  } else if ((runtime_flags & kMoveFlagFalling) != 0u) {
    mode = MovementCollisionMode::kFalling;
  } else if ((runtime_flags & kMoveFlagHover) != 0u) {
    mode = MovementCollisionMode::kSpecial;
  }

  const bool allow_secondary_pass =
      (runtime_flags2 & static_cast<std::uint16_t>(kMoveFlag2Unknown4)) == 0u &&
      (runtime_flags & kMoveFlagDisableGravity) == 0u &&
      !data_.HasNonExemptSplineFlag(
          movement::CMovementData::kParentFlyingSplineFlag);

  const bool parent_blocks_gap_fall =
      data_.HasParentMovement() &&
      (data_.GetParentMovementFlags() &
       (movement::CMovementData::kParentFallingSplineFlag |
        movement::CMovementData::kParentParabolicSplineFlag)) != 0u;

  constexpr std::uint32_t kSpecialFallBlockedMovementFlags =
      kMoveFlagFlying | kMoveFlagSwimming | kMoveFlagFalling;
  const bool allow_special_fall_transition =
      (runtime_flags2 & kMoveFlag2Unknown4) == 0u &&
      (runtime_flags & kMoveFlagDisableGravity) == 0u &&
      (!data_.HasParentMovement() ||
       (data_.GetParentMovementFlags() & kParentMovementAllowStopFlag) != 0u ||
       (data_.GetParentMovementFlags() &
        movement::CMovementData::kParentFlyingSplineFlag) == 0u) &&
      (runtime_flags & kSpecialFallBlockedMovementFlags) == 0u &&
      !parent_blocks_gap_fall;

  const auto collision_mask = data_.BuildTerrainIntersectFlags(
      {.is_navigable_as_player = IsNavigableAsPlayer(),
       .can_control_character = CanControlCharacter(),
       .is_ghost_player = IsGhostPlayerDescriptorPair()});
  MovementCollisionBody body{
      .position = {start[0], start[1], start[2]},
      .radius = radius,
      .height = height,

      .step_height = IsNavigableAsPlayer() ? data_.GetCollisionScaleRatio()
                                           : kNonPlayerStepAllowance,
      .hover_height = owner_.State().GetHoverHeight(),
      .mode = mode,
      .collision_mask = collision_mask,
      .include_secondary_facets =
          (runtime_flags & kMoveFlagSwimming) != 0u,
      .allow_secondary_pass = allow_secondary_pass,
      .allow_fall_transition =
          AllowsTerrainGapFallingFromMovementFlags(runtime_flags) &&
          !parent_blocks_gap_fall,
      .trigger_ascent_jump_on_liquid_contact =
          (runtime_flags & kMoveFlagAscending) != 0u,
      .permissive_walkable_slope = !IsNavigableAsPlayer(),
      .allow_special_fall_transition = allow_special_fall_transition,
      .stepping = collision_stepping_,

      .step_reference_z = data_.GetCumulativeCollisionZ(),

      .parent = parent_transform,
  };
  const MovementCollisionStep collision_step{
      .displacement = displacement,
      .duration_ms = step_ms,
      .movement_speed = data_.GetCurrentSpeed(),
      .vertical_speed = data_.GetRuntimeJumpZSpeed(),
      .fall_time_ms = start_fall_time,
      .initial_direction = [&data = data_] {
        const auto& direction = data.GetForwardDirection();
        return C3Vector{direction[0], direction[1], direction[2]};
      }(),

      .fall_start_z = data_.GetRuntimeFallStartZ(),
      .safe_fall = (runtime_flags & kMoveFlagFallingSlow) != 0u,
      .falling = mode == MovementCollisionMode::kFalling,

      .directional_input =
          (runtime_flags & (kMoveFlagForward | kMoveFlagBackward |
                            kMoveFlagStrafeLeft | kMoveFlagStrafeRight)) != 0u,
  };
  const MovementCollisionResult collision =
      solver->Solve(body, collision_step);

  collision_stepping_ = body.stepping;
  data_.SetCumulativeCollisionZ(body.step_reference_z);

  C3Vector solved_transform{integrated_transform[0], integrated_transform[1],
                            integrated_transform[2]};
  if (collision.status == MovementCollisionStatus::kCancelled ||
      collision.status == MovementCollisionStatus::kInvalidInput) {
    solved_transform = {start[0], start[1], start[2]};
  } else if (collision.status != MovementCollisionStatus::kQueryFailed) {
    solved_transform = body.position;
  }
  PublishSolvedTransformPosition(session.objects(), solved_transform,
                                 parent_transform.has_value(), integrated);

  if (collision.last_contact.has_value()) {
    data_.SetGroundSlopeZ(collision.last_contact->surface_normal.z);
  }
  if (collision.current_speed_update.has_value() &&
      collision.horizontal_direction_update.has_value()) {
    data_.ApplyAirborneCollisionKinematics(
        *collision.current_speed_update,
        collision.horizontal_direction_update->x,
        collision.horizontal_direction_update->y);
  } else if (collision.current_speed_update.has_value()) {
    data_.SetCurrentSpeedFromAirborneCollision(
        *collision.current_speed_update);
  }
  if (mode == MovementCollisionMode::kFalling &&
      collision.consumed_ms < step_ms && !collision.falling_far_contact) {

    integrated.fall_time =
        integrated.fall_time - std::min(integrated.fall_time,
                                       step_ms - collision.consumed_ms);
  }

  if (collision.transitioned_to_falling &&
      AllowsTerrainGapFallingFromMovementFlags(data_.GetRuntimeFlags()) &&
      (integrated.flags & kMoveFlagFalling) == 0u) {

    data_.SetRuntimeFallStartZ(solved_transform.z);
    integrated.flags =
        (integrated.flags & ~0x06E01000u) | kMoveFlagFalling;
    if ((integrated.flags2 & kMoveFlag2AlwaysAllowPitching) == 0u) {
      integrated.flags &= ~(kMoveFlagPitchUp | kMoveFlagPitchDown);
    }
    integrated.fall_time = 0u;
    integrated.jump = {};
  }
  if (collision.ascent_jump_contact) {

    if (data_.TryStartJump(false)) {
      integrated.flags = data_.GetRuntimeFlags() & 0x7FFFFFFFu;
      integrated.fall_time = data_.GetRuntimeFallTime();
      integrated.jump.z_speed = data_.GetRuntimeJumpZSpeed();
      integrated.jump.cos_angle = data_.GetRuntimeJumpCosAngle();
      integrated.jump.sin_angle = data_.GetRuntimeJumpSinAngle();
      integrated.jump.xy_speed = data_.GetRuntimeJumpXYSpeed();
    }
  }
  if (collision.falling_far_contact) {
    integrated.flags |= kMoveFlagFallingFar;
    integrated.fall_time = 0u;
    integrated.jump = {};
    data_.SetRuntimeFallStartZ(solved_transform.z);
  }
  if (mode == MovementCollisionMode::kFalling && !collision.landed &&
      (integrated.flags & (kMoveFlagFallingFar | kMoveFlagFallingSlow)) ==
          0u) {

    constexpr std::uint32_t kFallingFarZeroLaunchDelayMs = 500u;
    constexpr float kFallingFarLaunchedDropDistance = 0.11111111f;
    const bool falling_with_launch_velocity =
        (integrated.flags & kMoveFlagFalling) != 0u &&
        integrated.jump.z_speed != 0.0f;
    const bool falling_far =
        falling_with_launch_velocity
            ? data_.GetRuntimeFallStartZ() - kFallingFarLaunchedDropDistance >=
                  solved_transform.z
            : integrated.fall_time >= kFallingFarZeroLaunchDelayMs;
    if (falling_far) {
      integrated.flags |= kMoveFlagFallingFar;
    }
  }
  if (collision.landed) {

    integrated.flags &= ~(kMoveFlagFalling | kMoveFlagFallingFar);
  }

  data_.SyncPresentedMovementInfo(integrated);
  if (collision.landed) {

    data_.UpdateDirectionConditional();
    (void)data_.RecalculateStateFlags();

    constexpr float kFallLandMinimumDistance = 0.027777778f;
    const float fall_distance = ComputeOrdinaryFallDistance(
        data_.GetRuntimeFallStartZ(), solved_transform.z,
        data_.GetRuntimeJumpZSpeed());
    const bool landing_announced =
        owner_.State().HasSpellStateFlags(kSpellStateWireAnnouncedFalling) ||
        fall_distance > kFallLandMinimumDistance;
    if (landing_announced) {
      data_.QueueFallLand(timestamp);
    }

    pending_landing_animation_suppressed_ = !landing_announced;

    if (deferred_auto_release_pending_) {
      deferred_auto_release_pending_ = false;
      if (owner_.IsActiveMover()) {
        session.interaction().SendRepopRequest(true);
      }
    }
  }
  if (collision.reset_requested || collision.state_snapshot_required) {
    data_.SnapshotStateForDirectionRecompute();
  }
  AdoptTransportParentFromGroundContact(session, collision);
  commit_if_requested();
}

void UnitMovementRuntime::ArmDeferredAutoRelease() {
  deferred_auto_release_pending_ = true;
}

void UnitMovementRuntime::LogTransportSolveBail(WorldSession &session,
                                                 const char *const reason) {

  if (!data_.IsOnTransport() || !owner_.IsActiveMover()) {
    return;
  }
  constexpr std::uint32_t kTransportSolveBailLogIntervalMs = 1000u;
  const std::uint32_t now_ms = session.CurrentClientTimeMs();
  if (now_ms - last_transport_solve_bail_log_ms_ <
      kTransportSolveBailLogIntervalMs) {
    return;
  }
  last_transport_solve_bail_log_ms_ = now_ms;
  const auto local = data_.GetTransformPosition();
  diagnostics::Log(
      diagnostics::LogLevel::kInfo,
      std::string("transport_solve_bail: reason=") + reason +
          " guid=" + std::to_string(data_.GetTransportGuid()) + " local=(" +
          std::to_string(local[0]) + "," + std::to_string(local[1]) + "," +
          std::to_string(local[2]) + ")");
}

bool UnitMovementRuntime::TryDetachTransportParentForMovementStep(
    WorldSession &session) {

  if (!data_.IsOnTransport()) {
    return false;
  }

  if (!IsActivePlayerMover() ||
      (data_.HasParentMovement() &&
       (data_.GetParentMovementFlags() & kParentMovementAllowStopFlag) == 0u)) {
    return false;
  }

  auto &objects = session.objects();
  const std::uint64_t parent_guid = data_.GetTransportGuid();
  const auto *const parent =
      objects.GetGameObject(ObjectGuid(parent_guid));
  if (parent == nullptr) {

    constexpr std::uint8_t kNoTransportSeat = 0xffu;
    const auto result = ApplyTransportChange(
        session, 0u, kNoTransportSeat, false);
    if (result != TransportChangeResult::kApplied) {
      return false;
    }
    if (owner_.IsActiveMover()) {
      session.movement().ApplyAuthoritativeMovementInfo(
          owner_.GetMovementInfo());
    }
    return true;
  }

  const std::array<float, 3> local_position = data_.GetTransformPosition();
  if (parent->ContainsLocalPoint(local_position)) {
    return false;
  }

  constexpr std::uint8_t kNoTransportSeat = 0xffu;
  const auto result =
      ApplyTransportChange(session, 0u, kNoTransportSeat,
                           false);
  if (result != TransportChangeResult::kApplied) {
      return false;
  }
  if (owner_.IsActiveMover()) {
    session.movement().ApplyAuthoritativeMovementInfo(owner_.GetMovementInfo());
  }

  return true;
}

void UnitMovementRuntime::ForgetMovementParentTransform() noexcept {
  movement_parent_guid_ = 0u;
  has_movement_parent_matrix_ = false;
}

bool UnitMovementRuntime::ResolveMovementParentTransform(
    const ObjectManager &objects, MovementParentTransform &out) {
  const std::uint64_t parent_guid = data_.GetTransportGuid();
  if (parent_guid == 0u) {
    ForgetMovementParentTransform();
    return false;
  }

  std::array<float, 16> parent_to_world_4x4{};
  if (Movement_GetObjectTransform(objects, parent_guid,
                                  parent_to_world_4x4.data()) == 0) {

    ForgetMovementParentTransform();
    return false;
  }

  if (!has_movement_parent_matrix_ || movement_parent_guid_ != parent_guid ||
      openwow::math::row_major_mat4x4::NotEqualExact(
          parent_to_world_4x4.data(), movement_parent_matrix_.data())) {
    movement_parent_matrix_ = parent_to_world_4x4;
    movement_parent_guid_ = parent_guid;
    has_movement_parent_matrix_ = true;

    ++movement_parent_revision_;
  }

  std::array<float, 16> world_to_parent_4x4{};
  openwow::math::row_major_mat4x4::BuildInverseRigidTransform4x4(
      world_to_parent_4x4.data(), parent_to_world_4x4.data());

  const auto pack3x3 = [](const std::array<float, 16> &matrix4x4) {
    return std::array<float, 9>{matrix4x4[0], matrix4x4[4], matrix4x4[8],
                                matrix4x4[1], matrix4x4[5], matrix4x4[9],
                                matrix4x4[2], matrix4x4[6], matrix4x4[10]};
  };

  out.parent_to_world = pack3x3(parent_to_world_4x4);
  out.world_to_parent = pack3x3(world_to_parent_4x4);
  out.parent_origin_world = {parent_to_world_4x4[12], parent_to_world_4x4[13],
                             parent_to_world_4x4[14]};
  out.revision = movement_parent_revision_;
  return true;
}

void UnitMovementRuntime::PublishSolvedTransformPosition(
    const ObjectManager &objects, const C3Vector &transform_position,
    const bool has_parent, MovementInfo &movement_info) const {
  if (!has_parent) {
    movement_info.x = transform_position.x;
    movement_info.y = transform_position.y;
    movement_info.z = transform_position.z;
    return;
  }

  movement_info.transport.offset_x = transform_position.x;
  movement_info.transport.offset_y = transform_position.y;
  movement_info.transport.offset_z = transform_position.z;

  const float local_position[3] = {transform_position.x, transform_position.y,
                                   transform_position.z};
  float world_position[3]{};
  Passenger_TransformLocalToWorldPosition(objects, data_.GetTransportGuid(),
                                          world_position, local_position);
  movement_info.x = world_position[0];
  movement_info.y = world_position[1];
  movement_info.z = world_position[2];
}

void UnitMovementRuntime::AdoptTransportParentFromGroundContact(
    WorldSession &session, const MovementCollisionResult &collision) {

  if (!collision.last_contact.has_value()) {
    return;
  }
  const std::uint64_t contact_guid = collision.last_contact->owner_guid;
  if (contact_guid == 0u || contact_guid == data_.GetTransportGuid()) {
    return;
  }
  if (!IsActivePlayerMover() ||
      (data_.HasParentMovement() &&
       (data_.GetParentMovementFlags() & kParentMovementAllowStopFlag) == 0u)) {
    return;
  }

  constexpr std::uint8_t kNoTransportSeat = 0xffu;
  const auto result = ApplyTransportChange(
      session, contact_guid, kNoTransportSeat, false);
  if (result != TransportChangeResult::kApplied) {
    return;
  }
  if (owner_.IsActiveMover()) {
    session.movement().ApplyAuthoritativeMovementInfo(owner_.GetMovementInfo());
  }
}

void UnitMovementRuntime::ApplyActiveMoverMovementUpdate(
    WorldSession &session, const MovementOnlyUpdate &update) {
  const MovementInfo previous_movement = owner_.GetMovementInfo();
  if (!ApplyMovementUpdate(update)) {
    return;
  }
  session.objects().SynchronizeUnitTransportPassengerMembership(
      owner_, previous_movement);
  if (!update.has_resolved_presentation_tick) {
    SynchronizeAuthoritativeState(session, update.movement,
                                  update.client_receive_tick_ms);
  }
  session.movement().ApplyAuthoritativeMovementInfo(owner_.GetMovementInfo());
}

void UnitMovementRuntime::ApplyActiveMoverTeleportAck(
    WorldSession &session, MovementOnlyUpdate update) {

  session.click_to_move().Stop();

  auto &incoming = update.movement.movement;
  const MovementInfo current = owner_.GetMovementInfo();

  std::uint16_t flags2 = static_cast<std::uint16_t>(
      (data_.GetRuntimeFlags2() &
       ~movement::CMovementData::kTeleportPacketOwnedFlags2Mask) |
      (incoming.flags2 &
       movement::CMovementData::kTeleportPacketOwnedFlags2Mask));
  flags2 = static_cast<std::uint16_t>(
      flags2 & ~movement::CMovementData::kInterpolatedFlags2Mask);

  std::uint32_t flags =
      movement::CMovementData::ResolveTeleportArrivalFlags(data_.GetRuntimeFlags());

  const bool releases_spline_parent =
      data_.HasParentMovement() &&
      (data_.GetParentMovementFlags() & kParentMovementAllowStopFlag) == 0u;
  if (releases_spline_parent) {
    flags2 = static_cast<std::uint16_t>(
        flags2 & ~movement::CMovementData::kHasParentMovementUpdateBitMask);
    flags &= ~kMoveFlagSplineEnabled;
    session.movement_spline_mgr().CancelSpline(owner_.GetGuid().GetRawValue());
  }

  incoming.flags = flags;
  incoming.flags2 = flags2;

  incoming.pitch = 0.0f;

  incoming.fall_time = current.fall_time;
  incoming.jump = current.jump;

  ApplyActiveMoverMovementUpdate(session, update);
}

void UnitMovementRuntime::ClearSplineMovementPoseOwnership() {
  const auto previous_flags =
      owner_.GetMovementInfo().flags |
      (spline_locomotion_active_
           ? (spline_locomotion_backward_ ? kMoveFlagBackward
                                          : kMoveFlagForward)
           : 0u);
  if (!owner_.IsActiveMover()) {

    owner_.position_.movement.flags &= ~kLocomotionStateMask;
    data_.SyncPresentedMovementInfo(owner_.position_.movement);
  }

  data_.ClearParentMovement();
  spline_movement_pose_owned_ = false;
  spline_locomotion_active_ = false;
  spline_locomotion_backward_ = false;
  spline_locomotion_flags_ = 0u;
  spline_locomotion_arc_length_ = 0.0f;
  spline_locomotion_duration_ms_ = 0u;
  spline_pose_waiting_for_parent_ = false;
  spline_coordinate_parent_ = {};
  spline_coordinate_parent_seat_ = -1;
  spline_local_position_ = {};
  spline_local_facing_ = 0.0f;
  owner_.Animation().HandleMovementAnimation(
      previous_flags, owner_.GetMovementInfo().flags);
}

bool UnitMovementRuntime::TryComposeSplineParentPose() {
  if (spline_coordinate_parent_.IsEmpty()) {
    return true;
  }
  const auto *const objects = owner_.object_manager();
  if (objects == nullptr) {
    return false;
  }

  float parent_transform[16] = {};
  if (Movement_GetObjectTransform(
          *objects, spline_coordinate_parent_.GetRawValue(), parent_transform) == 0) {
    return false;
  }

  const float local[3] = {spline_local_position_.x, spline_local_position_.y,
                          spline_local_position_.z};
  float world[3] = {};
  Passenger_TransformLocalPointToWorld(world, local, parent_transform);
  owner_.position_.movement.x = world[0];
  owner_.position_.movement.y = world[1];
  owner_.position_.movement.z = world[2];
  owner_.position_.movement.orientation = Movement_TransformLocalFacingToWorld(
      *objects, spline_coordinate_parent_.GetRawValue(), spline_local_facing_);
  spline_pose_waiting_for_parent_ = false;
  return true;
}

bool UnitMovementRuntime::TrySettleSplineMovementPoseOwnership() {
  if (!spline_movement_pose_owned_) {
    return true;
  }
  if (spline_pose_waiting_for_parent_ && !TryComposeSplineParentPose()) {
    return false;
  }
  ClearSplineMovementPoseOwnership();
  return true;
}

void UnitMovementRuntime::StopLocomotionForDeath(WorldSession &session) {
  auto &spline_manager = session.movement_spline_mgr();
  if (const auto *const spline =
          spline_manager.GetSpline(owner_.GetGuid().GetRawValue());
      spline != nullptr) {

    ApplySplineMovementPose(
        spline->GetCurrentPosition(), spline->GetCurrentFacing(), false, false,
        spline->HasCoordinateParentBinding(), spline->GetCoordinateParent(),
        spline->GetCoordinateParentSeat());
    spline_manager.CancelSpline(owner_.GetGuid().GetRawValue());
  }
  (void)TrySettleSplineMovementPoseOwnership();

  const auto previous_flags = owner_.position_.movement.flags;
  owner_.position_.movement.flags &= ~kLocomotionStateMask;
  data_.SyncPresentedMovementInfo(owner_.position_.movement);
  owner_.Animation().HandleMovementAnimation(
      previous_flags, owner_.position_.movement.flags);
}

void UnitMovementRuntime::CommitMovementRuntimeState(
    WorldSession &session, const std::uint32_t timestamp,
    const std::optional<std::uint16_t> opcode,
    const movement::CPlayerMoveEvent *const event) {
  const auto previous_movement = owner_.GetMovementInfo();
  const auto previous_flags = previous_movement.flags;
  const bool active_mover = owner_.IsActiveMover();
  const auto committed = MovementInfoFromRuntimeData(
      data_, previous_movement, session.objects(), timestamp);

  RebaseAutoAttackForTransportGuidChange(
      session.objects(), previous_movement.transport.guid.GetRawValue(),
      committed.transport.guid.GetRawValue());
  owner_.SetMovementInfo(committed);
  session.objects().SynchronizeUnitTransportPassengerMembership(
      owner_, previous_movement);
  SynchronizeUnitBoundWorldCamera(owner_, session.world_camera());
  owner_.Animation().UpdatePendingFallAnimation(previous_flags,
                                                 committed.flags);
  owner_.Animation().HandleMovementAnimation(
      previous_flags, committed.flags,
      std::exchange(pending_landing_animation_suppressed_, false));
  const bool refresh_gravity_ack_after_send =
      opcode.has_value() && IsGravityForceMovementAck(*opcode);
  if (opcode.has_value()) {
    if (event == nullptr ||
        event->event_type != static_cast<std::uint32_t>(
                                 movement::MoveEventType::kTimeSync)) {
      if (!refresh_gravity_ack_after_send) {
        owner_.Animation().HandleMovementOpcodeAnimationSideEffects(session,
                                                                    *opcode);
      }
    }
  }

  for (std::size_t index = 0; index < kMaxSpeeds; ++index) {
    const auto type = static_cast<SpeedType>(index);
    const float speed = data_.GetSpeed(type);
    owner_.speeds_[index] = speed;
    owner_.position_.speeds[index] = speed;
    if (active_mover) {
      session.movement().SetSpeed(type, speed);
    }
  }
  if (active_mover) {
    session.movement().ApplyAuthoritativeMovementInfo(committed);
  }

  if (opcode.has_value() && event != nullptr) {
    if (event->event_type == static_cast<std::uint32_t>(
                                 movement::MoveEventType::kTimeSync)) {
      (void)session.Send(net::wotlk::PacketSender::BuildTimeSyncResponse(
          event->auxiliary_u32, event->timestamp));
    } else {
      if (SendQueuedMovementEvent(session, owner_, data_, *opcode, timestamp,
                                  *event)) {
        UpdateWireAnnouncedFallingLatch(owner_, data_.GetRuntimeFlags());
        NoteSuccessfulMovementPacket(timestamp);
        if (refresh_gravity_ack_after_send) {

          owner_.Animation().HandleMovementOpcodeAnimationSideEffects(
              session, *opcode);
        }
      }
    }
  }
}

void UnitMovementRuntime::NoteSuccessfulMovementPacket(
    const std::uint32_t timestamp) {
  movement_timeline_.active_mover_deadline =
      timestamp + net::wotlk::kHeartbeatIntervalMs;
  data_.DelayActiveMoverDeadline(timestamp);
}

void UnitMovementRuntime::FlushMovementStateForControlTransition(
    WorldSession &session, const std::uint32_t timestamp,
    const bool finalize_stops) {

  data_.SetEventProcessingGateCallback({});
  data_.SetDispatchMovementOpcodeCallback(
      [this, &session](movement::CMovementData &,
                       const std::uint16_t opcode,
                       const std::uint32_t event_timestamp,
                       const movement::CPlayerMoveEvent &event) {
        CommitMovementRuntimeState(session, event_timestamp, opcode, &event);
      });
  data_.SetDispatchStopOpcodeCallback(
      [this, &session, timestamp](movement::CMovementData &,
                                  const std::uint32_t opcode) {
        CommitMovementRuntimeState(
            session, timestamp, static_cast<std::uint16_t>(opcode));
        if (owner_.IsActiveMover()) {
          (void)SendImmediateMovementPacket(
              session, static_cast<std::uint16_t>(opcode), timestamp);
        }
      });
  data_.SetDispatchDeferredAuthoritativeMovementCallback(
      [this, &session](movement::CMovementData &,
                       const movement::CPlayerMoveEvent &event) {
        (void)ApplyClientMovementOpcodeState(
            session, event.deferred_authoritative_opcode,
            *event.deferred_authoritative_movement, event.timestamp,
            event.timestamp);
      });

  (void)data_.FlushQueuedEvents(session.objects());
  if (finalize_stops) {
    data_.ProcessPendingMovementStops();
  }

  InstallUpdateScopedCallbacks();
  InstallEventProcessingGate();
  if (!data_.GetEventQueue().HasEvents()) {
    data_.ClearPendingRuntimeNotification();
  }
}

void UnitMovementRuntime::InstallEventProcessingGate() {
  data_.SetEventProcessingGateCallback(
      [this](const movement::CMovementData &,
             const movement::CPlayerMoveEvent &) {
        return !HasPendingVehicleSeatChange(owner_);
      });
}

void UnitMovementRuntime::FinalizeActiveMoverRelease(
    WorldSession &session, const std::uint32_t timestamp) {
  FlushMovementStateForControlTransition(session, timestamp, true);
  data_.SetRuntimeFlags(data_.GetRuntimeFlags() & 0x7FFFFFFFu);
  CommitMovementRuntimeState(session, timestamp);
}

void UnitMovementRuntime::PrepareActiveMoverControl(
    WorldSession &session, const std::uint32_t timestamp) {
  data_.SetRuntimeFlags2(
      static_cast<std::uint16_t>(data_.GetRuntimeFlags2() & 0xE3FFu));
  const bool parent_allows_finalize =
      !data_.HasParentMovement() ||
      (data_.GetParentMovementFlags() & kParentMovementAllowStopFlag) != 0u;
  FlushMovementStateForControlTransition(session, timestamp,
                                         parent_allows_finalize);
  movement_timeline_.current_tick = timestamp;
  data_.SetRuntimeFlags(data_.GetRuntimeFlags() & 0x7FFFFFFFu);
  CommitMovementRuntimeState(session, timestamp);
  if (parent_allows_finalize) {
    data_.QueueHeartbeat(timestamp);
  }
}

void UnitMovementRuntime::IncrementMoveSequence(const std::int32_t delta) noexcept {
  move_sequence_ += delta;
}

void UnitMovementRuntime::StartSwim(const std::uint32_t timestamp,
                                 const bool force_remote) {

  if (owner_.IsActivePlayer()) {
    data_.QueueStartSwim(timestamp);
    TutorialSystem::Instance().TriggerTutorial(27u);
  } else if (force_remote ||
             (data_.HasParentMovement() &&
              (data_.GetParentMovementFlags() &
               kParentMovementAllowStopFlag) == 0u)) {
    data_.HandleRemoteTeleportReset();
  } else {
    data_.QueueStartSwim(timestamp);
  }
}

void UnitMovementRuntime::StopSwim(const std::uint32_t timestamp,
                                const bool force_remote) {

  if (owner_.IsActivePlayer()) {
    data_.QueueStopSwim(timestamp);
  } else if (force_remote ||
             (data_.HasParentMovement() &&
              (data_.GetParentMovementFlags() &
               kParentMovementAllowStopFlag) == 0u)) {
    data_.HandleRemoteStopSwimReset();
  } else {
    data_.QueueStopSwim(timestamp);
  }
}

void UnitMovementRuntime::ResetFlightTransitionBodyLeanState() {
  body_lean_yaw_ = 0.0f;
  body_lean_flags_ &= ~0x7u;

  body_lean_last_facing_ = WorldSmoothBodyFacing();
  body_lean_heading_ = body_lean_last_facing_;
  body_lean_pitch_ = 0.0f;
}

bool UnitMovementRuntime::CanControlCharacter() const {
  return owner_.State().HasSpellStateFlags(
      CGUnit_C::kSpellStateClientControlGranted);
}

bool UnitMovementRuntime::IsNavigableAsPlayer() const {
  constexpr std::uint32_t kOverrideForbiddenFlags = 0x00000002u;
  constexpr std::uint32_t kForbiddenFlags = 0x00C00004u;
  constexpr std::uint32_t kUseOwnerNavigation = 0x01000000u;
  constexpr std::uint32_t kOwnerNavigationBlocked = 0x00000001u;
  const auto flags = owner_.State().GetUnitFlags();
  if ((flags & kOverrideForbiddenFlags) == 0u &&
      (flags & kForbiddenFlags) != 0u) {
    return false;
  }
  if ((flags & kUseOwnerNavigation) == 0u) {
    return owner_.IsPlayer() && owner_.State().GetCharmedBy().IsEmpty() &&
           (flags & kOwnerNavigationBlocked) == 0u;
  }
  const auto *const objects = owner_.object_manager();
  const auto *const owner =
      objects != nullptr ? objects->GetUnit(owner_.State().GetCharmedByOrCreatedByGUID())
                         : nullptr;
  return owner != nullptr && owner->IsPlayer() &&
         (owner->State().GetUnitFlags() & kOwnerNavigationBlocked) == 0u;
}

bool UnitMovementRuntime::IsActivePlayerMover() const {
  return owner_.IsActivePlayer();
}

bool UnitMovementRuntime::IsGhostPlayerDescriptorPair() const {
  if (!owner_.IsPlayer()) {
    return false;
  }
  return static_cast<const CGPlayer_C &>(owner_).IsGhost();
}

std::uint32_t UnitMovementRuntime::BuildTerrainIntersectFlags() const {
  movement::CMovementData::TerrainIntersectUnitState state;
  state.is_navigable_as_player = IsNavigableAsPlayer();
  state.can_control_character = CanControlCharacter();
  state.is_ghost_player = IsGhostPlayerDescriptorPair();
  return data_.BuildTerrainIntersectFlags(state);
}

void UnitMovementRuntime::SetSpeedBounds(const float walk_speed,
                                         const float run_speed) noexcept {
  speed_bounds_[0] = walk_speed;
  speed_bounds_[1] = walk_speed;
  speed_bounds_[2] = walk_speed;
  speed_bounds_[3] = run_speed;
  speed_bounds_[4] = run_speed;
  speed_bounds_[5] = run_speed;
}

bool UnitMovementRuntime::IsFacingTarget(const CGUnit_C &other) const {
  const auto position = owner_.GetPosition();
  const auto target_position = other.GetPosition();
  const float direction[3] = {
      target_position.x - position.x, target_position.y - position.y,
      target_position.z - position.z};

  const auto orientation = data_.GetScalarFacing();
  const float local_forward[3] = {std::cos(orientation),
                                  std::sin(orientation), 0.0f};
  float world_forward[3];
  if (const auto *const objects = owner_.object_manager(); objects != nullptr) {
    MovementShared_GetWorldFacingDirection(
        *objects, owner_.GetTransportGUID().GetRawValue(), local_forward,
        world_forward);
  } else {
    std::copy(std::begin(local_forward), std::end(local_forward),
              world_forward);
  }
  return world_forward[0] * direction[0] +
             world_forward[1] * direction[1] +
             world_forward[2] * direction[2] >=
         0.0f;
}

bool UnitMovementRuntime::IsFacingTarget(const ObjectGuid target_guid) const {
  const auto *const objects = owner_.object_manager();
  const auto *const target =
      objects != nullptr ? objects->GetUnit(target_guid) : nullptr;
  return target != nullptr && IsFacingTarget(*target);
}

void UnitMovementRuntime::SynchronizeAuthoritativeState(
    WorldSession &session, const MovementUpdate &update,
    const std::uint32_t) {
  if (!update.IsLiving()) {
    return;
  }

  SyncAuthoritativeSpline(session, owner_, update);
  SynchronizeBoundWorldCamera(session.world_camera());
}

void UnitMovementRuntime::ApplyCreateMovementMetadata(
    WorldSession &session, const MovementUpdate &update) {

  if (update.HasUpdateFlag(kUpdateFlagVehicle) && update.vehicle_id != 0u) {
    UnitVehicle_EnsureVehicleData(session, owner_, update.vehicle_id);
  }
  if (owner_.IsActivePlayer()) {
    return;
  }
  if (update.HasUpdateFlag(kUpdateFlagSelf)) {
    update_flags_ |= 0x80u;
  }
  if ((update.update_flags & 0x0400u) != 0u) {
    update_flags_ |= 0x40000000u;
  }
}

void UnitMovementRuntime::ApplySplineSpeedChange(
    WorldSession &session, const SpeedType type, const float speed,
    const std::uint32_t movement_opcode) {
  const auto index = static_cast<std::size_t>(type);
  if (index >= kMaxSpeeds) {
    return;
  }

  const bool speed_changed =
      owner_.position_.IsLiving() && data_.SetSpeed(type, speed);
  if (speed_changed) {
    owner_.speeds_[index] = speed;
    owner_.position_.speeds[index] = speed;
    if (owner_.IsActiveMover()) {
      session.movement().SetSpeed(type, speed);
    }
  }
  owner_.Animation().HandleMovementOpcodeAnimationSideEffects(session, movement_opcode);
  owner_.Animation().RefreshSelectedStandAnimation(session, 0u, ~0u);
}

void UnitMovementRuntime::ApplySplineMovementPose(const Vec3 &position,
                                        const float facing,
                                        const bool spline_active,
                                        const bool moving_backward,
                                        const bool has_coordinate_parent_binding,
                                        const ObjectGuid coordinate_parent,
                                        const std::int8_t coordinate_parent_seat,
                                        const std::uint32_t spline_flags,
                                        const float spline_arc_length,
                                        const std::uint32_t spline_duration_ms,
                                        const bool parent_movement_active) {
  const MovementInfo previous_movement = owner_.position_.movement;
  const auto animation_base_flags =
      owner_.GetMovementInfo().flags & ~kDirectionalLocomotionMask;
  const auto previous_flags =
      animation_base_flags |
      (spline_locomotion_active_
           ? (spline_locomotion_backward_ ? kMoveFlagBackward
                                          : kMoveFlagForward)
           : 0u);
  if (has_coordinate_parent_binding) {
    spline_coordinate_parent_ = coordinate_parent;
    spline_coordinate_parent_seat_ = coordinate_parent_seat;
  }
  const auto parent_guid = has_coordinate_parent_binding
                               ? spline_coordinate_parent_
                               : owner_.position_.movement.transport.guid;
  if (!parent_guid.IsEmpty()) {
    auto &transport = owner_.position_.movement.transport;
    if (!has_coordinate_parent_binding) {
      spline_coordinate_parent_ = parent_guid;
      spline_coordinate_parent_seat_ = transport.seat;
    }
    transport.guid = parent_guid;
    transport.seat = spline_coordinate_parent_seat_;
    transport.offset_x = position.x;
    transport.offset_y = position.y;
    transport.offset_z = position.z;
    transport.offset_o = facing;
    owner_.position_.movement.flags |= kMoveFlagOnTransport;
    spline_local_position_ = position;
    spline_local_facing_ = facing;
    spline_pose_waiting_for_parent_ = true;
    (void)TryComposeSplineParentPose();
  } else {
    if (has_coordinate_parent_binding) {
      owner_.position_.movement.transport.guid = {};
      owner_.position_.movement.transport.seat = -1;
      owner_.position_.movement.flags &= ~kMoveFlagOnTransport;
    }
    owner_.position_.movement.x = position.x;
    owner_.position_.movement.y = position.y;
    owner_.position_.movement.z = position.z;
    owner_.position_.movement.orientation = facing;
    spline_pose_waiting_for_parent_ = false;
  }
  data_.SetParentMovementFlags(
      parent_movement_active ? spline_flags : 0u, parent_movement_active);
  data_.SyncPresentedMovementInfo(owner_.position_.movement);
  if (auto *const objects = owner_.object_manager(); objects != nullptr) {
    objects->SynchronizeUnitTransportPassengerMembership(owner_,
                                                         previous_movement);
  }
  spline_movement_pose_owned_ = true;
  spline_locomotion_active_ = spline_active;
  spline_locomotion_backward_ = spline_active && moving_backward;

  spline_locomotion_flags_ = spline_active ? spline_flags : 0u;
  spline_locomotion_arc_length_ = spline_active ? spline_arc_length : 0.0f;
  spline_locomotion_duration_ms_ = spline_active ? spline_duration_ms : 0u;
  const auto current_flags =
      animation_base_flags |
      (spline_locomotion_active_
           ? (spline_locomotion_backward_ ? kMoveFlagBackward
                                          : kMoveFlagForward)
           : 0u);
  owner_.Animation().HandleMovementAnimation(previous_flags, current_flags);
}

float UnitMovementRuntime::ComputeCurrentSpeed() const {

  if (spline_locomotion_active_ &&
      (spline_locomotion_flags_ & SplineFlag::kStateQueryExempt) == 0u &&
      spline_locomotion_duration_ms_ != 0u) {
    return spline_locomotion_arc_length_ /
           static_cast<float>(spline_locomotion_duration_ms_) * 1000.0f;
  }
  return data_.CalculateCurrentSpeed(false);
}

bool UnitMovementRuntime::HasNonExemptFlyingSpline() const noexcept {
  return spline_locomotion_active_ &&
         SplineFlag::HasNonExemptFlag(spline_locomotion_flags_,
                                      SplineFlag::kFlying);
}

void UnitMovementRuntime::ApplyServerMovementTimeSkipped(
    const std::uint32_t skipped_time_ms) {
  data_.ApplyServerMovementTimeSkipped(skipped_time_ms);
}

void UnitMovementRuntime::ApplyQueuedPreview(
    const std::uint32_t current_tick_ms,
    const std::uint32_t frame_delta_ms) {
  if (!owner_.position_.IsLiving()) {
    return;
  }
  const auto *const objects = owner_.object_manager();
  if (objects == nullptr) {
    return;
  }
  auto preview = data_.GetTransformPosition();
  float orientation = data_.GetScalarFacing();
  float pitch = owner_.position_.movement.pitch;
  data_.ApplyQueuedMovementPreview(
      current_tick_ms, frame_delta_ms, preview[0], preview[1], preview[2],
      orientation, pitch);
  if (owner_.position_.movement.IsOnTransport() &&
      !owner_.position_.movement.transport.guid.IsEmpty()) {
    auto &transport = owner_.position_.movement.transport;
    transport.offset_x = preview[0];
    transport.offset_y = preview[1];
    transport.offset_z = preview[2];
    transport.offset_o = orientation;
    owner_.position_.movement.pitch = pitch;
    const float local[3] = {preview[0], preview[1], preview[2]};
    float world[3]{};
    Passenger_TransformLocalToWorldPosition(
        *objects, transport.guid.GetRawValue(), world, local);
    owner_.position_.movement.x = world[0];
    owner_.position_.movement.y = world[1];
    owner_.position_.movement.z = world[2];
    owner_.position_.movement.orientation = Movement_TransformLocalFacingToWorld(
        *objects, transport.guid.GetRawValue(), orientation);
    return;
  }
  owner_.position_.movement.x = preview[0];
  owner_.position_.movement.y = preview[1];
  owner_.position_.movement.z = preview[2];
  owner_.position_.movement.orientation = orientation;
  owner_.position_.movement.pitch = pitch;
}

float UnitMovementRuntime::ComputePitchAngle(const float *const a,
                                             const float *const b) {
  const float dx = a[0] - b[0];
  const float dy = a[1] - b[1];
  const float dz = a[2] - b[2];
  const float magnitude_squared = dx * dx + dy * dy + dz * dz;
  if (magnitude_squared < 1e-12f) {
    return 0.0f;
  }
  const float inverse_magnitude = 1.0f / std::sqrt(magnitude_squared);
  const float normalized_x = dx * inverse_magnitude;
  const float normalized_y = dy * inverse_magnitude;
  const float normalized_z = dz * inverse_magnitude;
  if (std::fabs(normalized_x) >= 0.001f ||
      std::fabs(normalized_y) >= 0.001f) {
    return std::asin(normalized_z);
  }
  return b[2] >= a[2] ? -1.5707964f : 1.5707964f;
}

void UnitMovementRuntime::SetClientControlState(openwow::world::WorldCamera *const camera,
                                     const bool enabled) {
  constexpr std::uint32_t kControlActive =
      CGUnit_C::kSpellStateClientControlGranted;
  constexpr std::uint32_t kCharmedControl = 0x00001000u;

  constexpr std::uint32_t kControlGrant = 0x0C000000u | kControlActive;
  if (enabled) {
    owner_.State().AddSpellStateFlags(kControlGrant);
  } else {
    owner_.State().ClearSpellStateFlags(kControlActive);
    if (owner_.State().HasCharmedByGUID()) {
      owner_.State().AddSpellStateFlags(kCharmedControl);
    }
  }
  data_.ResetClientControlTransition();
  SynchronizeUnitBoundWorldCamera(owner_, camera);
}

void UnitMovementRuntime::SynchronizeBoundWorldCamera(
    openwow::world::WorldCamera *const camera) const {
  SynchronizeUnitBoundWorldCamera(owner_, camera);
}

void UnitMovementRuntime::SendSetFacing(WorldSession &session,
                              const std::uint32_t timestamp, float facing) {
  if (owner_.IsActiveMover() && owner_.Interaction().IsAutoAttacking() &&
      (session.player_control_runtime().movement_interaction_flags & 0x1u) == 0u) {
    owner_.Interaction().CompleteAutoAttackInteraction(false, true);
  }

  facing = Movement_TransformWorldFacingToLocal(
      session.objects(), data_.GetTransportGuid(), facing);
  float min_yaw = 0.0f;
  float max_yaw = 0.0f;
  if (TryGetVehicleYawBounds(owner_, min_yaw, max_yaw)) {
    facing = ClampVehicleYaw(facing, min_yaw, max_yaw);
  }
  data_.QueueSetFacing(timestamp, facing);
  if (owner_.Animation().GetStandState() == 3u && owner_.IsPlayer()) {
    owner_.Animation().TrySetStandStateAndNotifyServer(session, 0u);
  }
}

void UnitMovementRuntime::SendBoundedTurnFacing(WorldSession &session,
                                     const std::uint32_t timestamp,
                                     const float facing) {
  if (owner_.IsActiveMover() && owner_.Interaction().IsAutoAttacking() &&
      (session.player_control_runtime().movement_interaction_flags & 0x1u) == 0u) {
    owner_.Interaction().CompleteAutoAttackInteraction(false, true);
  }

  data_.QueueBoundedTurnFacing(
      timestamp, Movement_TransformWorldFacingToLocal(
                     session.objects(), data_.GetTransportGuid(), facing));
  if (owner_.Animation().GetStandState() == 3u && owner_.IsPlayer()) {
    owner_.Animation().TrySetStandStateAndNotifyServer(session, 0u);
  }
}

void UnitMovementRuntime::SendSetPitch(WorldSession &session,
                            const std::uint32_t timestamp,
                            const float pitch) {
  if (owner_.IsActiveMover() && owner_.Interaction().IsAutoAttacking() &&
      (session.player_control_runtime().movement_interaction_flags & 0x1u) == 0u) {
    owner_.Interaction().CompleteAutoAttackInteraction(false, true);
  }
  owner_.Interaction().CancelAutoAttackAndCheckLootClose(session, false, false);
  float min_pitch = 0.0f;
  float max_pitch = 0.0f;
  data_.QueueSetPitch(
      timestamp, TryGetVehiclePitchBounds(owner_, min_pitch, max_pitch)
                     ? std::clamp(pitch, min_pitch, max_pitch)
                     : pitch);
}

void UnitMovementRuntime::SendSetVehiclePitch(WorldSession &session,
                                   const std::uint32_t timestamp,
                                   const float pitch) {
  if (owner_.IsActiveMover() && owner_.Interaction().IsAutoAttacking() &&
      (session.player_control_runtime().movement_interaction_flags & 0x1u) == 0u) {
    owner_.Interaction().CompleteAutoAttackInteraction(false, true);
  }
  float min_pitch = 0.0f;
  float max_pitch = 0.0f;
  data_.QueueSetVehiclePitch(
      timestamp, TryGetVehiclePitchBounds(owner_, min_pitch, max_pitch)
                     ? std::clamp(pitch, min_pitch, max_pitch)
                     : pitch);
  if (owner_.Animation().GetStandState() == 3u && owner_.IsPlayer()) {
    owner_.Animation().TrySetStandStateAndNotifyServer(session, 0u);
  }
}

void UnitMovementRuntime::SendTurnMovement(const std::uint32_t timestamp,
                                const bool turning_right) {
  float min_yaw = 0.0f;
  float max_yaw = 0.0f;
  if (TryGetVehicleYawBounds(owner_, min_yaw, max_yaw)) {
    data_.QueueBoundedTurnFacing(timestamp,
                                          turning_right ? max_yaw : min_yaw);
    return;
  }
  data_.QueueTurnMovement(timestamp, turning_right);
  if (auto *const input = GetInputControlSingleton(); input != nullptr) {
    input->PopYawIfNeeded();
  }
}

void UnitMovementRuntime::SendPitchMovement(const std::uint32_t timestamp,
                                 const bool pitching_down) {
  float min_pitch = 0.0f;
  float max_pitch = 0.0f;
  if (TryGetVehiclePitchBounds(owner_, min_pitch, max_pitch)) {
    data_.QueueSetVehiclePitch(
        timestamp, pitching_down ? max_pitch : min_pitch);
    return;
  }
  data_.QueuePitchMovement(timestamp, pitching_down);
}

bool UnitMovementRuntime::ClampAngleToVehicleYawWindow(float *const angle) const {
  float min_yaw = 0.0f;
  float max_yaw = 0.0f;
  if (!TryGetVehicleYawBounds(owner_, min_yaw, max_yaw)) {
    return false;
  }
  const float wrapped = WrapVehicleYaw(*angle, min_yaw, max_yaw);
  if (wrapped < min_yaw) {
    *angle = min_yaw;
    return true;
  }
  if (wrapped > max_yaw) {
    *angle = max_yaw;
    return true;
  }
  return false;
}

bool UnitMovementRuntime::IsLocallyControlled() const {
  return owner_.IsActivePlayer() || owner_.IsActiveMover() ||
          owner_.State().HasSpellStateFlags(0x1000u);
}

bool UnitMovementRuntime::CanChangeDirection() const {
  constexpr std::uint32_t kMovementDirectionLockedUnitFlag = 0x00100000u;
  constexpr std::uint32_t kTurnWhileMovingFlag2 = 0x02000000u;
  if ((owner_.State().GetUnitFlags() & kMovementDirectionLockedUnitFlag) != 0u) {
    return false;
  }
  const auto active_player_guid = CGObject_C::GetActivePlayerGuid();
  if (active_player_guid.IsEmpty() || owner_.GetGuid() != active_player_guid ||
      GetInputControlSingleton() == nullptr) {
    return false;
  }
  return (owner_.State().GetUnitFlags2() & kTurnWhileMovingFlag2) == 0u ||
         InputControl_CheckTurnFlag(&owner_) != 0;
}

bool UnitMovementRuntime::CanTurn() const {
  constexpr std::uint32_t kMovementDirectionLockedUnitFlag = 0x00100000u;
  constexpr std::uint32_t kTurnWhileMovingFlag2 = 0x02000000u;
  constexpr std::uint32_t kTurnLockedShapeshiftFlag = 0x100u;
  if ((owner_.State().GetUnitFlags() & kMovementDirectionLockedUnitFlag) != 0u) {
    return false;
  }
  const auto active_player_guid = CGObject_C::GetActivePlayerGuid();
  if (active_player_guid.IsEmpty() || owner_.GetGuid() != active_player_guid ||
      GetInputControlSingleton() == nullptr) {
    return false;
  }
  if ((owner_.State().GetUnitFlags2() & kTurnWhileMovingFlag2) != 0u &&
      InputControl_CheckTurnFlag(&owner_) == 0) {
    return false;
  }
  const auto shapeshift_form = owner_.Animation().GetShapeshiftForm();
  if (shapeshift_form == 0u) {
    return true;
  }
  const auto *const dbc = owner_.dbc_loader();
  if (dbc == nullptr) {
    return true;
  }
  const auto *const form =
      dbc->spell_shapeshift_form().LookupEntry(shapeshift_form);
  return form == nullptr || (form->flags & kTurnLockedShapeshiftFlag) == 0u;
}

void UnitMovementRuntime::StopForward(const std::uint32_t timestamp) {
  data_.QueueStopForward(timestamp);
}

void UnitMovementRuntime::StopStrafe(const std::uint32_t timestamp) {
  data_.QueueStopStrafe(timestamp);
}

void UnitMovementRuntime::StopVertical(const std::uint32_t timestamp) {
  data_.QueueStopVertical(timestamp);
}

void UnitMovementRuntime::StopTurn(const std::uint32_t timestamp) {
  data_.QueueStopTurn(timestamp);
}

void UnitMovementRuntime::ToggleRun(const std::uint32_t timestamp) {
  const bool was_walking =
      (owner_.GetMovementInfo().flags & kMoveFlagWalking) != 0u;
  data_.QueueToggleRun(timestamp, was_walking);
}

void UnitMovementRuntime::QueueHeartbeat(const std::uint32_t timestamp) {
  data_.QueueHeartbeat(timestamp);
}

void UnitMovementRuntime::SwimToFly(const std::uint32_t timestamp,
                         const bool enable_fly_mode) {
  if (enable_fly_mode) {
    g_last_can_fly_fall_start_timestamp = timestamp;
  }
  data_.QueueSwimToFly(timestamp, enable_fly_mode);
}

bool UnitMovementRuntime::HasCanFlyFlag() const {
  return (owner_.GetMovementInfo().flags & kMoveFlagCanFly) != 0u;
}

void UnitMovementRuntime::ResetCanFlyGroundContactRuntimeForTesting() {
  g_last_can_fly_fall_start_timestamp = 0u;
  g_can_fly_landing_deadline_initialized = false;
  g_can_fly_landing_deadline = 0u;
}

void UnitMovementRuntime::ReconcileCanFlyGroundContact(
    const WorldSession &session, const std::uint32_t timestamp,
    const std::optional<float> ground_surface_height,
    const std::optional<float> vertical_clearance) {
  const auto &movement = owner_.GetMovementInfo();
  if ((movement.flags & kMoveFlagCanFly) == 0u) {
    return;
  }
  if ((movement.flags & kMoveFlagFlying) != 0u) {
    if (!g_can_fly_landing_deadline_initialized) {
      g_can_fly_landing_deadline_initialized = true;
      g_can_fly_landing_deadline =
          timestamp + kCanFlyGroundContactThrottleMs;
    }
    if (ground_surface_height.has_value() &&
        HasSignedTimestampReached(timestamp, g_can_fly_landing_deadline)) {
      const Position position = owner_.GetPosition();

      if (position.z + data_.GetCollisionHeightProduct() >
          *ground_surface_height - kCanFlyGroundProximityAllowance) {
        if (!owner_.Vehicle().VehicleSuppressesTransitionAnimation(owner_)) {
          owner_.Animation().PlayEmoteAnimation(45, 0u);
          owner_.Animation().UpdateStandAnimation(session, 0, 0);
        }
        PlayCanFlyLandingSound(owner_, ResolveCanFlyLandingSoundKitId(owner_),
                               position);
        g_can_fly_landing_deadline =
            timestamp + kCanFlyGroundContactThrottleMs;
      }
    }
    if (static_cast<std::uint32_t>(
            timestamp - g_last_can_fly_fall_start_timestamp) >
            kCanFlyGroundContactThrottleMs &&
        owner_.IsActivePlayer() && vertical_clearance.has_value() &&
        *vertical_clearance < kCanFlyVerticalClearanceThreshold &&
        !CanUseCommentatorMovementControls(owner_)) {
      SwimToFly(timestamp, false);
    }
    return;
  }

  const bool has_active_launch_gate = IsRisingBeforeFallApex(movement);
  if ((movement.flags & kMoveFlagFalling) == 0u || has_active_launch_gate ||
      !owner_.IsActiveMover() || !vertical_clearance.has_value() ||
      *vertical_clearance < kCanFlyVerticalClearanceThreshold) {
    return;
  }
  SwimToFly(timestamp, true);
}

[[nodiscard]] static bool IsRetailClientMovementOpcode(
    const std::uint32_t opcode) noexcept {
  switch (opcode) {
    case 0xB5u:
    case 0xB6u:
    case 0xB7u:
    case 0xB8u:
    case 0xB9u:
    case 0xBAu:
    case 0xBBu:
    case 0xBCu:
    case 0xBDu:
    case 0xBEu:
    case 0xBFu:
    case 0xC0u:
    case 0xC1u:
    case 0xC2u:
    case 0xC3u:
    case 0xC5u:
    case 0xC9u:
    case 0xCAu:
    case 0xCBu:
    case 0xD9u:
    case 0xDAu:
    case 0xDBu:
    case 0xECu:
    case 0xEDu:
    case 0xEEu:
    case 0xF1u:
    case 0xF7u:
    case 0x2B0u:
    case 0x2B1u:
    case 0x341u:
    case 0x342u:
    case 0x34Au:
    case 0x359u:
    case 0x35Au:
    case 0x3A7u:
    case 0x3ADu:
    case 0x4D2u:
      return true;
    default:
      return false;
  }
}

struct SpeedAckRoute {
  movement::MoveEventType event_type;
  SpeedType speed_type;
  bool collision_height;
};

[[nodiscard]] static std::optional<SpeedAckRoute> RouteSpeedAckOpcode(
    const std::uint32_t opcode) noexcept {
  using movement::MoveEventType;
  switch (opcode) {
    case 0x0CDu: return SpeedAckRoute{MoveEventType::kSetRunSpeed, kSpeedRun, false};
    case 0x0CFu: return SpeedAckRoute{MoveEventType::kSetRunBackSpeed, kSpeedRunBack, false};
    case 0x0D1u: return SpeedAckRoute{MoveEventType::kSetWalkSpeed, kSpeedWalk, false};
    case 0x0D3u: return SpeedAckRoute{MoveEventType::kSetSwimSpeed, kSpeedSwim, false};
    case 0x0D5u: return SpeedAckRoute{MoveEventType::kSetSwimBackSpeed, kSpeedSwimBack, false};
    case 0x0D8u: return SpeedAckRoute{MoveEventType::kSetTurnRate, kSpeedTurnRate, false};
    case 0x37Eu: return SpeedAckRoute{MoveEventType::kSetFlightSpeed, kSpeedFlight, false};
    case 0x380u: return SpeedAckRoute{MoveEventType::kSetFlightBackSpeed, kSpeedFlightBack, false};
    case 0x45Bu: return SpeedAckRoute{MoveEventType::kSetPitchRate, kSpeedPitchRate, false};
    case 0x518u:
      return SpeedAckRoute{MoveEventType::kSetCollisionHeight, kSpeedWalk, true};
    default:
      return std::nullopt;
  }
}

[[nodiscard]] static bool IsDeferredSpeedAckEvent(
    const std::uint32_t event_type) noexcept {
  const auto type = static_cast<movement::MoveEventType>(event_type);
  return (type >= movement::MoveEventType::kSetRunSpeed &&
          type <= movement::MoveEventType::kSetPitchRate) ||
         type == movement::MoveEventType::kSetCollisionHeight;
}

[[nodiscard]] static std::uint32_t DeferredMoveEventTypeForOpcode(
    const std::uint32_t opcode, const MovementInfo& movement_info) noexcept {
  using movement::MoveEventType;
  switch (opcode) {
    case 0xB5u: return static_cast<std::uint32_t>(MoveEventType::kStartForward);
    case 0xB6u: return static_cast<std::uint32_t>(MoveEventType::kStartBackward);
    case 0xB7u: return static_cast<std::uint32_t>(MoveEventType::kStopForwardBackward);
    case 0xB8u: return static_cast<std::uint32_t>(MoveEventType::kStartStrafeLeft);
    case 0xB9u: return static_cast<std::uint32_t>(MoveEventType::kStartStrafeRight);
    case 0xBAu: return static_cast<std::uint32_t>(MoveEventType::kStopStrafe);
    case 0xBBu: return static_cast<std::uint32_t>(MoveEventType::kJump);
    case 0xF1u: return static_cast<std::uint32_t>(MoveEventType::kKnockBack);
    case 0xBCu: return static_cast<std::uint32_t>(MoveEventType::kStartTurnLeft);
    case 0xBDu: return static_cast<std::uint32_t>(MoveEventType::kStartTurnRight);
    case 0xBEu: return static_cast<std::uint32_t>(MoveEventType::kStopTurn);
    case 0xBFu: return static_cast<std::uint32_t>(MoveEventType::kStartPitchUp);
    case 0xC0u: return static_cast<std::uint32_t>(MoveEventType::kStartPitchDown);
    case 0xC1u: return static_cast<std::uint32_t>(MoveEventType::kStopPitch);
    case 0xC2u: return static_cast<std::uint32_t>(MoveEventType::kStartRun);
    case 0xC3u: return static_cast<std::uint32_t>(MoveEventType::kStartWalk);
    case 0xC5u: return static_cast<std::uint32_t>(MoveEventType::kChangeTransportSeat);
    case 0xCAu: return static_cast<std::uint32_t>(MoveEventType::kStartSwim);
    case 0xCBu: return static_cast<std::uint32_t>(MoveEventType::kStopSwim);
    case 0xDAu: return static_cast<std::uint32_t>(MoveEventType::kSetFacing);
    case 0xDBu: return static_cast<std::uint32_t>(MoveEventType::kSetPitch);
    case 0xECu: return static_cast<std::uint32_t>(MoveEventType::kRoot);
    case 0xEDu: return static_cast<std::uint32_t>(MoveEventType::kUnroot);
    case 0xF7u:
      return static_cast<std::uint32_t>(
          (movement_info.flags & kMoveFlagHover) != 0u
              ? MoveEventType::kHoverEnable
              : MoveEventType::kHoverDisable);
    case 0x2B0u:
      return static_cast<std::uint32_t>(
          (movement_info.flags & kMoveFlagFallingSlow) != 0u
              ? MoveEventType::kFeatherFallEnable
              : MoveEventType::kFeatherFallDisable);
    case 0x2B1u:
      return static_cast<std::uint32_t>(
          (movement_info.flags & kMoveFlagWaterwalking) != 0u
              ? MoveEventType::kWaterWalkEnable
              : MoveEventType::kWaterWalkDisable);
    case 0x341u: return static_cast<std::uint32_t>(MoveEventType::kStartSwim);
    case 0x342u: return static_cast<std::uint32_t>(MoveEventType::kStopSwim);
    case 0x34Au:
      return static_cast<std::uint32_t>(
          (movement_info.flags2 & 0x4000u) != 0u
              ? MoveEventType::kSwimFlyTransitionEnable
              : MoveEventType::kSwimFlyTransitionDisable);
    case 0x359u: return static_cast<std::uint32_t>(MoveEventType::kStartAscend);
    case 0x35Au: return static_cast<std::uint32_t>(MoveEventType::kStopVerticalExplicit);
    case 0x3A7u: return static_cast<std::uint32_t>(MoveEventType::kStartDescend);
    case 0x3ADu:
      return static_cast<std::uint32_t>(
          (movement_info.flags & kMoveFlagCanFly) != 0u
              ? MoveEventType::kCanFlyEnable
              : MoveEventType::kCanFlyDisable);
    case 0x4D2u:
      return static_cast<std::uint32_t>(
          (movement_info.flags & kMoveFlagDisableGravity) != 0u
              ? MoveEventType::kGravityDisable
              : MoveEventType::kGravityEnable);
    case 0xC9u:
    case 0xEEu:
      return static_cast<std::uint32_t>(MoveEventType::kRemoteMovementSnapshot);
    default:
      return static_cast<std::uint32_t>(MoveEventType::kHeartbeat);
  }
}

int UnitMovementRuntime::DispatchClientMovementOpcode(WorldSession &session,
                                            const std::uint32_t opcode,
                                            const MovementInfo &movement_info,
                                            const std::uint32_t client_receive_tick) {
  if (!IsRetailClientMovementOpcode(opcode)) {
    return 0;
  }
  if (opcode == 0xD9u) {

    return 1;
  }

  const bool parent_allows_transition_gate =
      !data_.HasParentMovement() ||
      (data_.GetParentMovementFlags() & kParentMovementAllowStopFlag) != 0u;
  if (parent_allows_transition_gate && HasPendingVehicleSeatChange(owner_)) {
    data_.QueueDeferredAuthoritativeMovement(
        client_receive_tick,
        DeferredMoveEventTypeForOpcode(opcode, movement_info), opcode,
        movement_info);
    return 0;
  }

  const std::uint32_t runtime_current_tick =
      has_movement_update_tick_ ? movement_timeline_.current_tick
                                : client_receive_tick;
  const auto timing = data_.ResolveServerMovementTiming(
      client_receive_tick, movement_info.time, runtime_current_tick, false);
  if (!timing.apply_now) {
    data_.QueueDeferredAuthoritativeMovement(
        timing.presentation_tick,
        DeferredMoveEventTypeForOpcode(opcode, movement_info), opcode,
        movement_info);
    return 0;
  }

  return ApplyClientMovementOpcodeState(session, opcode, movement_info,
                                        timing.presentation_tick,
                                        runtime_current_tick);
}

int UnitMovementRuntime::ApplyClientMovementOpcodeState(
    WorldSession &session, const std::uint32_t opcode,
    const MovementInfo &movement_info,
    const std::uint32_t presentation_tick,
    const std::uint32_t application_tick) {

  auto &move_data = data_;
  const auto previous_runtime_flags = move_data.GetRuntimeFlags();

  if (!owner_.IsActiveMover()) {
    ImportRemoteMovementOpcodeState(session, movement_info, presentation_tick,
                                    application_tick);
  }
  bool handled = false;
  const auto sync = [&] {
    handled = true;
  };

  switch (opcode) {

  case 0xB5u:
    handled = move_data.HandleRemoteForwardStart(true);
    break;
  case 0xB6u:
    handled = move_data.HandleRemoteForwardStart(false);
    break;
  case 0xB7u: {
    sync();
    handled = move_data.TryStopForwardBackwardIfPreviouslyActive(
        previous_runtime_flags);
    break;
  }
  case 0xB8u:
    handled = move_data.HandleRemoteStrafeStart(true);
    break;
  case 0xB9u:
    handled = move_data.HandleRemoteStrafeStart(false);
    break;
  case 0xBAu: {
    sync();
    handled = move_data.TryClearStrafeIfPreviouslyActive(
        previous_runtime_flags);
    break;
  }
  case 0xBBu:
    handled = move_data.HandleRemoteJump();
    break;

  case 0xBCu:
    move_data.HandleRemoteTurnStart(true);
    handled = true;
    break;
  case 0xBDu:
    move_data.HandleRemoteTurnStart(false);
    handled = true;
    break;
  case 0xBEu: {
    const auto old_turn_flags = previous_runtime_flags &
        (kMoveFlagTurnLeft | kMoveFlagTurnRight);
    sync();
    move_data.SetRuntimeFlags(move_data.GetRuntimeFlags() | old_turn_flags);
    handled = move_data.ProcessPendingTurnStop();
    break;
  }

  case 0xBFu:
    move_data.HandleRemotePitchStart(true);
    handled = true;
    break;
  case 0xC0u:
    move_data.HandleRemotePitchStart(false);
    handled = true;
    break;
  case 0xC1u: {
    const auto old_pitch_flags = previous_runtime_flags &
        (kMoveFlagPitchUp | kMoveFlagPitchDown);
    sync();
    move_data.SetRuntimeFlags(move_data.GetRuntimeFlags() | old_pitch_flags);
    handled = move_data.ProcessPendingPitchStop();
    break;
  }

  case 0xC2u:
  case 0xC3u:
    sync();
    break;

  case 0xC5u:
    handled = HandleRemoteMoveChangeTransportSeat(
        session, movement_info, application_tick);
    break;

  case 0xC9u:
    sync();
    break;
  case 0xCAu:
    move_data.HandleRemoteStartSwim();
    handled = true;
    break;
  case 0xCBu:
    move_data.HandleRemoteStopSwimReset();
    handled = true;
    break;

  case 0xDAu:
  case 0xDBu:
    move_data.HandleRemotePoseSnapshot();
    handled = true;
    break;

  case 0xECu:
    move_data.HandleRemoteRootAck();
    handled = true;
    break;

  case 0xEDu:
    owner_.State().SetForcedVehicleTransition(true);
    move_data.HandleRemoteUnrootAck();
    owner_.State().SetForcedVehicleTransition(false);
    handled = true;
    break;

  case 0xEEu:
  case 0xF1u:
  case 0x2B0u:
    sync();
    break;
  case 0xF7u:
    move_data.HandleRemoteHoverSnapshot();
    handled = true;
    break;
  case 0x2B1u:
    handled = move_data.HandleRemoteWaterWalkSnapshot();
    break;
  case 0x341u:
    move_data.HandleRemoteStartSwim();
    handled = true;
    break;
  case 0x342u:
    move_data.HandleRemoteStopSwimReset();
    handled = true;
    break;

  case 0x34Au:
    handled = move_data.HandleRemoteCanTransitionSwimFly(
        (movement_info.flags2 &
         static_cast<std::uint16_t>(
             kMoveFlag2CanTransitionBetweenSwimAndFly)) != 0u);
    break;
  case 0x359u:
    handled = move_data.HandleRemoteAscendDescend(true);
    break;
  case 0x35Au:
    handled = move_data.HandleRemoteStopAscendDescend();
    break;

  case 0x3A7u:
    handled = move_data.HandleRemoteAscendDescend(false);
    break;

  case 0x3ADu:
    handled = move_data.HandleRemoteCanFlySnapshot();
    break;
  case 0x4D2u:
    handled = move_data.HandleRemoteGravitySnapshot();
    break;

  default:
    return 0;
  }

  CommitRemoteMovementOpcodeState(session, movement_info, presentation_tick,
                                  application_tick);

  if (handled) {
    owner_.Animation().HandleMovementOpcodeAnimationSideEffects(session, opcode);
    owner_.Animation().RefreshSelectedStandAnimation(session, 0u, ~0u);
  }

  return 1;
}

void UnitMovementRuntime::ImportRemoteMovementOpcodeState(
    WorldSession &session, const MovementInfo &movement_info,
    const std::uint32_t presentation_tick,
    const std::uint32_t application_tick) {
  data_.ImportMovementOpcodeSnapshot(movement_info);
  if (data_.HasParentMovement()) {
    return;
  }
  auto catch_up_ms =
      static_cast<std::int32_t>(application_tick - presentation_tick);
  std::uint32_t catch_up_cursor = presentation_tick;
  while (catch_up_ms > 0) {
    const auto step_ms = static_cast<std::uint32_t>(
        std::min<std::int32_t>(catch_up_ms, kRemoteMovementStepClampMs));
    catch_up_cursor += step_ms;
    AdvanceMovementStep(session, catch_up_cursor, step_ms, false);
    catch_up_ms -= static_cast<std::int32_t>(step_ms);
  }
}

void UnitMovementRuntime::CommitRemoteMovementOpcodeState(
    WorldSession &session, const MovementInfo &movement_info,
    const std::uint32_t presentation_tick,
    const std::uint32_t application_tick) {
  MovementOnlyUpdate update;
  update.guid = owner_.GetGuid();
  update.movement = owner_.GetMovementUpdate();
  update.movement.update_flags |= static_cast<std::uint16_t>(kUpdateFlagLiving);
  update.movement.movement = MovementInfoFromRuntimeData(
      data_, movement_info, session.objects(), movement_info.time);
  for (std::size_t index = 0; index < kMaxSpeeds; ++index) {
    update.movement.speeds[index] =
        data_.GetSpeed(static_cast<SpeedType>(index));
  }
  update.client_receive_tick_ms = application_tick;
  update.presentation_tick_ms = presentation_tick;
  update.has_resolved_presentation_tick = true;
  if (owner_.IsActiveMover()) {
    ApplyActiveMoverMovementUpdate(session, update);
  } else {
    session.objects().ApplyMovementUpdate(update);
  }
}

bool UnitMovementRuntime::HandleRemoteMoveChangeTransportSeat(
    WorldSession &session, const MovementInfo &movement_info,
    const std::uint32_t ) {

  const std::uint64_t requested_guid =
      movement_info.IsOnTransport()
          ? movement_info.transport.guid.GetRawValue()
          : 0u;
  const std::uint8_t requested_seat =
      requested_guid != 0u
          ? static_cast<std::uint8_t>(movement_info.transport.seat)
          : 0xFFu;
  (void)ApplyTransportChange(session, requested_guid, requested_seat,
                             true, true);

  const bool request_is_current =
      data_.GetTransportGuid() == requested_guid &&
      (requested_guid == 0u || data_.GetTransportSeat() == requested_seat);
  if (request_is_current) {

    const bool releases_spline_parent =
        data_.HasParentMovement() &&
        (data_.GetParentMovementFlags() & kParentMovementAllowStopFlag) == 0u;
    data_.ApplyTeleportArrivalPose(movement_info);
    if (releases_spline_parent) {
      session.movement_spline_mgr().CancelSpline(owner_.GetGuid().GetRawValue());
    }
  }

  return data_.HandleRemoteChangeTransportSeat();
}

int UnitMovementRuntime::ApplySplineMovementStateOpcode(
    WorldSession &session, const std::uint32_t opcode) {

  const std::uint32_t transaction_timestamp = session.CurrentClientTimeMs();
  data_.SetRuntimeFlags2(static_cast<std::uint16_t>(
      data_.GetRuntimeFlags2() & ~kSplineStopInterpolationFlags2Mask));
  session.movement_spline_mgr().CancelSpline(owner_.GetGuid().GetRawValue());
  const bool parent_allows_stop_finalization =
      !data_.HasParentMovement() ||
      (data_.GetParentMovementFlags() & kParentMovementAllowStopFlag) != 0u;
  FlushMovementStateForControlTransition(session, transaction_timestamp,
                                         parent_allows_stop_finalization);

  std::optional<movement::SplineMovementMode> mode;
  switch (opcode) {
    case 0x304u: mode = movement::SplineMovementMode::kUnroot; break;
    case 0x305u: mode = movement::SplineMovementMode::kFeatherFall; break;
    case 0x306u: mode = movement::SplineMovementMode::kNormalFall; break;
    case 0x307u: mode = movement::SplineMovementMode::kHover; break;
    case 0x308u: mode = movement::SplineMovementMode::kNoHover; break;
    case 0x309u: mode = movement::SplineMovementMode::kWaterWalk; break;
    case 0x30Au: mode = movement::SplineMovementMode::kLandWalk; break;
    case 0x30Bu: mode = movement::SplineMovementMode::kStartSwim; break;
    case 0x30Cu: mode = movement::SplineMovementMode::kStopSwim; break;
    case 0x30Du: mode = movement::SplineMovementMode::kRun; break;
    case 0x30Eu: mode = movement::SplineMovementMode::kWalk; break;
    case 0x31Au: mode = movement::SplineMovementMode::kRoot; break;
    case 0x422u: mode = movement::SplineMovementMode::kSetFlying; break;
    case 0x423u: mode = movement::SplineMovementMode::kUnsetFlying; break;
    case 0x4D3u: mode = movement::SplineMovementMode::kGravityDisable; break;
    case 0x4D4u: mode = movement::SplineMovementMode::kGravityEnable; break;
    default: return 0;
  }

  data_.ApplySplineMovementMode(*mode);
  CommitMovementRuntimeState(session, transaction_timestamp,
                             static_cast<std::uint16_t>(opcode));
  owner_.Animation().RefreshSelectedStandAnimation(session, 0u, ~0u);
  return 1;
}

int UnitMovementRuntime::DispatchSpeedAckOpcode(
    WorldSession &session, const std::uint32_t opcode,
    const MovementInfo& movement_info,
    const std::uint32_t client_receive_tick, const float value) {
  const auto route = RouteSpeedAckOpcode(opcode);
  if (!route.has_value()) {
    return 0;
  }

  if (owner_.IsActiveMover()) {
    data_.QueueSpeedChangeEvent(client_receive_tick,
                                route->event_type, value);
    return 1;
  }

  const bool parent_allows_transition_gate =
      !data_.HasParentMovement() ||
      (data_.GetParentMovementFlags() & kParentMovementAllowStopFlag) != 0u;
  if (parent_allows_transition_gate && HasPendingVehicleSeatChange(owner_)) {
    data_.QueueDeferredAuthoritativeMovement(
        client_receive_tick, static_cast<std::uint32_t>(route->event_type),
        opcode, movement_info, value);
    return 0;
  }

  const std::uint32_t runtime_current_tick =
      has_movement_update_tick_ ? movement_timeline_.current_tick
                                : client_receive_tick;
  const auto timing = data_.ResolveServerMovementTiming(
      client_receive_tick, movement_info.time, runtime_current_tick, false);
  if (!timing.apply_now) {
    data_.QueueDeferredAuthoritativeMovement(
        timing.presentation_tick,
        static_cast<std::uint32_t>(route->event_type), opcode, movement_info,
        value);
    return 0;
  }

  return ApplySpeedAckOpcodeState(session, opcode, movement_info,
                                  timing.presentation_tick,
                                  runtime_current_tick, value);
}

int UnitMovementRuntime::ApplySpeedAckOpcodeState(
    WorldSession &session, const std::uint32_t opcode,
    const MovementInfo &movement_info,
    const std::uint32_t presentation_tick,
    const std::uint32_t application_tick, const float value) {
  const auto route = RouteSpeedAckOpcode(opcode);
  if (!route.has_value()) {
    return 0;
  }

  ImportRemoteMovementOpcodeState(session, movement_info, presentation_tick,
                                  application_tick);
  bool handled = true;
  if (route->collision_height) {
    if ((movement_info.flags & kMoveFlagSplineEnabled) == 0u) {
      data_.ClearParentMovement();
    }
    data_.SetCollisionHeightProduct(value);
  } else {
    handled = data_.HandleRemoteSpeedAck(
        (movement_info.flags & kMoveFlagSplineEnabled) != 0u, value,
        route->speed_type);
  }

  CommitRemoteMovementOpcodeState(session, movement_info, presentation_tick,
                                  application_tick);
  if (handled) {
    owner_.Animation().HandleMovementOpcodeAnimationSideEffects(session, opcode);
    owner_.Animation().RefreshSelectedStandAnimation(session, 0u, ~0u);
  }
  return 1;
}

void UnitMovementRuntime::SendForward(WorldSession &session,
                                   const std::uint32_t timestamp,
                                   const bool forward) {
  if (owner_.IsActiveMover() && owner_.Interaction().IsAutoAttacking()) {
    owner_.Interaction().CompleteAutoAttackInteraction(false, true);
  }
  owner_.Interaction().CancelAutoAttackAndCheckLootClose(session, false, false);

  if (forward && owner_.Animation().GetStandState() != 0u) {
    owner_.Animation().MaybeStandUpIfPlayer(session, 0u);
  }
  data_.QueueForwardMove(timestamp, forward);
}

void UnitMovementRuntime::SendStrafe(WorldSession &session, const int direction,
                                  const std::uint32_t timestamp) {
  if (owner_.IsActiveMover() && owner_.Interaction().IsAutoAttacking()) {
    owner_.Interaction().CompleteAutoAttackInteraction(false, true);
  }
  owner_.Interaction().CancelAutoAttackAndCheckLootClose(session, false, false);

  if (direction != 0 && owner_.Animation().GetStandState() != 0u) {
    owner_.Animation().MaybeStandUpIfPlayer(session, 0u);
  }
  data_.QueueStrafeMove(timestamp, direction != 0);
}

void UnitMovementRuntime::SendVertical(WorldSession &session, const int direction,
                                    const std::uint32_t timestamp) {
  if (owner_.IsActiveMover() && owner_.Interaction().IsAutoAttacking()) {
    owner_.Interaction().CompleteAutoAttackInteraction(false, true);
  }
  owner_.Interaction().CancelAutoAttackAndCheckLootClose(session, false, false);
  data_.QueueVerticalMove(timestamp, direction != 0);
}

void UnitMovementRuntime::SendTurn(WorldSession &session, const int direction,
                                    const std::uint32_t timestamp) {
  const auto channel_spell_id = owner_.Casts().GetChannelSpellId(owner_);
  if (channel_spell_id != 0u) {
    const auto *const dbc = owner_.dbc_loader();
    const auto *const spell =
        dbc != nullptr ? dbc->spell().LookupEntry(channel_spell_id) : nullptr;
    if (spell != nullptr &&
        (spell->channel_interrupt_flags & kChannelInterruptFlagTurning) != 0u &&
        (spell->attributes_ex & kSpellAttrEx1Channeled) != 0u &&
        owner_.IsActiveMover() && owner_.Interaction().IsAutoAttacking()) {
      session.spells().SendCancelChannelling(session, channel_spell_id);
    }
  }
  owner_.Interaction().CancelAutoAttackAndCheckLootClose(session, true, false);

  if (direction != 0 && owner_.Animation().GetStandState() != 0u) {
    owner_.Animation().MaybeStandUpIfPlayer(session, 0u);
  }
  SendTurnMovement(timestamp, direction < 0);
}

void UnitMovementRuntime::SendPitch(WorldSession &session,
                                     const int direction,
                                     const std::uint32_t timestamp) {
  owner_.Interaction().CancelAutoAttackAndCheckLootClose(session, true, false);
  SendPitchMovement(timestamp, direction < 0);
}

void UnitMovementRuntime::SendStopPitch(WorldSession &session,
                                        const std::uint32_t timestamp) {
  owner_.Interaction().CancelAutoAttackAndCheckLootClose(session, true, false);
  data_.QueueStopPitch(timestamp);
}

void UnitMovementRuntime::ApplyInputControlMovement(
    WorldSession &session, CInputControl &control,
    const ProcessMovementDecision &decision) {
  if (decision.used_movement_rates) {
    if (decision.movement_rates_updated) {
      CommentatorState::Get().SetMovementRates(
          decision.movement_rates.forward,
          decision.movement_rates.strafe,
          decision.movement_rates.vertical,
          decision.movement_rates.turn_rate);
    }
    return;
  }

  const int raw_forward = control.ComputeNetForward();
  const int forward = decision.force_forward_override && raw_forward < 1
                          ? 1
                          : raw_forward;
  const int strafe = control.ComputeNetStrafe();
  const int turn = control.ComputeNetTurn(
      decision.allow_keyboard_turn_in_move_and_steer);
  const int pitch = control.ComputeNetPitch();
  const std::uint32_t flags = decision.movement_flags;

  if (decision.can_move) {

    if (forward > 0) {
      SendForward(session, decision.timestamp, true);
      control.MarkForwardSent(true);
    } else if (forward < 0) {
      SendForward(session, decision.timestamp, false);
      control.MarkForwardSent(true);
    } else {
      if (control.IsForwardSent()) {
        StopForward(decision.timestamp);
      }
      control.MarkForwardSent(false);
    }

    if (strafe > 0) {
      SendStrafe(session, 1, decision.timestamp);
      control.MarkStrafeSent(true);
    } else if (strafe < 0) {
      SendStrafe(session, 0, decision.timestamp);
      control.MarkStrafeSent(true);
    } else {
      if (control.IsStrafeSent()) {
        StopStrafe(decision.timestamp);
      }
      control.MarkStrafeSent(false);
    }

    const auto vertical = control.ResolveVerticalMovementDecision(
        flags, decision.movement_flags2);
    switch (vertical.command) {
      case VerticalMovementCommand::kStartPitchUp:
        SendPitch(session, 1, decision.timestamp);
        break;
      case VerticalMovementCommand::kStartPitchDown:
        SendPitch(session, -1, decision.timestamp);
        break;
      case VerticalMovementCommand::kStopPitch:
        SendStopPitch(session, decision.timestamp);
        break;
      case VerticalMovementCommand::kStartAscend:
        SendVertical(session, 1, decision.timestamp);
        break;
      case VerticalMovementCommand::kStartDescend:
        SendVertical(session, 0, decision.timestamp);
        break;
      case VerticalMovementCommand::kStopVertical:
        StopVertical(decision.timestamp);
        break;
      case VerticalMovementCommand::kNoChange:
        break;
    }
    control.MarkVerticalSent(vertical.sent_state_after);
  } else {
    if (decision.vehicle_control_allows_free_movement) {
      if (control.IsForwardSent()) {
        StopForward(decision.timestamp);
      }
      if (control.IsStrafeSent()) {
        StopStrafe(decision.timestamp);
      }
      if (control.IsVerticalSent()) {
        StopVertical(decision.timestamp);
      }
    }
    control.MarkForwardSent(false);
    control.MarkStrafeSent(false);
    control.MarkVerticalSent(false);
  }

  const bool allow_pitch =
      (flags & (kMoveFlagSwimming | kMoveFlagFlying)) != 0u ||
      (decision.movement_flags2 & kMoveFlag2AlwaysAllowPitching) != 0u;
  if (decision.can_turn) {

    if (turn > 0) {
      SendTurn(session, 1, decision.timestamp);
      control.MarkTurnSent(true);
    } else if (turn < 0) {
      SendTurn(session, -1, decision.timestamp);
      control.MarkTurnSent(true);
    } else {
      if (control.IsTurnSent()) {
        StopTurn(decision.timestamp);
      }
      control.MarkTurnSent(false);
    }

    if (allow_pitch) {
      if (pitch > 0) {
        if (!control.IsPitchSent() ||
            (flags & kMoveFlagPitchDown) != 0u) {
          SendPitch(session, 1, decision.timestamp);
        }
        control.MarkPitchSent(true);
      } else if (pitch < 0) {
        if (!control.IsPitchSent() ||
            (flags & kMoveFlagPitchUp) != 0u) {
          SendPitch(session, -1, decision.timestamp);
        }
        control.MarkPitchSent(true);
      } else {
        if (control.IsPitchSent()) {
          SendStopPitch(session, decision.timestamp);
        }
        control.MarkPitchSent(false);
      }
    }
  } else {
    if (decision.vehicle_control_allows_free_movement) {
      if (control.IsTurnSent()) {
        StopTurn(decision.timestamp);
      }
      if (control.IsPitchSent() &&
          (flags & (kMoveFlagSwimming | kMoveFlagFlying)) != 0u) {
        SendStopPitch(session, decision.timestamp);
      }
    }
    control.MarkTurnSent(false);
    control.MarkPitchSent(false);
  }
}

bool UnitMovementRuntime::SendSimpleMovePacket(
    const WorldSession &session, const std::uint16_t opcode,
    const std::uint32_t timestamp) {
  const auto movement_info = MovementInfoFromRuntimeData(
      data_, owner_.GetMovementInfo(), session.objects(), timestamp,
      true);
  const auto packet = net::wotlk::PacketSender::BuildMovement(
      static_cast<net::wotlk::Opcode>(opcode), owner_.GetGuid(), movement_info);
  const bool sent = net::ClientServices__SendPacket(packet);
  if (sent) {
    UpdateWireAnnouncedFallingLatch(owner_, data_.GetRuntimeFlags());
  }
  return sent;
}

bool UnitMovementRuntime::SendImmediateMovementPacket(
    const WorldSession &session, const std::uint16_t opcode,
    const std::uint32_t timestamp) {
  const bool sent = SendSimpleMovePacket(session, opcode, timestamp);
  if (sent) {
    NoteSuccessfulMovementPacket(timestamp);
  }
  return sent;
}

std::uint32_t PlayerControlRuntime::GetActiveMoverAutoAttackType(
    const ObjectManager &objects) const {

  const auto mover_guid = objects.player_control().active_mover_guid;
  if (mover_guid == 0u) {
    return kAutoAttackTypeIdle;
  }
  const auto *const object =
      CGObject_HasFlags(objects, mover_guid, kUnitTypeMask);
  if (object == nullptr) {
    return kAutoAttackTypeIdle;
  }
  return static_cast<const CGUnit_C *>(object)->Interaction().AutoAttackType();
}

void PlayerControlRuntime::SetActiveMover(
    WorldSession &session, ObjectManager &objects,
  UnitMissileTrajectory_C &missile_trajectory,
  const std::uint64_t new_guid) {
  auto &control = session.player_control_runtime();
  if (control.active_mover_guid == new_guid) {
    return;
  }

  const auto timestamp = core::GameClock::GetTickCount32();
  const auto active_player_guid = objects.GetActivePlayerGuid();
  if (auto *const old_mover =
          objects.GetMutableUnit(ObjectGuid(control.active_mover_guid));
      old_mover != nullptr) {
    old_mover->Interaction().CompleteAutoAttackInteraction(false, true);
    auto &old_movement_data = old_mover->Movement().Data();
    old_movement_data.SetRuntimeFlags(
        old_movement_data.GetRuntimeFlags() & 0x7FFFFFFFu);
    if (control.active_mover_guid != active_player_guid.GetRawValue()) {
      old_movement_data.SetRuntimeFlags(
          old_movement_data.GetRuntimeFlags() & ~kMoveFlagOnTransport);
    }
    const bool allow_stop =
        !old_movement_data.HasParentMovement() ||
        (old_movement_data.GetParentMovementFlags() &
         kParentMovementAllowStopFlag) != 0u;
    if (allow_stop) {
      old_mover->Movement().FinalizeActiveMoverRelease(session, timestamp);
      if (ObjectGuid(control.active_mover_guid) == active_player_guid) {
        old_mover->Movement().SendSimpleMovePacket(session, 721u, timestamp);
      }
    }
  }

  control.active_mover_guid = new_guid;
  if (new_guid != 0u) {
    const auto packet = net::wotlk::PacketSender::BuildSetActiveMover(new_guid);
    static_cast<void>(net::ClientServices__SendPacket(packet));
  }

  auto *const new_mover =
      objects.GetMutableUnit(ObjectGuid(control.active_mover_guid));
  if (new_mover != nullptr) {

    ui::game::ScriptEventDispatch::Get().FireEvent(
        ui::game::events::VEHICLE_UPDATE);

    session.movement().ApplyAuthoritativeMovementInfo(
        new_mover->GetMovementInfo());
    for (std::size_t index = 0; index < kMaxSpeeds; ++index) {
      session.movement().SetSpeed(
          static_cast<SpeedType>(index),
          new_mover->GetSpeed(static_cast<SpeedType>(index)));
    }
    auto &movement_runtime = new_mover->Movement();
    if (ObjectGuid(control.active_mover_guid) != active_player_guid) {
      const auto *const player = objects.GetMutableUnit(active_player_guid);
      if (player != nullptr &&
          (player->Movement().Data().GetRuntimeFlags() &
           kMoveFlagOnTransport) != 0u) {
        auto &new_movement_data = new_mover->Movement().Data();
        new_movement_data.SetRuntimeFlags(
            new_movement_data.GetRuntimeFlags() | kMoveFlagOnTransport);
      }
    }
    movement_runtime.PrepareActiveMoverControl(session, timestamp);
  }
  UnitPresentationRuntime::UpdateCameraTargetAndMissilePreview(
      missile_trajectory, new_mover);
}

void UnitMovementRuntime::RebaseAutoAttackForTransportChange(const float* matrix4x4,
                                                   const float facing_delta) {
  owner_.Interaction().RebaseAutoAttackForTransportChange(matrix4x4,
                                                           facing_delta);
}

void UnitMovementRuntime::RebaseAutoAttackForTransportGuidChange(
    ObjectManager &objects, const std::uint64_t old_transport_guid,
    const std::uint64_t new_transport_guid) {
  if (old_transport_guid == new_transport_guid) {
    return;
  }
  if (old_transport_guid != 0u) {
    float old_matrix[16];
    if (Movement_GetObjectTransform(objects, old_transport_guid,
                                    old_matrix) != 0) {
      RebaseAutoAttackForTransportChange(
          old_matrix,
          Movement_GetObjectOrientation(objects, old_transport_guid));
    }
  }
  if (new_transport_guid != 0u) {
    float new_matrix[16];
    if (Movement_GetObjectTransform(objects, new_transport_guid,
                                    new_matrix) != 0) {
      float new_inverse[16];
      openwow::math::row_major_mat4x4::BuildInverseRigidTransform4x4(
          new_inverse, new_matrix);
      RebaseAutoAttackForTransportChange(
          new_inverse,
          -Movement_GetObjectOrientation(objects, new_transport_guid));
    }
  }
}

void UnitMovementRuntime::RebaseTransportSplineState(
    const float *const matrix4x4, const float facing_delta,
    const bool leaving_parent, const std::uint64_t parent_guid,
    const std::uint8_t parent_seat) {
  if (matrix4x4 == nullptr || !spline_movement_pose_owned_ ||
      !spline_pose_waiting_for_parent_) {
    return;
  }

  const ObjectGuid expected_parent(parent_guid);
  if (leaving_parent && spline_coordinate_parent_ != expected_parent) {
    return;
  }
  if (!leaving_parent && !spline_coordinate_parent_.IsEmpty() &&
      spline_coordinate_parent_ != expected_parent) {
    return;
  }

  const float source_position[3] = {spline_local_position_.x,
                                    spline_local_position_.y,
                                    spline_local_position_.z};
  float rebased_position[3] = {};
  Passenger_TransformPointByMatrix(rebased_position, source_position,
                                   matrix4x4);
  spline_local_position_ =
      {rebased_position[0], rebased_position[1], rebased_position[2]};
  spline_local_facing_ = Movement_NormalizeFacing0ToTau(
      spline_local_facing_ + facing_delta);

  if (leaving_parent) {
    spline_coordinate_parent_ = {};
    spline_coordinate_parent_seat_ = -1;
  } else {
    spline_coordinate_parent_ = expected_parent;
    spline_coordinate_parent_seat_ =
        static_cast<std::int8_t>(parent_seat);
  }
}

void UnitMovementRuntime::ApplyTransportParentRebaseSideEffects(
    ObjectManager &objects, const bool leaving_parent,
    const std::uint64_t parent_guid, const float body_facing_delta) {

  OffsetBodyFacingAngles(body_facing_delta);

  float parent_transform[16];
  if (Movement_GetObjectTransform(objects, parent_guid, parent_transform) ==
      0) {
    return;
  }
  if (leaving_parent) {
    RebaseTransportSplineState(parent_transform, body_facing_delta,
                               true, parent_guid, 0xffu);
    RebaseAutoAttackForTransportChange(parent_transform, body_facing_delta);

    float identity[16] = {};
    identity[0] = 1.0f;
    identity[5] = 1.0f;
    identity[10] = 1.0f;
    identity[15] = 1.0f;
    owner_.ApplyModelParentTransform(identity);
    owner_.Presentation().RefreshModelBoundsAndEffectsForced();
    Movement_NotifyVehicle(objects, 0, 0, parent_guid);
  } else {
    float parent_inverse[16];
    openwow::math::row_major_mat4x4::BuildInverseRigidTransform4x4(
        parent_inverse, parent_transform);
    RebaseTransportSplineState(parent_inverse, body_facing_delta,
                               false, parent_guid,
                               data_.GetTransportSeat());
    RebaseAutoAttackForTransportChange(parent_inverse, body_facing_delta);

    owner_.ApplyModelParentTransform(parent_transform);
    owner_.Presentation().RefreshModelBoundsAndEffectsForced();
  }
}

UnitMovementRuntime::TransportChangeResult
UnitMovementRuntime::ApplyTransportChange(
    WorldSession &session, const std::uint64_t transport_guid,
    const std::uint8_t seat,
    const bool seed_from_owner, const bool force) {
  ObjectManager &objects = session.objects();
  openwow::world::WorldCamera *const camera = session.world_camera();
  if (seed_from_owner) {
    SeedMovementTransportState(data_, owner_.GetMovementInfo());
  }
  const std::uint64_t old_guid = data_.GetTransportGuid();
  const std::uint32_t transport_timestamp = core::GameClock::GetTickCount32();

  const auto boundary_world_before = owner_.GetPosition();
  const float boundary_world_facing_before = owner_.GetWorldFacing();
  const auto boundary_local_before = data_.GetTransformPosition();
  const float boundary_local_facing_before = data_.GetScalarFacing();

  std::optional<float> old_parent_body_facing_delta;
  std::optional<float> new_parent_body_facing_delta;
  const bool request_applied = data_.ForceSetTransport(
      objects, transport_guid, seat, force, &old_parent_body_facing_delta,
      &new_parent_body_facing_delta,
      [this, old_guid, transport_guid]() {
        if (transport_guid == 0u) {
          return;
        }
        if ((data_.GetRuntimeFlags() & kMoveFlagSwimming) != 0u) {

          data_.StopSwimmingForTransportAttach();
        }
        if (owner_.IsActivePlayer() && old_guid != 0u &&
            old_guid != transport_guid) {
          core::CMovementRuntime_MarkTransportTimestampTransition();
        }
        if ((data_.GetRuntimeFlags() &
             (kMoveFlagAscending | kMoveFlagDescending)) != 0u) {

          data_.SnapshotStateForDirectionRecompute();
        }
      },
      [this, &session, transport_timestamp](const std::uint64_t old_parent_guid,
                       const std::uint64_t new_parent_guid,
                       const std::uint8_t new_seat) {

        if (Movement_IsVehicleOrPlayerGuid(old_parent_guid) ||
            Movement_IsVehicleOrPlayerGuid(new_parent_guid)) {
          UnitVehicle_ProcessSeatChange(
              session, &owner_,
              static_cast<double>(transport_timestamp),
              new_parent_guid, new_seat, false);
        }
      },
      [this, &objects](const bool leaving_parent,
                       const std::uint64_t parent_guid,
                       const float body_facing_delta) {
        ApplyTransportParentRebaseSideEffects(objects, leaving_parent,
                                              parent_guid, body_facing_delta);
      });
  const std::uint64_t new_guid = data_.GetTransportGuid();
  if (!request_applied && new_guid == old_guid) {
    return TransportChangeResult::kUnchanged;
  }
  if (!request_applied && old_guid != 0u && new_guid == 0u) {

    const MovementInfo previous_movement = owner_.GetMovementInfo();
    const MovementInfo cleared_movement =
        BuildTransportMovementInfo(previous_movement, data_);
    owner_.SetMovementInfo(cleared_movement);
    SynchronizeUnitBoundWorldCamera(owner_, camera);
    objects.SynchronizeUnitTransportPassengerMembership(owner_,
                                                        previous_movement);
    return TransportChangeResult::kClearedStaleParent;
  }

  ForgetMovementParentTransform();

  const MovementInfo previous_movement = owner_.GetMovementInfo();
  const MovementInfo updated_movement =
      BuildTransportMovementInfo(owner_.GetMovementInfo(), data_);
  owner_.SetMovementInfo(updated_movement);
  SynchronizeUnitBoundWorldCamera(owner_, camera);
  objects.SynchronizeUnitTransportPassengerMembership(owner_,
                                                      previous_movement);

  if (old_guid != new_guid && (owner_.IsActiveMover() || owner_.IsPlayer())) {
    const auto boundary_world_after = owner_.GetPosition();
    const float boundary_world_facing_after = owner_.GetWorldFacing();
    const auto boundary_local_after = data_.GetTransformPosition();
    const float boundary_local_facing_after = data_.GetScalarFacing();
    const char *const boundary_kind = new_guid != 0u ? "attach" : "detach";
    diagnostics::Log(
        diagnostics::LogLevel::kInfo,
        std::string(boundary_kind) + ": old_guid=" + std::to_string(old_guid) +
            " new_guid=" + std::to_string(new_guid) + " world_before=(" +
            std::to_string(boundary_world_before.x) + "," +
            std::to_string(boundary_world_before.y) + "," +
            std::to_string(boundary_world_before.z) + ") world_o_before=" +
            std::to_string(boundary_world_facing_before) + " local_before=(" +
            std::to_string(boundary_local_before[0]) + "," +
            std::to_string(boundary_local_before[1]) + "," +
            std::to_string(boundary_local_before[2]) + ") local_o_before=" +
            std::to_string(boundary_local_facing_before) + " world_after=(" +
            std::to_string(boundary_world_after.x) + "," +
            std::to_string(boundary_world_after.y) + "," +
            std::to_string(boundary_world_after.z) + ") world_o_after=" +
            std::to_string(boundary_world_facing_after) + " local_after=(" +
            std::to_string(boundary_local_after[0]) + "," +
            std::to_string(boundary_local_after[1]) + "," +
            std::to_string(boundary_local_after[2]) + ") local_o_after=" +
            std::to_string(boundary_local_facing_after));
  }

  return request_applied ? TransportChangeResult::kApplied
                         : TransportChangeResult::kClearedStaleParent;
}

bool UnitMovementRuntime::ForceSetTransport(
    WorldSession &session, const std::uint64_t transport_guid,
    const std::uint8_t seat, const bool force) {
  const auto result = ApplyTransportChange(session, transport_guid, seat,
                                            false, force);
  if (result == TransportChangeResult::kUnchanged) {
    return false;
  }
  if (result == TransportChangeResult::kClearedStaleParent) {
    return false;
  }
  if (owner_.IsActiveMover()) {
    session.movement().ApplyAuthoritativeMovementInfo(
        owner_.GetMovementInfo());
  }
  if (SendActiveMoverTransportChangePacket(
          session, owner_, data_, owner_.GetMovementInfo())) {
    UpdateWireAnnouncedFallingLatch(owner_, data_.GetRuntimeFlags());
    NoteSuccessfulMovementPacket(session.CurrentClientTimeMs());
  }
  return result == TransportChangeResult::kApplied;
}

bool UnitMovementRuntime::ApplyTransportMotionCollision(
    WorldSession &session, const TransportMotionStep &step,
    const std::uint32_t client_time_ms) {
  constexpr std::uint8_t kNoTransportSeat = 0xffu;

  if (step.distance <= 0.0f || step.elapsed_ms == 0u ||
      data_.GetTransportGuid() != step.guid.GetRawValue()) {
    return false;
  }

  const std::shared_ptr<MovementCollisionSolver> solver =
      session.GetMovementCollisionSolver();
  if (!solver || !solver->IsBound()) {
    return false;
  }

  movement::CMovementData::PassengerCollisionCallbacks callbacks;
  const CGObject_C *const transport_object =
      session.objects().Get(step.guid);
  if (transport_object == nullptr) {
    return false;
  }
  const std::uint32_t object_timestamp_offset =
      transport_object->GetObjectTimeOffsetMs();
  if (const auto *const transport_game_object =
          dynamic_cast<const CGGameObject_C *>(transport_object);
      transport_game_object != nullptr) {
    callbacks.map_transport_timestamp =
        [transport_game_object](const std::uint32_t raw_timestamp) {
          return transport_game_object->MapMOTransportMovementTimestamp(
              raw_timestamp);
        };
  }
  callbacks.get_unit_state =
      [this](const movement::CMovementData &) {
    movement::CMovementData::PassengerCollisionUnitState state;
    state.is_navigable_as_player = IsNavigableAsPlayer();
    state.is_ghost_player = IsGhostPlayerDescriptorPair();
    state.has_character_control =
        !data_.HasParentMovement() ||
        (data_.GetParentMovementFlags() & kParentMovementAllowStopFlag) != 0u;
    return state;
  };
  callbacks.trace_world =
      [solver, this](const float *const source_aabb,
               const float *const source_position,
               const float *const sweep_direction,
               const float sweep_distance,
               const std::uint32_t collision_mask)
          -> std::optional<movement::CMovementData::PassengerCollisionHit> {
    if (source_aabb == nullptr || source_position == nullptr ||
        sweep_direction == nullptr || !std::isfinite(sweep_distance) ||
        sweep_distance <= 0.0f) {
      return std::nullopt;
    }

    MovementCollisionBody body;
    body.position = {source_position[0], source_position[1], source_position[2]};
    body.radius = data_.GetCollisionHalfWidth();
    body.height = data_.GetCollisionHeightProduct();
    body.step_height = 0.0f;
    body.mode = MovementCollisionMode::kSpecial;
    body.collision_mask = collision_mask;

    const C3Vector displacement{ sweep_direction[0] * sweep_distance,
                                 sweep_direction[1] * sweep_distance,
                                 sweep_direction[2] * sweep_distance };
    bool query_ok = false;
    const MovementCollisionTrace trace =
        solver->SweepHull(body, displacement, &query_ok);
    if (!query_ok || !trace.hit || trace.contacts.empty()) {
      return std::nullopt;
    }
    const MovementCollisionContact &contact = trace.contacts.front();
    movement::CMovementData::PassengerCollisionHit hit;
    hit.distance = contact.distance;
    hit.normal = {contact.normal.x, contact.normal.y, contact.normal.z};
    return hit;
  };
  callbacks.force_set_transport_notify =
      [this, &session](movement::CMovementData &) {

    const auto result = ApplyTransportChange(
        session, 0u, kNoTransportSeat, false);
    if (result != TransportChangeResult::kApplied) {
      return;
    }
    if (owner_.IsActiveMover()) {
      session.movement().ApplyAuthoritativeMovementInfo(
          owner_.GetMovementInfo());
    }
    if (SendActiveMoverTransportChangePacket(
            session, owner_, data_, owner_.GetMovementInfo())) {
      UpdateWireAnnouncedFallingLatch(owner_, data_.GetRuntimeFlags());
      NoteSuccessfulMovementPacket(session.CurrentClientTimeMs());
    }
  };
  const float sweep_direction[3] = {step.direction.x, step.direction.y,
                                    step.direction.z};
  return data_.TestUnitBone(client_time_ms, object_timestamp_offset,
                            step.elapsed_ms, step.distance, sweep_direction,
                            step.world_transform.data(), callbacks);
}

void UnitMovementRuntime::InputControlClearClickToMoveFacingFlags(
    const std::uint32_t timestamp) {

  InputControl_ApplyControlFlagChange(kCtrlClickToMoveFacing, false, timestamp);
  InputControl_ApplyControlFlagChange(kCtrlClickToMovePending, false, timestamp);
}

void UnitMovementRuntime::InputControlStopForward(const std::uint32_t timestamp) {

  constexpr std::uint32_t kUnitFlags2ForceMovement = 0x40u;
  if ((owner_.State().GetUnitFlags2() & kUnitFlags2ForceMovement) != 0u) {
    return;
  }

  if (owner_.Vehicle().HasVehicleSeatMovementBlock(owner_)) {
    return;
  }

  InputControl_ApplyControlFlagChange(kCtrlClickToMoveForward, false, timestamp);

  const auto gate_state = BuildInputControlStopForwardGateState(owner_);
  if (!CInputControl::IsPlayerAliveAndFree(gate_state)) {
    return;
  }

  const auto flags = owner_.GetMovementInfo().flags;
  if ((flags & (kMoveFlagForward | kMoveFlagBackward)) == 0u) {
    return;
  }

  {
    auto *const objects = owner_.object_manager();
    if (objects == nullptr) {
      return;
    }
    const ScopedMovementInteractionFlag guard(objects->player_control(), 0x1u);
    data_.QueueStopForward(timestamp);
    if (auto *ctrl = GetInputControlSingleton(); ctrl != nullptr) {
      ctrl->ClearForwardSentAndAutoRun();
    }
  }
}

void UnitMovementRuntime::SetInputControlPitchFlag(const bool enable,
                                        const std::uint32_t timestamp) {
  (void)InputControl_ApplyControlFlagChange(kCtrlClickToMovePending, enable,
                                            timestamp);
}

void UnitMovementRuntime::SendJump(WorldSession &session, const std::uint32_t timestamp) {
  if (owner_.IsActiveMover()) {
    const auto* const objects = owner_.object_manager();
    if (const auto* const active_player =
            objects != nullptr ? objects->GetActivePlayer() : nullptr;
        active_player != nullptr &&
        active_player->Animation().StandSelectionInteractionTargetGuid() != 0u) {
      CloseActiveLootWindow(
          session,
          CloseLootWindowOptions{
              .send_release = true,
              .skip_item_check = true,
              .show_interrupted = false,
              .clear_dead_target = true,
          });
    }
  }

  if (owner_.State().HasSpellStateFlags(kSpellStateDirectJumpQueue)) {

    SwimToFly(timestamp, true);
    return;
  }

  const MovementInfo& movement_info = owner_.GetMovementInfo();
  const bool queue_jump =
      !owner_.Mount().IsMountedStateActive(owner_) ||
      HasCanFlyFlag() ||
      HasJumpQueueSeatFlag(owner_) ||
      (HasJumpMovementFlags(movement_info) &&
       !AllowsMountedJumpWhileMoving(owner_, movement_info));
  if (queue_jump) {
    data_.QueueJump(timestamp);
    return;
  }

  if (HasJumpTurnFlags(movement_info)) {
    return;
  }

  const auto liquid_surface_height = ResolveJumpLiquidSurfaceHeight(owner_);
  const Position position = owner_.GetPosition();
  if (ShouldSuppressMountedSpecialForLiquidDepth(
          position.z, liquid_surface_height.value_or(0.0f),
          data_.GetCollisionHeightProduct(),
          liquid_surface_height.has_value())) {
    return;
  }

  auto* const objects = owner_.object_manager();
  if (auto* const active_player =
          objects != nullptr ? objects->GetActivePlayer() : nullptr;
      active_player != nullptr && active_player->GetGuid() == owner_.GetGuid()) {
    active_player->Animation().PlayEmoteAnimation(94, false);
  }

  SendRawMountedJumpPacket();
}

void UnitMovementRuntime::GameUIAutoWalk(WorldSession &session, const float *direction) {
  if (direction == nullptr) {
    return;
  }

  const float len_sq = direction[0] * direction[0] +
                       direction[1] * direction[1] +
                       direction[2] * direction[2];
  if (len_sq == 0.0f) {
    return;
  }

  const float inv_len = 1.0f / std::sqrt(len_sq);
  const float dir_x = direction[0] * inv_len;
  const float dir_y = direction[1] * inv_len;
  const float dir_z = direction[2] * inv_len;
  const std::uint32_t timestamp = core::GameClock::GetTickCount32();

  SendSetFacing(session, timestamp, std::atan2(dir_y, dir_x));

  if ((owner_.GetMovementInfo().flags & 0x02200000u) != 0u) {
    constexpr float kSteepThreshold = 0.86602002f;
    constexpr float kDeadZoneThreshold = 0.17364f;
    constexpr float kHalfPi = 1.5707964f;

    const float abs_dz = std::fabs(dir_z);
    float pitch = 0.0f;

    if (abs_dz > kSteepThreshold) {
      pitch = math::CopySignFloat(kHalfPi, dir_z);
    } else if (abs_dz > kDeadZoneThreshold) {
      pitch = std::asin(dir_z);
    }

    SendSetPitch(session, timestamp, pitch);
  }

  if ((owner_.GetMovementInfo().flags & kMoveFlagForward) == 0u) {
    SendForward(session, timestamp, true);
  }
}

bool UnitMovementRuntime::InterpolateSwimHeight(WorldSession &session,
                                     const float water_level,
                                     const float current_z,
                                     const float target_pitch,
                                     const float delta_step,
                                     const std::int32_t timestamp,
                                     bool *out_steep) {
  static constexpr float kEpsilon = 0.001f;
  static constexpr float kEquilibriumScale = 0.75f;
  static constexpr float kSteepThreshold = 0.39269909f;

  const auto send_pitch_if_alive =
      [this, &session, timestamp](const float pitch) {
        GetInputControlSingleton();
        const PlayerMovementGateState gate_state{
            .health = static_cast<std::int32_t>(owner_.State().GetHealth()),
            .has_knockdown_animation = owner_.Mount().HasKnockdownAnimation(owner_),
            .is_active_player = owner_.IsActivePlayer(),
            .vehicle_control_allows_free_movement = true,
            .is_in_vehicle_transition =
                UnitVehicle_IsActivePlayerInVehicle(&owner_),
        };
        if (!CInputControl::IsPlayerAliveAndFree(gate_state)) {
          return;
        }

        const ScopedMovementInteractionFlag suppress_auto_attack_cancel(
            session.player_control_runtime(), 0x1u);
        SendSetPitch(session, static_cast<std::uint32_t>(timestamp), pitch);
      };

  if (current_z < water_level) {
    const Position position = owner_.GetPosition();
    const float query_position[3] = {position.x, position.y, current_z};
    UnitSoundGroundState ground_state{};
    if (UnitSound_QueryGroundState(owner_, query_position, ground_state) &&
        ground_state.has_liquid_surface &&
        current_z < ground_state.liquid_surface_z) {
      const float equilibrium =
          data_.GetCollisionHeightProduct() * kEquilibriumScale;
      if (std::fabs((ground_state.liquid_surface_z - current_z) - equilibrium) <
          kEpsilon) {
        if (data_.GetRuntimePitch() != 0.0f) {
          SetInputControlPitchFlag(
              true, static_cast<std::uint32_t>(timestamp));
          send_pitch_if_alive(0.0f);
        }
        return false;
      }
    }
  }

  SetInputControlPitchFlag(true, static_cast<std::uint32_t>(timestamp));

  const float current_pitch = data_.GetRuntimePitch();
  if (std::fabs(target_pitch - current_pitch) < kEpsilon) {
    return false;
  }

  float new_pitch;
  if (target_pitch <= current_pitch) {
    new_pitch = std::max(target_pitch, current_pitch - delta_step);
  } else {
    new_pitch = std::min(target_pitch, current_pitch + delta_step);
  }

  send_pitch_if_alive(new_pitch);
  *out_steep = std::fabs(new_pitch - target_pitch) > kSteepThreshold;
  return true;
}

void UnitMovementRuntime::InstallUpdateScopedCallbacks() {

  data_.SetDispatchMovementOpcodeCallback(
      [this](movement::CMovementData &, const std::uint16_t opcode,
             const std::uint32_t timestamp,
             const movement::CPlayerMoveEvent &event) {
        if (!update_callbacks_live_) {
          return;
        }
        CommitMovementRuntimeState(*update_session_, timestamp, opcode,
                                   &event);
      });

  data_.SetDispatchStopOpcodeCallback(
      [this](movement::CMovementData &, const std::uint32_t opcode) {
        if (!update_callbacks_live_) {
          return;
        }
        WorldSession &session = *update_session_;
        const std::uint32_t tick_ms = update_dispatch_tick_ms_;
        CommitMovementRuntimeState(
            session, tick_ms, static_cast<std::uint16_t>(opcode));
        if (owner_.IsActiveMover()) {
          (void)SendImmediateMovementPacket(
              session, static_cast<std::uint16_t>(opcode), tick_ms);
        }
      });
  data_.SetDispatchDeferredAuthoritativeMovementCallback(
      [this](movement::CMovementData &,
             const movement::CPlayerMoveEvent &event) {
        if (!update_callbacks_live_) {
          return;
        }
        WorldSession &session = *update_session_;
        if (IsDeferredSpeedAckEvent(event.event_type)) {
          (void)ApplySpeedAckOpcodeState(
              session, event.deferred_authoritative_opcode,
              *event.deferred_authoritative_movement, event.timestamp,
              event.timestamp, event.auxiliary_f32);
        } else {
          (void)ApplyClientMovementOpcodeState(
              session, event.deferred_authoritative_opcode,
              *event.deferred_authoritative_movement, event.timestamp,
              event.timestamp);
        }
      });

  data_.SetTransportSeatChangeCallback(
      [this](const std::uint64_t, const std::uint64_t new_transport_guid,
             const std::uint8_t transport_seat) {
        if (!update_callbacks_live_) {
          return;
        }
        UnitVehicle_ProcessSeatChange(
            *update_session_, &owner_,
            static_cast<double>(core::GameClock::GetTickCount32()),
            new_transport_guid, transport_seat, false);
      });
  data_.SetTransportParentCommitCallback(
      [this](const std::uint64_t old_transport_guid,
             const std::uint64_t new_transport_guid) {
        if (!update_callbacks_live_) {
          return;
        }
        if (new_transport_guid == 0u) {
          return;
        }
        if ((data_.GetRuntimeFlags() & kMoveFlagSwimming) != 0u) {
          data_.StopSwimmingForTransportAttach();
        }
        if (owner_.IsActivePlayer() && old_transport_guid != 0u &&
            old_transport_guid != new_transport_guid) {
          core::CMovementRuntime_MarkTransportTimestampTransition();
        }
        if ((data_.GetRuntimeFlags() &
             (kMoveFlagAscending | kMoveFlagDescending)) != 0u) {
          data_.SnapshotStateForDirectionRecompute();
        }
      });
  data_.SetTransportParentRebaseCallback(
      [this](const bool leaving_parent, const std::uint64_t parent_guid,
             const float body_facing_delta) {
        if (!update_callbacks_live_) {
          return;
        }
        ApplyTransportParentRebaseSideEffects(update_session_->objects(),
                                              leaving_parent, parent_guid,
                                              body_facing_delta);
      });
}

void UnitMovementRuntime::Update(WorldSession &session,
                                 const std::uint32_t current_tick_ms) {

  owner_.Animation().RunPendingStandSelectorRefresh(session);
  if (!owner_.position_.IsLiving()) {
    return;
  }

  const bool active_mover = owner_.IsActiveMover();
  if (active_mover) {
    data_.RefreshQueuedMovementPreview(current_tick_ms);
  }

  update_session_ = &session;
  update_dispatch_tick_ms_ = current_tick_ms;

  if (!persistent_callbacks_installed_) {
    movement::CMovementData::UpdateCallbacks callbacks;
    callbacks.is_active_mover =
        [this](const movement::CMovementData &) { return owner_.IsActiveMover(); };
    callbacks.is_active_player =
        [this](const movement::CMovementData &) { return owner_.IsActivePlayer(); };
    callbacks.process_movement_loop =
        [this](movement::CMovementData &runtime,
               const std::uint32_t timestamp,
               const std::uint32_t step_ms) {
          if (spline_movement_pose_owned_) {
            return true;
          }
          (void)runtime;
          AdvanceMovementStep(*update_session_, timestamp, step_ms);
          return true;
        };
    callbacks.clear_input_control_on_stop =
        [this](movement::CMovementData &) {
          if (owner_.IsActiveMover()) {
            if (auto *const input = GetInputControlSingleton(); input != nullptr) {
              input->ClearForwardSentAndAutoRun();
            }
          }
        };
    callbacks.finalize_stopped =
        [](movement::CMovementData &runtime, const bool) {
          runtime.ProcessPendingMovementStops();
        };
    callbacks.vehicle_post_update =
        [this](movement::CMovementData &, const std::uint32_t end_time) {
          update_flags_ &= ~kMovementUpdateVehiclePostBit;

          const auto signed_frame_delta = static_cast<std::int32_t>(
              end_time - last_movement_update_tick_);
          const auto frame_delta_ms = signed_frame_delta > 0
                                          ? static_cast<std::uint32_t>(
                                                signed_frame_delta)
                                          : 0u;
          RunMovementPostUpdate(*update_session_, end_time, frame_delta_ms);
        };
    callbacks.send_heartbeat =
        [this](movement::CMovementData &, const std::uint32_t timestamp) {
          (void)SendImmediateMovementPacket(
              *update_session_,
              static_cast<std::uint16_t>(net::wotlk::Opcode::MSG_MOVE_HEARTBEAT),
              timestamp);
        };
    callbacks.skip_excess_time =
        [this](movement::CMovementData &, const std::uint32_t skipped_time_ms) {
          (void)update_session_->Send(
              net::wotlk::PacketSender::BuildMoveTimeSkipped(
                  owner_.GetGuid(), skipped_time_ms));
        };
    data_.SetUpdateCallbacks(std::move(callbacks));
    InstallEventProcessingGate();
    InstallUpdateScopedCallbacks();
    persistent_callbacks_installed_ = true;
  }

  if (!has_movement_update_tick_) {
    last_movement_update_tick_ = current_tick_ms - 1u;
    movement_timeline_.current_tick = last_movement_update_tick_;
    movement_timeline_.active_mover_deadline =
        current_tick_ms + net::wotlk::kHeartbeatIntervalMs;
    has_movement_update_tick_ = true;
  }

  const auto signed_frame_delta =
      static_cast<std::int32_t>(current_tick_ms - last_movement_update_tick_);
  const std::uint32_t frame_delta_ms =
      signed_frame_delta > 0
          ? static_cast<std::uint32_t>(signed_frame_delta)
          : 0u;

  update_callbacks_live_ = true;
  (void)data_.Update(session.objects(), current_tick_ms,
                     last_movement_update_tick_, &movement_timeline_);
  update_callbacks_live_ = false;
  last_movement_update_tick_ = current_tick_ms;

  if (!active_mover && !spline_movement_pose_owned_) {
    ApplyQueuedPreview(current_tick_ms, frame_delta_ms);
  }

  if (!data_.GetEventQueue().HasEvents()) {
    data_.ClearPendingRuntimeNotification();
  }
  UpdateWaterRipples(current_tick_ms);
}

void UnitMovementRuntime::Cleanup() {
  const auto old_transport_guid = data_.Cleanup();
  if (old_transport_guid != 0u) {
    if (auto *const objects = owner_.object_manager(); objects != nullptr) {
      Movement_NotifyVehicle(*objects, 0, 0, old_transport_guid);
    }
  }
  movement_timeline_ = {};
  collision_solver_source_.reset();
  collision_solver_.reset();
  ForgetMovementParentTransform();
  last_movement_update_tick_ = 0u;
  has_movement_update_tick_ = false;
  next_water_ripple_timestamp_ = 0u;
}

void UnitMovementRuntime::ResetState() noexcept {
  update_flags_ = 0u;
  movement_timeline_ = {};
  collision_solver_source_.reset();
  collision_solver_.reset();
  ForgetMovementParentTransform();
  last_movement_update_tick_ = 0u;
  has_movement_update_tick_ = false;
  previous_liquid_depth_ = 0.0f;
  in_water_ = false;
  next_water_ripple_timestamp_ = 0u;
}

}
