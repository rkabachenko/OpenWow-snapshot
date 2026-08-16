
#include "openwow/game/movement/player_move_event.h"

#include "openwow/game/movement/movement_opcode_utils.h"
#include "openwow/game/movement/retail_fall_kinematics.h"
#include "openwow/game/movement/retail_movement_kinematics.h"
#include "openwow/game/movement_callbacks.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/passenger_movement.h"
#include "openwow/core/client_misc.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/game/object_types.h"
#include "openwow/foundation/math/angle_normalize.h"
#include "openwow/foundation/math/row_major_mat4x4.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace openwow::game::movement {

namespace {

constexpr float kAngleChangeEpsilon = 0.00000095367432f;
constexpr std::uint32_t kQueuedPreviewLinearMotionMask = 0x00C0100Fu;
constexpr std::uint32_t kQueuedPreviewPitchMask = 0x02200000u;
constexpr std::uint32_t kIgnoredQueuedPreviewEventType = 44u;

void TransformPosition(std::array<float, 3>& position, const float* matrix) {
  Passenger_TransformPointByMatrix(
      position.data(), position.data(), matrix);
}

void RotateDirection(std::array<float, 3>& direction, const float* matrix) {
  const float x = direction[0];
  const float y = direction[1];
  const float z = direction[2];
  direction[0] = matrix[0] * x + matrix[4] * y + matrix[8] * z;
  direction[1] = matrix[1] * x + matrix[5] * y + matrix[9] * z;
  direction[2] = matrix[2] * x + matrix[6] * y + matrix[10] * z;
}

constexpr float kFacingNormalizeEpsilon = 0.00000023841858f;

bool ResolveTransformOrIdentity(const openwow::game::ObjectManager& objects,
                                const std::uint64_t guid, float* matrix) {
  if (openwow::game::Movement_GetObjectTransform(objects, guid, matrix) != 0) {
    return true;
  }

  for (int index = 0; index < 16; ++index) {
    matrix[index] = 0.0f;
  }
  matrix[0] = 1.0f;
  matrix[5] = 1.0f;
  matrix[10] = 1.0f;
  matrix[15] = 1.0f;
  return false;
}

bool CanResolvePassengerParent(const openwow::game::ObjectManager& objects,
                               const std::uint64_t guid) {

  return guid == 0 || openwow::game::Movement_C_IsGuidTransport(objects, guid);
}

bool IsPassengerParentObjectPresent(const openwow::game::ObjectManager& objects,
                                    const std::uint64_t guid) {
  if (guid == 0) {
    return true;
  }

  return openwow::game::CGObject_HasFlags(
             objects, guid, openwow::game::kTypeMaskObject) != nullptr;
}

void LogInvalidTransportChange(const std::uint64_t current_guid,
                               const std::uint64_t requested_guid) {
  std::ostringstream message;
  message << "CMovementData_C::ForceSetTransportInt() was called with an "
             "invalid m_transportGUID: (m_transportGUID: "
          << std::uppercase << std::hex << std::setw(16) << std::setfill('0')
          << static_cast<unsigned long long>(current_guid)
          << ") (guid: " << std::setw(16)
          << static_cast<unsigned long long>(requested_guid) << ')';
  diagnostics::Log(diagnostics::LogLevel::kWarn, message.str());
}

bool QueuedEventCarriesFacingPayload(const CPlayerMoveEvent& event) {
  return event.event_type == static_cast<std::uint32_t>(MoveEventType::kSetFacing) ||
         event.needs_ack;
}

bool AnglesDiffer(const float lhs, const float rhs) {
  return std::fabs(lhs - rhs) >= kAngleChangeEpsilon;
}

float NormalizeSignedAngle(const float delta) {
  return openwow::math::NormalizeSignedAngle(delta);
}

float NormalizeWrappedAngle0ToTau(const float angle) {
  return openwow::math::NormalizePositiveAngle(angle);
}

bool TickBefore(const std::uint32_t lhs, const std::uint32_t rhs) {
  return static_cast<std::int32_t>(lhs - rhs) < 0;
}

}

void CPlayerMoveEventQueue::QueueEvent(CPlayerMoveEvent event) {
  auto it = events_.begin();
  while (it != events_.end() && !TickBefore(event.timestamp, it->timestamp)) {
    ++it;
  }
  events_.insert(it, std::move(event));
}

void CPlayerMoveEventQueue::RemoveByType(std::uint32_t event_type) {
  events_.remove_if(
      [event_type](const CPlayerMoveEvent& e) {
        return e.event_type == event_type;
      });
}

bool CPlayerMoveEventQueue::RescheduleFirstByType(
    const std::uint32_t event_type,
    const std::uint32_t timestamp) {
  auto match = std::find_if(
      events_.begin(), events_.end(),
      [event_type](const CPlayerMoveEvent& event) {
        return event.event_type == event_type;
      });
  if (match == events_.end()) {
    return false;
  }

  std::list<CPlayerMoveEvent> detached;
  detached.splice(detached.begin(), events_, match);
  detached.front().timestamp = timestamp;

  auto insert_pos = events_.begin();
  while (insert_pos != events_.end() &&
         !TickBefore(detached.front().timestamp, insert_pos->timestamp)) {
    ++insert_pos;
  }
  events_.splice(insert_pos, detached, detached.begin());
  return true;
}

void CPlayerMoveEventQueue::ClearAll() { events_.clear(); }

CPlayerMoveEvent CPlayerMoveEventQueue::PopFront() {
  auto event = events_.front();
  events_.pop_front();
  return event;
}

const CPlayerMoveEvent& CPlayerMoveEventQueue::PeekFront() const {
  return events_.front();
}

bool CPlayerMoveEventQueue::HasEventInTypeRange(std::uint32_t min_type,
                                                 std::uint32_t max_type) const {
  for (const auto& event : events_) {
    if (event.event_type >= min_type && event.event_type <= max_type) {
      return true;
    }
  }
  return false;
}

void CMovementData::Init() {
  camera_pitch_min_ = 0.33333334f;
  camera_pitch_max_ = 2.0277777f;
  camera_zoom_ = 1.0f;
  has_pending_runtime_notification_ = false;
  vehicle_seat_transfer_runtime_flags_ = 0;
  transport_guid_ = 0;
  transport_seat_ = 0;
  transform_position_ = {0.0f, 0.0f, 0.0f};
  scalar_facing_ = 0.0f;
  runtime_pitch_ = 0.0f;
  runtime_flags_ = 0;
  runtime_flags2_ = 0;
  parent_movement_flags_ = 0u;
  has_parent_movement_ = false;
  remote_gravity_changed_ = false;
  runtime_fall_time_ = 0u;
  runtime_fall_start_z_ = 0.0f;
  server_movement_timestamp_baseline_ = 0;
  server_event_presentation_anchor_ = 0;
  server_timing_bias_history_.fill(0);
  server_timing_bias_head_ = 0;
  server_timing_bias_ms_ = 0;
  packed_orientation_ = 0;
  uses_packed_orientation_ = false;
  prev_position_ = {0.0f, 0.0f, 0.0f};
  prev_orientation_ = 0.0f;
  prev_pitch_ = 0.0f;
  interpolation_progress_ = 0.0f;
  collision_half_width_ = 0.0f;
  collision_height_product_ = 0.0f;
  collision_scale_ratio_ = 1.0f;
  cumulative_collision_z_ = 0.0f;
  ClearQueuedMovementPreview();
  event_queue_.ClearAll();
}

void CMovementData::InitCollisionBounds(const float width, const float height,
                                        const float effective_scale,
                                        const float raw_scale, const bool forced,
                                        const bool is_navigable_as_player) {
  constexpr float kEpsilon = 0.00000023841858f;

  const float scale_ratio =
      (std::fabs(raw_scale) >= kEpsilon || std::isnan(raw_scale))
          ? (effective_scale / raw_scale)
          : 1.0f;

  collision_scale_ratio_ = scale_ratio;
  collision_half_width_ = width * effective_scale * 0.5f;

  if (forced || !is_navigable_as_player) {
    collision_height_product_ = effective_scale * height;
  }

}

float* CMovementData::BuildAABBFromPosition(const float* position,
                                            float* out_aabb) const {

  out_aabb[0] = position[0];
  out_aabb[1] = position[1];
  out_aabb[2] = position[2];
  out_aabb[3] = position[0];
  out_aabb[4] = position[1];
  out_aabb[5] = position[2];

  out_aabb[0] -= collision_half_width_;
  out_aabb[1] -= collision_half_width_;
  out_aabb[3] += collision_half_width_;
  out_aabb[4] += collision_half_width_;

  out_aabb[5] += collision_height_product_;

  return out_aabb;
}

std::uint32_t CMovementData::BuildTerrainIntersectFlags(
    const TerrainIntersectUnitState& unit_state) const {

  std::uint32_t flags = unit_state.is_navigable_as_player
                            ? kTerrainBasePlayer
                            : kTerrainBaseNPC;

  if (unit_state.can_control_character) {
    flags |= kTerrainCanControl;
  }

  const std::uint32_t mflags = runtime_flags_;
  if ((mflags & kFlagWaterWalking) != 0 &&
      (mflags & kFlagSwimming) == 0 &&
      (ground_slope_z_ > kWaterWalkSlopeThreshold ||
       (runtime_flags2_ & kFlags2WaterWalkSlopeOverride) != 0)) {
    flags |= kTerrainWaterWalk;
  }

  if ((mflags & kFlagFlying) != 0) {
    flags |= kTerrainFlying;
    if ((runtime_flags2_ & kFlags2HoverFlight) == 0) {
      flags |= kTerrainNonHoverFly;
    }
  }

  if (unit_state.is_ghost_player) {
    flags |= kTerrainPlayerGhost;
  }

  return flags;
}

bool CMovementData::TestUnitBone(
    const std::uint32_t timestamp_offset,
    const std::uint32_t object_timestamp_offset,
    const std::uint32_t time_delta,
    const float sweep_distance,
    const float* const sweep_direction,
    const float* transport_transform,
    const PassengerCollisionCallbacks& callbacks) {

  using openwow::math::row_major_mat4x4::TransformPointByRowMajorAffine4x4Unbuffered;
  using openwow::math::row_major_mat4x4::BuildInverseRigidTransform4x4;
  using openwow::math::row_major_mat4x4::TransformVec3ByUpper3x3;

  float position[3];
  TransformPointByRowMajorAffine4x4Unbuffered(
      position, transform_position_.data(), transport_transform);

  if (sweep_direction == nullptr || !std::isfinite(sweep_distance) ||
      sweep_distance <= 0.0f) {
    return false;
  }

  float aabb[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
  BuildAABBFromPosition(position, aabb);
  const float delta_x = sweep_distance * sweep_direction[0];
  const float delta_y = sweep_distance * sweep_direction[1];
  const float delta_z = sweep_distance * sweep_direction[2];
  aabb[0] = std::min(aabb[0], aabb[0] + delta_x);
  aabb[1] = std::min(aabb[1], aabb[1] + delta_y);
  aabb[2] = std::min(aabb[2], aabb[2] + delta_z);
  aabb[3] = std::max(aabb[3], aabb[3] + delta_x);
  aabb[4] = std::max(aabb[4], aabb[4] + delta_y);
  aabb[5] = std::max(aabb[5], aabb[5] + delta_z);

  if (!callbacks.get_unit_state) {
    return false;
  }
  const auto unit_state = callbacks.get_unit_state(*this);

  std::uint32_t intersect_flags = unit_state.is_navigable_as_player
                                      ? kBoneTerrainBasePlayer
                                      : kBoneTerrainBaseNPC;

  if (unit_state.is_ghost_player) {
    intersect_flags |= kTerrainPlayerGhost;
  }

  if (!callbacks.trace_world) {
    return false;
  }
  const std::optional<PassengerCollisionHit> hit =
      callbacks.trace_world(aabb, position, sweep_direction, sweep_distance,
                            intersect_flags);
  if (!hit.has_value()) {
    return false;
  }

  if (unit_state.has_character_control.has_value() &&
      !unit_state.has_character_control.value()) {
    return false;
  }

  float contact_time_ms = 0.0f;
  if (std::isfinite(sweep_distance) && sweep_distance > 0.0f &&
      std::isfinite(hit->distance)) {
    contact_time_ms = (hit->distance / sweep_distance) *
                      static_cast<float>(time_delta) + 0.5f;
  }
  if (!std::isfinite(contact_time_ms) || contact_time_ms <= 0.0f) {
    contact_time_ms = 0.0f;
  }
  const std::uint32_t contact_time_offset =
      contact_time_ms >= 4294967296.0f
          ? std::numeric_limits<std::uint32_t>::max()
          : static_cast<std::uint32_t>(contact_time_ms);
  const std::uint32_t adjusted_timestamp =
      timestamp_offset + object_timestamp_offset - time_delta +
      contact_time_offset;

  const std::uint32_t saved_current_movement_timestamp =
      openwow::core::CMovementRuntime_GetMovementTimestamp();
  const std::uint32_t mapped_timestamp =
      callbacks.map_transport_timestamp
          ? callbacks.map_transport_timestamp(adjusted_timestamp)
          : adjusted_timestamp;
  openwow::core::CMovementRuntime_SetMovementTimestamp(mapped_timestamp);

  float world_to_transport[16];
  BuildInverseRigidTransform4x4(world_to_transport, transport_transform);

  float local_direction[3];
  TransformVec3ByUpper3x3(local_direction, sweep_direction,
                          world_to_transport);

  const float contact_distance = hit->distance;
  transform_position_[0] += local_direction[0] * contact_distance;
  transform_position_[1] += local_direction[1] * contact_distance;
  transform_position_[2] += local_direction[2] * contact_distance;

  runtime_fall_start_z_ += local_direction[2] * contact_distance;

  if (callbacks.force_set_transport_notify) {
    callbacks.force_set_transport_notify(*this);
  }

  QueueCollisionHeartbeat(adjusted_timestamp);

  openwow::core::CMovementRuntime_RestoreMovementTimestampState(
      saved_current_movement_timestamp, mapped_timestamp);

  return true;
}

void CMovementData::InterpolateOrientation(
    const openwow::game::ObjectManager& objects,
    const float desired_facing) {
  float transport_orientation = 0.0f;
  if (transport_guid_ != 0) {
    transport_orientation =
        openwow::game::Movement_GetObjectOrientation(objects, transport_guid_);
  }

  const float relative_facing = openwow::game::Movement_NormalizeFacing0ToTau(
      desired_facing - transport_orientation);

  if (std::fabs(relative_facing - scalar_facing_) >= kAngleChangeEpsilon) {
    scalar_facing_ = relative_facing;
    if ((runtime_flags_ & openwow::game::kMoveFlagFalling) == 0) {
      SnapshotStateForDirectionRecompute();
    }
  }

  runtime_flags_ &= ~(static_cast<std::uint32_t>(openwow::game::kMoveFlagTurnLeft) |
                       static_cast<std::uint32_t>(openwow::game::kMoveFlagTurnRight));
}

void CMovementData::SnapshotStateForDirectionRecompute() {
  prev_position_ = transform_position_;
  prev_orientation_ = scalar_facing_;
  prev_pitch_ = runtime_pitch_;
  interpolation_progress_ = 0.0f;
  ComputeDirectionVectors(false);
}

void CMovementData::SetFacingWithVisualUpdate(
                                              const openwow::game::ObjectManager& objects,
                                              const float facing,
                                              const bool update_visuals) {

  InterpolateOrientation(objects, facing);

  if ((runtime_flags_ & kFacingVisualUpdateSuppressionMask) != 0) {
    return;
  }

  if (update_visuals && set_facing_visual_update_callback_) {
    set_facing_visual_update_callback_(*this, facing);
  }
}

bool CMovementData::IsMovementInitSuppressed() const {
  const bool non_exempt_parent =
      has_parent_movement_ &&
      (parent_movement_flags_ & kParentAllowStopFlag) == 0u;
  const bool falling_parent =
      non_exempt_parent &&
      (parent_movement_flags_ & kParentFallingSplineFlag) != 0u;

  if (non_exempt_parent && !falling_parent &&
      (parent_movement_flags_ & kParentFlyingSplineFlag) != 0u) {
    return true;
  }

  if (!falling_parent &&
      ((runtime_flags2_ & kFlags2SuppressBit) != 0u ||
       (runtime_flags_ & openwow::game::kMoveFlagDisableGravity) != 0u)) {
    return true;
  }

  if ((runtime_flags_ &
       (kInitSuppressActiveMotion | kInitBlockedByRootOrTransport)) != 0u) {
    return true;
  }

  return has_parent_movement_ &&
         (parent_movement_flags_ & kInitBlockedByRootOrTransport) != 0u;
}

bool CMovementData::TryResetMovementState() {
  if (IsMovementInitSuppressed()) {
    return false;
  }

  SnapshotStateForDirectionRecompute();
  runtime_flags_ = (runtime_flags_ & ~kInitResetClearMask) |
                   openwow::game::kMoveFlagFalling;

  if ((runtime_flags2_ & kFlags2AllowPitchBit) == 0) {
    runtime_flags_ &= ~static_cast<std::uint32_t>(
        openwow::game::kMoveFlagPitchUp | openwow::game::kMoveFlagPitchDown);
  }

  interpolation_progress_ = 0.0f;
  runtime_fall_time_ = 0u;
  runtime_fall_start_z_ = transform_position_[2];
  runtime_jump_z_speed_ = 0.0f;
  return true;
}

bool CMovementData::TryInitRemoteMovement() {
  if (!TryResetMovementState()) {
    return false;
  }

  if (!has_pending_runtime_notification_) {
    has_pending_runtime_notification_ = true;
    if (runtime_notification_callback_) {
      runtime_notification_callback_(*this);
    }
  }

  return true;
}

bool CMovementData::CanSerializeActiveMoverMovementState(
    const std::uint16_t opcode, const bool is_active_mover) const {
  if (!is_active_mover) {
    return false;
  }
  if (!has_parent_movement_ ||
      (parent_movement_flags_ & kParentAllowStopFlag) != 0u) {
    return true;
  }
  return MovementOpcode_IsActiveMoverGated(opcode);
}

void CMovementData::ClearQueuedEvents() {
  event_queue_.ClearAll();
  has_pending_runtime_notification_ = false;
  ClearQueuedMovementPreview();
}

std::uint64_t CMovementData::Cleanup() {

  ClearParentMovement();

  const auto old_transport_guid = transport_guid_;
  transport_guid_ = 0;
  transport_seat_ = 0;
  runtime_fall_time_ = 0u;
  runtime_fall_start_z_ = 0.0f;

  runtime_flags_ &= kCleanupFlagRetainMask;
  remote_gravity_changed_ = false;

  ClearQueuedEvents();

  return old_transport_guid;
}

CPlayerMoveEvent CMovementData::MakeEvent(std::uint32_t timestamp,
                                           std::uint32_t event_type) {
  CPlayerMoveEvent event;
  event.timestamp = timestamp;
  event.event_type = event_type;
  event.cos_angle = 0.0f;
  event.sin_angle = 0.0f;
  event.jump_z_speed = 0.0f;
  event.auxiliary_f32 = 0.0f;
  event.auxiliary_u32 = 0;
  event.needs_ack = true;
  return event;
}

CPlayerMoveEvent CMovementData::MakeAngleEvent(std::uint32_t timestamp,
                                               std::uint32_t event_type,
                                               const float primary_angle,
                                               const float secondary_angle) {
  auto event = MakeEvent(timestamp, event_type);
  event.cos_angle = primary_angle;
  event.sin_angle = secondary_angle;
  return event;
}

void CMovementData::CaptureRuntimeSnapshot(CPlayerMoveEvent& event) const {
  event.runtime_position = transform_position_;
  event.runtime_orientation = scalar_facing_;
  event.runtime_pitch = runtime_pitch_;
  event.runtime_flags = runtime_flags_;
  event.runtime_flags2 = runtime_flags2_;
  event.runtime_transport_guid = transport_guid_;
  event.runtime_fall_time = runtime_fall_time_;
  event.runtime_fall_start_z = runtime_fall_start_z_;
  event.runtime_transport_seat = transport_seat_;
  event.has_runtime_snapshot = true;
}

void CMovementData::RestoreRuntimeSnapshot(const CPlayerMoveEvent &event) {
  runtime_flags_ =
      (event.runtime_flags & 0x77FFFDFFu) |
      (runtime_flags_ & 0x88000200u);
  transform_position_ = event.runtime_position;
  scalar_facing_ = event.runtime_orientation;
  runtime_pitch_ = event.runtime_pitch;
  if ((event.runtime_flags & openwow::game::kMoveFlagFalling) != 0u) {
    runtime_fall_time_ = event.runtime_fall_time;
    runtime_fall_start_z_ = event.runtime_fall_start_z;
  }
  interpolation_progress_ = 0.0f;
}

void CMovementData::RebaseQueuedTransportState(const float facing_delta) {
  event_queue_.ForEachMutable(
      [&](CPlayerMoveEvent& event) {

        if (QueuedEventCarriesFacingPayload(event)) {
          event.cos_angle = openwow::game::Movement_NormalizeFacing0ToTau(
              event.cos_angle + facing_delta);
        }
      });
}

void CMovementData::ClearQueuedMovementPreview() {
  queued_preview_ = {};
  queued_preview_total_lead_time_ms_ = 0;
  queued_preview_source_timestamp_ = 0;
  queued_preview_source_event_type_ = 0;
}

void CMovementData::QueueAndNotify(CPlayerMoveEvent event) {
  event_queue_.QueueEvent(std::move(event));
  if (IsOnTransport()) {
    return;
  }

  has_pending_runtime_notification_ = true;
  if (runtime_notification_callback_) {
    runtime_notification_callback_(*this);
  }
}

void CMovementData::QueueDeferredMoveEvent(const std::uint32_t timestamp,
                                           const std::uint32_t event_type,
                                           const bool needs_ack,
                                           const std::uint32_t auxiliary_u32,
                                           const float auxiliary_f32,
                                           const bool capture_runtime_snapshot,
                                           const std::uint32_t current_time_ms) {
  auto event = MakeEvent(timestamp, event_type);
  event.needs_ack = needs_ack;
  event.auxiliary_u32 = auxiliary_u32;
  event.auxiliary_f32 = auxiliary_f32;
  if (capture_runtime_snapshot) {
    CaptureRuntimeSnapshot(event);
  }

  QueueAndNotify(std::move(event));
  if (capture_runtime_snapshot && !IsOnTransport()) {
    RefreshQueuedMovementPreview(current_time_ms);
  }
}

void CMovementData::QueueVehicleSeatSwitch(const std::uint32_t timestamp,
                                           const std::uint32_t target_guid_lo,
                                           const std::uint32_t target_guid_hi,
                                           const std::uint8_t seat) {
  auto event = MakeEvent(
      timestamp, static_cast<std::uint32_t>(MoveEventType::kVehicleSeatSwitch));
  event.auxiliary_u32 = target_guid_lo;
  event.auxiliary_u32_secondary = target_guid_hi;
  event.auxiliary_u8 = seat;
  QueueAndNotify(std::move(event));
}

void CMovementData::QueueForwardMove(std::uint32_t timestamp, bool forward) {
  auto event = MakeEvent(
      timestamp,
      static_cast<std::uint32_t>(forward ? MoveEventType::kStartForward
                                         : MoveEventType::kStartBackward));
  QueueAndNotify(std::move(event));
}

void CMovementData::QueueStrafeMove(std::uint32_t timestamp, bool left) {
  auto event = MakeEvent(
      timestamp,
      static_cast<std::uint32_t>(left ? MoveEventType::kStartStrafeLeft
                                      : MoveEventType::kStartStrafeRight));
  QueueAndNotify(std::move(event));
}

void CMovementData::QueueJump(std::uint32_t timestamp) {
  auto event = MakeEvent(timestamp, static_cast<std::uint32_t>(MoveEventType::kJump));
  QueueAndNotify(std::move(event));
}

void CMovementData::QueueFallLand(std::uint32_t timestamp) {
  auto event = MakeEvent(timestamp, static_cast<std::uint32_t>(MoveEventType::kFallLand));
  QueueAndNotify(std::move(event));
}

void CMovementData::QueueHeartbeat(std::uint32_t timestamp) {
  auto event = MakeEvent(timestamp, static_cast<std::uint32_t>(MoveEventType::kHeartbeat));
  QueueAndNotify(std::move(event));
}

void CMovementData::QueueTimeSync(const std::uint32_t timestamp,
                                 const std::uint32_t counter) {
  auto event = MakeEvent(
      timestamp, static_cast<std::uint32_t>(MoveEventType::kTimeSync));
  event.auxiliary_u32 = counter;
  QueueAndNotify(std::move(event));
}

void CMovementData::QueueCollisionHeartbeat(std::uint32_t timestamp) {
  const bool is_active = update_callbacks_.is_active_player
                             ? update_callbacks_.is_active_player(*this)
                             : false;
  auto event =
      MakeEvent(timestamp, static_cast<std::uint32_t>(MoveEventType::kHeartbeat));
  event.needs_ack = is_active;
  QueueAndNotify(std::move(event));
}

void CMovementData::QueueStopForward(std::uint32_t timestamp) {
  auto event = MakeEvent(timestamp,
                         static_cast<std::uint32_t>(MoveEventType::kStopForwardBackward));
  QueueAndNotify(std::move(event));
}

void CMovementData::QueueStopStrafe(std::uint32_t timestamp) {
  auto event = MakeEvent(timestamp,
                         static_cast<std::uint32_t>(MoveEventType::kStopStrafe));
  QueueAndNotify(std::move(event));
}

void CMovementData::QueueTurnMovement(std::uint32_t timestamp,
                                      const bool turning_right) {
  auto event = MakeEvent(
      timestamp,
      static_cast<std::uint32_t>(turning_right ? MoveEventType::kStartTurnRight
                                               : MoveEventType::kStartTurnLeft));
  QueueAndNotify(std::move(event));
  event_queue_.RemoveByType(
      static_cast<std::uint32_t>(MoveEventType::kScheduledTurnStop));
  RefreshQueuedMovementPreview(timestamp);
}

void CMovementData::QueuePitchMovement(const std::uint32_t timestamp,
                                       const bool pitching_down) {
  auto event = MakeEvent(
      timestamp,
      static_cast<std::uint32_t>(pitching_down
                                     ? MoveEventType::kStartPitchDown
                                     : MoveEventType::kStartPitchUp));
  QueueAndNotify(std::move(event));
  event_queue_.RemoveByType(
      static_cast<std::uint32_t>(MoveEventType::kScheduledPitchStop));
  RefreshQueuedMovementPreview(timestamp);
}

void CMovementData::QueueStopTurn(std::uint32_t timestamp) {
  auto event = MakeEvent(timestamp, 13);
  QueueAndNotify(std::move(event));
  event_queue_.RemoveByType(
      static_cast<std::uint32_t>(MoveEventType::kScheduledTurnStop));
  RefreshQueuedMovementPreview(timestamp);
}

void CMovementData::QueueToggleRun(std::uint32_t timestamp, bool running) {
  auto event = MakeEvent(
      timestamp,
      static_cast<std::uint32_t>(running ? MoveEventType::kStartRun
                                         : MoveEventType::kStartWalk));
  QueueAndNotify(std::move(event));
  RefreshQueuedMovementPreview(timestamp);
}

void CMovementData::QueueStopPitch(std::uint32_t timestamp) {
  auto event = MakeEvent(timestamp, 16);
  QueueAndNotify(std::move(event));
  event_queue_.RemoveByType(
      static_cast<std::uint32_t>(MoveEventType::kScheduledPitchStop));
  RefreshQueuedMovementPreview(timestamp);
}

void CMovementData::QueueSetFacing(std::uint32_t timestamp, const float facing) {
  auto event = MakeAngleEvent(
      timestamp,
      static_cast<std::uint32_t>(MoveEventType::kSetFacing),
      facing,
      0.0f);
  QueueAndNotify(std::move(event));
}

void CMovementData::QueueSetPitch(std::uint32_t timestamp, const float pitch) {
  auto event = MakeAngleEvent(
      timestamp,
      static_cast<std::uint32_t>(MoveEventType::kSetPitch),
      0.0f,
      pitch);
  QueueAndNotify(std::move(event));
}

void CMovementData::QueueSetVehiclePitch(std::uint32_t timestamp,
                                         const float pitch) {
  auto event = MakeAngleEvent(
      timestamp,
      static_cast<std::uint32_t>(MoveEventType::kSetVehiclePitch),
      0.0f,
      pitch);
  QueueAndNotify(std::move(event));
}

void CMovementData::QueueBoundedTurnFacing(std::uint32_t timestamp,
                                           const float facing) {
  auto event = MakeAngleEvent(
      timestamp,
      static_cast<std::uint32_t>(MoveEventType::kBoundedTurnFacing),
      facing,
      0.0f);
  QueueAndNotify(std::move(event));
}

bool CMovementData::ScheduleTurnToFacing(const std::uint32_t timestamp,
                                         const float angle_delta) {
  const float turn_rate = speed_table_[openwow::game::kSpeedTurnRate];
  const auto duration_ms =
      static_cast<std::uint32_t>(std::fabs(angle_delta) / turn_rate * 1000.0f);
  const std::uint32_t end_time = duration_ms + timestamp;

  if (!event_queue_.RescheduleFirstByType(
          static_cast<std::uint32_t>(MoveEventType::kScheduledTurnStop),
          end_time)) {
    auto event = MakeEvent(
        end_time,
        static_cast<std::uint32_t>(MoveEventType::kScheduledTurnStop));
    QueueAndNotify(std::move(event));
  }

  return angle_delta > 0.0f;
}

bool CMovementData::SchedulePitchToTarget(const std::uint32_t timestamp,
                                          const float pitch_delta) {
  const float pitch_rate = speed_table_[openwow::game::kSpeedPitchRate];
  const auto duration_ms =
      static_cast<std::uint32_t>(std::fabs(pitch_delta) / pitch_rate * 1000.0f);
  const std::uint32_t end_time = duration_ms + timestamp;

  if (!event_queue_.RescheduleFirstByType(
          static_cast<std::uint32_t>(MoveEventType::kScheduledPitchStop),
          end_time)) {
    auto event = MakeEvent(
        end_time,
        static_cast<std::uint32_t>(MoveEventType::kScheduledPitchStop));
    QueueAndNotify(std::move(event));
  }

  return pitch_delta > 0.0f;
}

void CMovementData::QueueSwimToFly(std::uint32_t timestamp,
                                   bool enable_fly_mode) {
  auto event = MakeEvent(timestamp, enable_fly_mode ? 45 : 46);
  QueueAndNotify(std::move(event));
  RefreshQueuedMovementPreview(timestamp);
}

void CMovementData::QueueVerticalMove(std::uint32_t timestamp, bool ascending) {
  auto event = MakeEvent(timestamp,
                         ascending ? static_cast<std::uint32_t>(MoveEventType::kStartAscend)
                                   : static_cast<std::uint32_t>(MoveEventType::kStartDescend));
  QueueAndNotify(std::move(event));
  RefreshQueuedMovementPreview(timestamp);
}

void CMovementData::QueueStopVertical(std::uint32_t timestamp) {
  auto event = MakeEvent(timestamp, 8);
  QueueAndNotify(std::move(event));
  RefreshQueuedMovementPreview(timestamp);
}

void CMovementData::QueueStartSwim(std::uint32_t timestamp) {
  auto event = MakeEvent(
      timestamp, static_cast<std::uint32_t>(MoveEventType::kStartSwim));
  QueueAndNotify(std::move(event));
  RefreshQueuedMovementPreview(timestamp);
}

void CMovementData::QueueStopSwim(std::uint32_t timestamp) {
  auto event = MakeEvent(
      timestamp, static_cast<std::uint32_t>(MoveEventType::kStopSwim));
  QueueAndNotify(std::move(event));
  RefreshQueuedMovementPreview(timestamp);
}

void CMovementData::StopSwimmingForTransportAttach() {

  ApplyStopSwimState();
}

void CMovementData::QueueGravityToggle(const std::uint32_t timestamp,
                                       const std::uint32_t counter,
                                       const bool disable_gravity) {
  auto event = MakeEvent(
      timestamp,
      disable_gravity
          ? static_cast<std::uint32_t>(MoveEventType::kGravityDisable)
          : static_cast<std::uint32_t>(MoveEventType::kGravityEnable));
  event.auxiliary_u32 = counter;
  QueueAndNotify(std::move(event));
}

void CMovementData::QueueKnockBack(const std::uint32_t timestamp,
                                   const std::uint32_t counter,
                                   const float direction_x,
                                   const float direction_y,
                                   const float horizontal_speed,
                                   const float vertical_speed) {
  auto event = MakeEvent(
      timestamp, static_cast<std::uint32_t>(MoveEventType::kKnockBack));
  event.cos_angle = direction_x;
  event.sin_angle = direction_y;
  event.auxiliary_f32 = horizontal_speed;
  event.jump_z_speed = vertical_speed;
  event.auxiliary_u32 = counter;
  QueueAndNotify(std::move(event));
}

void CMovementData::QueueSpeedChangeEvent(const std::uint32_t timestamp,
                                          const MoveEventType event_type,
                                          const float speed) {
  auto event = MakeEvent(timestamp, static_cast<std::uint32_t>(event_type));
  event.auxiliary_f32 = speed;
  event.needs_ack = false;
  event.auxiliary_u32 = 0;
  QueueAndNotify(std::move(event));
}

void CMovementData::QueueForceSpeedChangeEvent(const std::uint32_t timestamp,
                                                const MoveEventType event_type,
                                                const std::uint32_t counter,
                                                const float speed) {
  auto event = MakeEvent(timestamp, static_cast<std::uint32_t>(event_type));
  event.auxiliary_f32 = speed;
  event.auxiliary_u32 = counter;

  QueueAndNotify(std::move(event));
}

void CMovementData::RefreshQueuedMovementPreview(const std::uint32_t current_time_ms) {
  queued_preview_ = {};
  runtime_flags2_ &= ~static_cast<std::uint16_t>(
      openwow::game::kMoveFlag2InterpolatedMovement |
      openwow::game::kMoveFlag2InterpolatedTurning |
      openwow::game::kMoveFlag2InterpolatedPitching);
  if (!event_queue_.HasEvents()) {
    queued_preview_total_lead_time_ms_ = 0;
    queued_preview_source_timestamp_ = 0;
    queued_preview_source_event_type_ = 0;
    return;
  }

  const CPlayerMoveEvent& queued_event = event_queue_.PeekFront();
  if (!queued_event.has_runtime_snapshot ||
      queued_event.event_type == kIgnoredQueuedPreviewEventType ||
      queued_event.runtime_transport_guid != transport_guid_) {
    queued_preview_total_lead_time_ms_ = 0;
    queued_preview_source_timestamp_ = 0;
    queued_preview_source_event_type_ = 0;
    return;
  }

  const std::int32_t lead_time_ms =
      static_cast<std::int32_t>(queued_event.timestamp - current_time_ms);
  if (lead_time_ms == 0) {
    queued_preview_total_lead_time_ms_ = 0;
    queued_preview_source_timestamp_ = 0;
    queued_preview_source_event_type_ = 0;
    return;
  }

  queued_preview_total_lead_time_ms_ = static_cast<std::uint32_t>(lead_time_ms);
  queued_preview_source_timestamp_ = queued_event.timestamp;
  queued_preview_source_event_type_ = queued_event.event_type;

  queued_preview_.lead_time_ms = lead_time_ms;
  queued_preview_.delta_position[0] = queued_event.runtime_position[0] - transform_position_[0];
  queued_preview_.delta_position[1] = queued_event.runtime_position[1] - transform_position_[1];
  queued_preview_.delta_position[2] = queued_event.runtime_position[2] - transform_position_[2];
  queued_preview_.delta_orientation =
      NormalizeSignedAngle(queued_event.runtime_orientation - scalar_facing_);
  queued_preview_.delta_pitch =
      NormalizeSignedAngle(queued_event.runtime_pitch - runtime_pitch_);
  if ((runtime_flags_ & kQueuedPreviewLinearMotionMask) != 0u) {
    runtime_flags2_ |= openwow::game::kMoveFlag2InterpolatedMovement;
  }
  if (AnglesDiffer(scalar_facing_, queued_event.runtime_orientation)) {
    runtime_flags2_ |= openwow::game::kMoveFlag2InterpolatedTurning;
  }
  if ((runtime_flags_ & kQueuedPreviewPitchMask) != 0u &&
      AnglesDiffer(runtime_pitch_, queued_event.runtime_pitch)) {
    runtime_flags2_ |= openwow::game::kMoveFlag2InterpolatedPitching;
  }

  queued_preview_.has_linear_motion_delta =
      (runtime_flags2_ & openwow::game::kMoveFlag2InterpolatedMovement) != 0u;
  queued_preview_.has_orientation_delta =
      (runtime_flags2_ & openwow::game::kMoveFlag2InterpolatedTurning) != 0u;
  queued_preview_.has_pitch_delta =
      (runtime_flags2_ & openwow::game::kMoveFlag2InterpolatedPitching) != 0u;
}

bool CMovementData::ApplyQueuedMovementPreview(
    const std::uint32_t current_time_ms,
    const std::uint32_t frame_delta_ms, float &x, float &y, float &z,
    float &orientation, float &pitch) {
  constexpr std::uint16_t kPreviewFlags =
      static_cast<std::uint16_t>(
          openwow::game::kMoveFlag2InterpolatedMovement |
          openwow::game::kMoveFlag2InterpolatedTurning |
          openwow::game::kMoveFlag2InterpolatedPitching);
  if (queued_preview_total_lead_time_ms_ == 0 ||
      !event_queue_.HasEvents()) {
    return false;
  }

  const auto &target = event_queue_.PeekFront();
  if (target.timestamp != queued_preview_source_timestamp_ ||
      target.event_type != queued_preview_source_event_type_) {
    runtime_flags2_ &= ~kPreviewFlags;
    return false;
  }

  const std::int32_t remaining_time_ms =
      static_cast<std::int32_t>(target.timestamp - current_time_ms);
  if (remaining_time_ms < 0) {
    runtime_flags2_ &= ~kPreviewFlags;
    return false;
  }

  queued_preview_.lead_time_ms = remaining_time_ms;
  const float remaining_fraction =
      static_cast<float>(remaining_time_ms) /
      static_cast<float>(queued_preview_total_lead_time_ms_);
  const float blend_fraction = 1.0f - remaining_fraction;
  bool position_applied = false;

  if ((runtime_flags2_ & openwow::game::kMoveFlag2InterpolatedMovement) != 0u) {
    const float next_x =
        (target.runtime_position[0] -
         remaining_fraction * queued_preview_.delta_position[0]) *
            blend_fraction +
        remaining_fraction * x;
    const float next_y =
        (target.runtime_position[1] -
         remaining_fraction * queued_preview_.delta_position[1]) *
            blend_fraction +
        remaining_fraction * y;
    const float next_z =
        (target.runtime_position[2] -
         remaining_fraction * queued_preview_.delta_position[2]) *
            blend_fraction +
        remaining_fraction * z;
    const float delta_x = next_x - transform_position_[0];
    const float delta_y = next_y - transform_position_[1];
    const float max_distance =
        static_cast<float>(frame_delta_ms) * 0.001f * 60.0f;
    if (delta_x * delta_x + delta_y * delta_y + kAngleChangeEpsilon <=
        max_distance * max_distance) {
      x = next_x;
      y = next_y;
      z = next_z;
      position_applied = true;
    } else {
      runtime_flags2_ &= ~static_cast<std::uint16_t>(
          openwow::game::kMoveFlag2InterpolatedMovement);
    }
  }

  if ((runtime_flags2_ & openwow::game::kMoveFlag2InterpolatedTurning) != 0u) {
    const float target_orientation = NormalizeWrappedAngle0ToTau(
        target.runtime_orientation -
        remaining_fraction * queued_preview_.delta_orientation);
    orientation = NormalizeWrappedAngle0ToTau(
        orientation +
        NormalizeSignedAngle(target_orientation - orientation) *
            blend_fraction);
  }

  if ((runtime_flags2_ & openwow::game::kMoveFlag2InterpolatedPitching) != 0u) {
    const float target_pitch = NormalizeWrappedAngle0ToTau(
        target.runtime_pitch -
        remaining_fraction * queued_preview_.delta_pitch);
    pitch = NormalizeWrappedAngle0ToTau(
        pitch + NormalizeSignedAngle(target_pitch - pitch) * blend_fraction);
  }

  return position_applied &&
         (runtime_flags2_ &
          openwow::game::kMoveFlag2InterpolatedMovement) != 0u;
}

void CMovementData::SetVehicleSeatTransferPacketBit(const bool enabled) {
  if (enabled) {
    vehicle_seat_transfer_runtime_flags_ |= kVehicleSeatTransferPacketBitMask;
    return;
  }

  vehicle_seat_transfer_runtime_flags_ &= ~kVehicleSeatTransferPacketBitMask;
}

void CMovementData::SetStandAnimRefreshFlag(const bool enabled) {
  if (enabled) {
    vehicle_seat_transfer_runtime_flags_ |= kStandAnimRefreshBitMask;
    return;
  }

  vehicle_seat_transfer_runtime_flags_ &= ~kStandAnimRefreshBitMask;
}

void CMovementData::SetTransformPosition(const float x,
                                         const float y,
                                         const float z) {
  transform_position_ = {x, y, z};
  ClearQueuedMovementPreview();
}

void CMovementData::SetPresentedTransform(const float x, const float y,
                                          const float z,
                                          const float facing) noexcept {
  transform_position_ = {x, y, z};
  scalar_facing_ = facing;
  uses_packed_orientation_ = false;
}

float* CMovementData::GetPassengerWorldPosition(
    const openwow::game::ObjectManager& objects, float* out_world_pos) const {
  Passenger_TransformLocalToWorldPosition(
      objects, transport_guid_, out_world_pos, transform_position_.data());
  return out_world_pos;
}

std::array<float, 4> CMovementData::GetPassengerWorldOrientation(
    const openwow::game::ObjectManager& objects) const {
  std::array<float, 4> orientation{};
  if (uses_packed_orientation_) {
    Passenger_GetPackedWorldOrientation(
        objects, transport_guid_, packed_orientation_, orientation.data());
    return orientation;
  }

  const float facing = Movement_TransformLocalFacingToWorld(
      objects, transport_guid_, scalar_facing_);
  const float half_facing = facing * 0.5f;
  orientation[2] = std::sin(half_facing);
  orientation[3] = std::cos(half_facing);
  return orientation;
}

float CMovementData::GetPassengerWorldFacing(
    const openwow::game::ObjectManager& objects) const {
  if (uses_packed_orientation_) {
    return Passenger_GetPackedWorldFacing(
        objects, transport_guid_, packed_orientation_);
  }
  return Movement_TransformLocalFacingToWorld(
      objects, transport_guid_, scalar_facing_);
}

void CMovementData::SetScalarFacing(const float facing) {
  scalar_facing_ = facing;
  uses_packed_orientation_ = false;
  ClearQueuedMovementPreview();
}

void CMovementData::SetPackedOrientation(
    const std::int64_t packed_orientation) {
  packed_orientation_ = packed_orientation;
  uses_packed_orientation_ = true;
  ClearQueuedMovementPreview();
}

void CMovementData::RebaseTransportInterpolationState(
    const float* transform_matrix, const float facing_delta,
    const bool clear_spline) {
  TransformPosition(prev_position_, transform_matrix);

  prev_orientation_ = openwow::game::Movement_NormalizeFacing0ToTau(
      prev_orientation_ + facing_delta);

  RotateDirection(dir_forward_, transform_matrix);

  facing_cos_ = dir_forward_[0];
  facing_sin_ = dir_forward_[1];
  const float mag_sq = facing_cos_ * facing_cos_ + facing_sin_ * facing_sin_;
  if (mag_sq > kFacingNormalizeEpsilon) {
    const float inv_mag = 1.0f / std::sqrt(mag_sq);
    facing_cos_ *= inv_mag;
    facing_sin_ *= inv_mag;
  }

  if (clear_spline) {
    runtime_flags_ &= ~static_cast<std::uint32_t>(
        openwow::game::kMoveFlagSplineEnabled);
  }
}

bool CMovementData::ForceSetTransport(
    const openwow::game::ObjectManager& objects,
    const std::uint64_t transport_guid, const std::uint8_t seat,
    const bool force,
    std::optional<float>* const old_parent_body_facing_delta_out,
    std::optional<float>* const new_parent_body_facing_delta_out,
    std::function<void()> before_parent_commit,
    std::function<void(const std::uint64_t old_transport_guid,
                       const std::uint64_t new_transport_guid,
                       const std::uint8_t transport_seat)>
        before_parent_rebase,
    std::function<void(const bool leaving_parent,
                       const std::uint64_t transport_guid,
                       const float body_facing_delta)>
        after_parent_rebase) {
  if (old_parent_body_facing_delta_out != nullptr) {
    *old_parent_body_facing_delta_out = std::nullopt;
  }
  if (new_parent_body_facing_delta_out != nullptr) {
    *new_parent_body_facing_delta_out = std::nullopt;
  }

  if (transport_guid == transport_guid_ && seat == transport_seat_) {
    return false;
  }

  if (!force && has_parent_movement_) {
    return false;
  }

  if (!CanResolvePassengerParent(objects, transport_guid)) {
    return false;
  }

  if (transport_guid_ != 0 &&
      !IsPassengerParentObjectPresent(objects, transport_guid_)) {
    LogInvalidTransportChange(transport_guid_, transport_guid);
    transport_guid_ = 0;
    ClearQueuedMovementPreview();
    return false;
  }

  if (openwow::game::Movement_IsVehicleOrPlayerGuid(transport_guid_) ||
      openwow::game::Movement_IsVehicleOrPlayerGuid(transport_guid)) {
    if (before_parent_rebase) {
      before_parent_rebase(transport_guid_, transport_guid, seat);
    } else if (transport_seat_change_callback_) {
      transport_seat_change_callback_(transport_guid_, transport_guid, seat);
    }
  }

  if (transport_guid == transport_guid_) {

    transport_seat_ = seat;
    ClearQueuedMovementPreview();
    return true;
  }

  PassengerPose pose{
      .position = transform_position_,
      .scalar_facing = scalar_facing_,
      .packed_orientation = packed_orientation_,
      .uses_packed_orientation = uses_packed_orientation_,
  };

  const auto rebaseStoredZ = [this](const std::array<float, 3>& source_position,
                                    const float* transform_matrix) {
    std::array<float, 3> fall_start_position{
        source_position[0], source_position[1], runtime_fall_start_z_};
    TransformPosition(fall_start_position, transform_matrix);
    runtime_fall_start_z_ = fall_start_position[2];

    std::array<float, 3> collision_position{
        source_position[0], source_position[1], cumulative_collision_z_};
    TransformPosition(collision_position, transform_matrix);
    cumulative_collision_z_ = collision_position[2];
  };

  transport_seat_ = seat;

  const auto notify_parent_rebase =
      [&](const bool leaving_parent, const std::uint64_t parent_guid,
          const float body_facing_delta) {
        if (after_parent_rebase) {
          after_parent_rebase(leaving_parent, parent_guid,
                              body_facing_delta);
        } else if (transport_parent_rebase_callback_) {
          transport_parent_rebase_callback_(leaving_parent, parent_guid,
                                            body_facing_delta);
        }
      };

  if (transport_guid_ != 0) {
    float current_transform[16];
    ResolveTransformOrIdentity(objects, transport_guid_, current_transform);
    const float world_delta =
        openwow::game::Movement_GetObjectOrientation(objects, transport_guid_);
    PassengerQuaternion parent_rotation{};
    openwow::game::Movement_GetObjectWorldRotation(
        objects, transport_guid_, &parent_rotation.x);
    rebaseStoredZ(pose.position, current_transform);
    Passenger_ApplyParentTransform(
        pose, current_transform, world_delta, parent_rotation);

    RebaseTransportInterpolationState(current_transform, world_delta,
                                      true);

    if (!openwow::game::Movement_IsVehicleOrPlayerGuid(transport_guid_)) {
      RebaseQueuedTransportState(world_delta);
    }

    if (old_parent_body_facing_delta_out != nullptr) {
      *old_parent_body_facing_delta_out = world_delta;
    }
    notify_parent_rebase(true, transport_guid_,
                         world_delta);
  }

  if (transport_guid != 0) {
    float parent_transform[16];
    float parent_inverse[16];
    ResolveTransformOrIdentity(objects, transport_guid, parent_transform);
    const float parent_facing =
        openwow::game::Movement_GetObjectOrientation(objects, transport_guid);
    const auto source_position = pose.position;
    Passenger_ApplyInverseParentTransform(
        pose, parent_transform, parent_facing, parent_inverse);
    const float local_delta = -parent_facing;

    rebaseStoredZ(source_position, parent_inverse);
    RebaseTransportInterpolationState(parent_inverse, local_delta,
                                      false);
    if (!openwow::game::Movement_IsVehicleOrPlayerGuid(transport_guid)) {
      RebaseQueuedTransportState(local_delta);
    }

    if (new_parent_body_facing_delta_out != nullptr) {
      *new_parent_body_facing_delta_out = local_delta;
    }
    notify_parent_rebase(false, transport_guid, local_delta);
  }

  if (before_parent_commit) {
    before_parent_commit();
  } else if (transport_parent_commit_callback_) {
    transport_parent_commit_callback_(transport_guid_, transport_guid);
  }

  transform_position_ = pose.position;
  scalar_facing_ = pose.scalar_facing;
  packed_orientation_ = pose.packed_orientation;

  transport_guid_ = transport_guid;
  ClearQueuedMovementPreview();
  return true;
}

void CMovementData::SeedAuthoritativeTransportState(
    const MovementInfo &movement_info) {
  if (movement_info.IsOnTransport() && !movement_info.transport.guid.IsEmpty()) {
    transport_guid_ = movement_info.transport.guid.GetRawValue();
    transport_seat_ = static_cast<std::uint8_t>(movement_info.transport.seat);
    transform_position_ = {movement_info.transport.offset_x,
                           movement_info.transport.offset_y,
                           movement_info.transport.offset_z};
    scalar_facing_ = movement_info.transport.offset_o;
    return;
  }

  transport_guid_ = 0u;
  transport_seat_ = 0u;
  transform_position_ = {movement_info.x, movement_info.y, movement_info.z};
  scalar_facing_ = movement_info.orientation;
}

std::uint32_t CMovementData::ResolveTeleportArrivalFlags(std::uint32_t flags) {

  if ((flags & openwow::game::kMoveFlagFalling) != 0u) {
    flags &= ~(openwow::game::kMoveFlagFalling | openwow::game::kMoveFlagFallingFar);
  }

  if ((flags & openwow::game::kMoveFlagPendingRoot) != 0u) {
    flags |= openwow::game::kMoveFlagRoot;
  }

  return flags & kTeleportPreservedMoveFlagMask;
}

void CMovementData::ApplyTeleportArrivalPose(
    const MovementInfo &movement_info) {

  if (transport_guid_ != 0u && movement_info.IsOnTransport() &&
      movement_info.transport.guid.GetRawValue() == transport_guid_) {
    transform_position_ = {movement_info.transport.offset_x,
                           movement_info.transport.offset_y,
                           movement_info.transport.offset_z};
    scalar_facing_ = movement_info.transport.offset_o;
  } else {
    transform_position_ = {movement_info.x, movement_info.y, movement_info.z};
    scalar_facing_ = movement_info.orientation;
  }
  FinishTeleportArrival();
}

void CMovementData::FinishTeleportArrival() {

  runtime_pitch_ = 0.0f;
  runtime_flags_ = ResolveTeleportArrivalFlags(runtime_flags_);

  ApplyDirectionStateTransition(SpeedRefreshTiming::kAfterDirection);

  if (has_parent_movement_ &&
      (parent_movement_flags_ & kParentAllowStopFlag) == 0u) {
    runtime_flags2_ = static_cast<std::uint16_t>(
        runtime_flags2_ & ~kHasParentMovementUpdateBitMask);
    ClearParentMovement();
    if ((runtime_flags_ & openwow::game::kMoveFlagFalling) == 0u) {
      current_speed_ = CalculateCurrentSpeed(false);
    }
  }

  runtime_flags2_ = static_cast<std::uint16_t>(
      runtime_flags2_ & ~kInterpolatedFlags2Mask);
  ClearQueuedMovementPreview();
}

void CMovementData::ImportMovementOpcodeSnapshot(
    const MovementInfo &movement_info) {
  const std::uint32_t old_flags = runtime_flags_;
  runtime_flags_ = (movement_info.flags & kPacketOwnedFlagMask) |
                   (runtime_flags_ & kRuntimeOwnedFlagMask);
  runtime_flags2_ = movement_info.flags2;
  remote_gravity_changed_ =
      ((old_flags ^ runtime_flags_) & openwow::game::kMoveFlagDisableGravity) !=
      0u;
  runtime_pitch_ = movement_info.pitch;
  runtime_fall_time_ = movement_info.fall_time;
  runtime_jump_z_speed_ = movement_info.jump.z_speed;
  runtime_jump_sin_angle_ = movement_info.jump.sin_angle;
  runtime_jump_cos_angle_ = movement_info.jump.cos_angle;
  runtime_jump_xy_speed_ = movement_info.jump.xy_speed;

  if (transport_guid_ != 0u) {
    if (movement_info.IsOnTransport() &&
        movement_info.transport.guid.GetRawValue() == transport_guid_) {
      transform_position_ = {movement_info.transport.offset_x,
                             movement_info.transport.offset_y,
                             movement_info.transport.offset_z};
      scalar_facing_ = movement_info.transport.offset_o;
    }
  } else {
    transform_position_ = {movement_info.x, movement_info.y, movement_info.z};
    scalar_facing_ = movement_info.orientation;
  }

  if ((runtime_flags_ & openwow::game::kMoveFlagFalling) != 0u) {
    runtime_fall_start_z_ =
        transform_position_[2] +
        IntegrateFallDistance(
            static_cast<float>(runtime_fall_time_) * 0.001f,
            (runtime_flags_ & openwow::game::kMoveFlagFallingSlow) != 0u,
            runtime_jump_z_speed_);
  }
  prev_position_ = transform_position_;
  prev_orientation_ = scalar_facing_;
  prev_pitch_ = runtime_pitch_;
  interpolation_progress_ = 0.0f;

  if ((runtime_flags_ & openwow::game::kMoveFlagFalling) == 0u) {
    current_speed_ = CalculateCurrentSpeed(false);
  }
  ComputeDirectionVectors(false);

  packed_orientation_ = 0;
  uses_packed_orientation_ = false;
  ClearQueuedMovementPreview();
}

void CMovementData::SyncAuthoritativeMovementInfo(
    const MovementInfo &movement_info,
    const bool replace_spline_ownership) {
  const std::uint32_t spline_flag =
      replace_spline_ownership
          ? movement_info.flags & openwow::game::kMoveFlagSplineEnabled
          : runtime_flags_ & openwow::game::kMoveFlagSplineEnabled;
  runtime_flags_ =
      (movement_info.flags & kPacketOwnedFlagMask) | spline_flag |
      (runtime_flags_ & kSyncPreservedRuntimeFlagMask);
  runtime_flags2_ = movement_info.flags2;
  runtime_pitch_ = movement_info.pitch;
  runtime_fall_time_ = movement_info.fall_time;
  runtime_jump_z_speed_ = movement_info.jump.z_speed;
  runtime_jump_sin_angle_ = movement_info.jump.sin_angle;
  runtime_jump_cos_angle_ = movement_info.jump.cos_angle;
  runtime_jump_xy_speed_ = movement_info.jump.xy_speed;

  if (movement_info.IsOnTransport() && !movement_info.transport.guid.IsEmpty()) {
    transport_guid_ = movement_info.transport.guid.GetRawValue();
    transport_seat_ = static_cast<std::uint8_t>(movement_info.transport.seat);
    transform_position_ = {movement_info.transport.offset_x, movement_info.transport.offset_y,
                           movement_info.transport.offset_z};
    scalar_facing_ = movement_info.transport.offset_o;
  } else {
    transport_guid_ = 0;
    transport_seat_ = 0;
    transform_position_ = {movement_info.x, movement_info.y, movement_info.z};
    scalar_facing_ = movement_info.orientation;
  }

  if ((runtime_flags_ & openwow::game::kMoveFlagFalling) != 0u) {
    runtime_fall_start_z_ =
        transform_position_[2] +
        IntegrateFallDistance(
            static_cast<float>(runtime_fall_time_) * 0.001f,
            (runtime_flags_ & openwow::game::kMoveFlagFallingSlow) != 0u,
            runtime_jump_z_speed_);
  }

  if ((runtime_flags_ & openwow::game::kMoveFlagFalling) == 0u) {
    current_speed_ = CalculateCurrentSpeed(false);
  }
  ComputeDirectionVectors(false);

  prev_position_ = transform_position_;
  prev_orientation_ = scalar_facing_;
  interpolation_progress_ = 0.0f;

  packed_orientation_ = 0;
  uses_packed_orientation_ = false;
  ClearQueuedMovementPreview();
}

void CMovementData::SyncPresentedMovementInfo(
    const MovementInfo &movement_info) {
  const bool was_falling =
      (runtime_flags_ & openwow::game::kMoveFlagFalling) != 0u;
  constexpr std::uint16_t kQueuedPreviewFlags =
      static_cast<std::uint16_t>(
          openwow::game::kMoveFlag2InterpolatedMovement |
          openwow::game::kMoveFlag2InterpolatedTurning |
          openwow::game::kMoveFlag2InterpolatedPitching);
  runtime_flags_ = (movement_info.flags & ~kSyncPreservedRuntimeFlagMask) |
                   (runtime_flags_ & kSyncPreservedRuntimeFlagMask);
  runtime_flags2_ = static_cast<std::uint16_t>(
      (movement_info.flags2 & ~kQueuedPreviewFlags) |
      (runtime_flags2_ & kQueuedPreviewFlags));
  runtime_pitch_ = movement_info.pitch;
  runtime_fall_time_ = movement_info.fall_time;
  runtime_jump_z_speed_ = movement_info.jump.z_speed;
  runtime_jump_sin_angle_ = movement_info.jump.sin_angle;
  runtime_jump_cos_angle_ = movement_info.jump.cos_angle;
  runtime_jump_xy_speed_ = movement_info.jump.xy_speed;

  if (movement_info.IsOnTransport() && !movement_info.transport.guid.IsEmpty()) {
    transport_guid_ = movement_info.transport.guid.GetRawValue();
    transport_seat_ = static_cast<std::uint8_t>(movement_info.transport.seat);
    transform_position_ = {movement_info.transport.offset_x,
                           movement_info.transport.offset_y,
                           movement_info.transport.offset_z};
    scalar_facing_ = movement_info.transport.offset_o;
  } else {
    transport_guid_ = 0;
    transport_seat_ = 0;
    transform_position_ = {movement_info.x, movement_info.y, movement_info.z};
    scalar_facing_ = movement_info.orientation;
  }

  const bool is_falling =
      (runtime_flags_ & openwow::game::kMoveFlagFalling) != 0u;
  if (is_falling && !was_falling) {
    runtime_fall_start_z_ =
        transform_position_[2] +
        IntegrateFallDistance(
            static_cast<float>(runtime_fall_time_) * 0.001f,
            (runtime_flags_ & openwow::game::kMoveFlagFallingSlow) != 0u,
            runtime_jump_z_speed_);
  }

  if ((runtime_flags_ & openwow::game::kMoveFlagFalling) == 0u) {
    current_speed_ = CalculateCurrentSpeed(false);
  }
  ComputeDirectionVectors(false);

  packed_orientation_ = 0;
  uses_packed_orientation_ = false;
}

void CMovementData::AdvanceKinematics(const std::uint32_t step_ms) {

  if (step_ms == 0u || (runtime_flags_ & kActiveMotionFlagMask) == 0u ||
      (runtime_flags_ & kVehicleControlTransferFlag) != 0u) {
    return;
  }

  MovementInfo state;
  state.flags = runtime_flags_ & 0x7FFFFFFFu;
  state.flags2 = runtime_flags2_;
  state.x = transform_position_[0];
  state.y = transform_position_[1];
  state.z = transform_position_[2];
  state.orientation = scalar_facing_;
  state.pitch = runtime_pitch_;
  state.fall_time = runtime_fall_time_;
  state.jump.z_speed = runtime_jump_z_speed_;
  state.jump.sin_angle = runtime_jump_sin_angle_;
  state.jump.cos_angle = runtime_jump_cos_angle_;
  state.jump.xy_speed = runtime_jump_xy_speed_;

  const float elapsed_seconds = static_cast<float>(step_ms) * 0.001f;
  const auto step =
      ComputeRetailMovementStep(state, speed_table_, elapsed_seconds,
                                current_speed_);
  scalar_facing_ = step.orientation;
  runtime_pitch_ = step.pitch;

  const bool falling =
      (state.flags & openwow::game::kMoveFlagFalling) != 0u;
  transform_position_[0] += step.x;
  transform_position_[1] += step.y;
  transform_position_[2] += step.z;

  if (falling) {
    runtime_fall_time_ += step_ms;

    transform_position_[2] -= ComputeCollisionFallDisplacement(
        state.flags, runtime_jump_z_speed_, state.z, runtime_fall_start_z_,
        runtime_fall_time_);
  }
}

CMovementData::ServerMovementTimingDecision
CMovementData::ResolveServerMovementTiming(
    std::uint32_t event_tick, const std::uint32_t server_movement_tick,
    const std::uint32_t runtime_current_tick, bool parent_timeline) {
  constexpr std::int32_t kMinPresentationDeltaMs = -500;
  constexpr std::int32_t kMaxPresentationDeltaMs = 1000;
  constexpr std::uint32_t kPresentationLockMotionMask = 0x00C010FFu;

  const auto signed_delta = [](const std::uint32_t newer,
                               const std::uint32_t older) {
    return static_cast<std::int32_t>(newer - older);
  };
  const auto wrapped_add = [](const std::int32_t lhs,
                              const std::int32_t rhs) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(lhs) +
                                     static_cast<std::uint32_t>(rhs));
  };
  const auto wrapped_subtract = [](const std::int32_t lhs,
                                   const std::int32_t rhs) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(lhs) -
                                     static_cast<std::uint32_t>(rhs));
  };
  const auto clamp_i16 = [](const std::int32_t value) {
    return static_cast<std::int16_t>(std::clamp(
        value,
        static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::min()),
        static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::max())));
  };

  if (has_parent_movement_) {
    parent_timeline = true;
  }

  if ((runtime_flags_ & 0x80000000u) == 0u) {
    server_event_presentation_anchor_ = event_tick;
    runtime_flags_ |= 0x80000000u;
    server_movement_timestamp_baseline_ = server_movement_tick;
  }

  if (signed_delta(event_tick, runtime_current_tick) < 0) {
    event_tick = runtime_current_tick;
  }

  std::int32_t server_delta =
      signed_delta(server_movement_tick, server_movement_timestamp_baseline_);
  if (server_delta < 1) {
    server_delta = 0;
  } else {
    server_movement_timestamp_baseline_ = server_movement_tick;
  }

  std::int32_t presentation_delta = wrapped_subtract(
      server_delta,
      signed_delta(event_tick, server_event_presentation_anchor_));
  const auto sample = clamp_i16(
      wrapped_subtract(server_timing_bias_ms_, presentation_delta));
  server_timing_bias_history_[server_timing_bias_head_] = sample;
  server_timing_bias_head_ =
      static_cast<std::uint8_t>((server_timing_bias_head_ + 1u) & 0x1Fu);

  std::int32_t maximum_bias = sample;
  for (const auto entry : server_timing_bias_history_) {
    maximum_bias = std::max(maximum_bias, static_cast<std::int32_t>(entry));
  }

  if (!parent_timeline) {
    if ((runtime_flags_ & kPresentationLockMotionMask) == 0u &&
        !event_queue_.HasEvents()) {
      presentation_delta = std::clamp(
          wrapped_add(presentation_delta,
                      wrapped_subtract(maximum_bias,
                                       server_timing_bias_ms_)),
          kMinPresentationDeltaMs, kMaxPresentationDeltaMs);
      if (signed_delta(event_tick + presentation_delta,
                       server_event_presentation_anchor_) < 0) {
        presentation_delta = signed_delta(server_event_presentation_anchor_,
                                           event_tick);
      }
      server_timing_bias_ms_ = maximum_bias;
    }

    presentation_delta = std::clamp(
        presentation_delta, kMinPresentationDeltaMs,
        kMaxPresentationDeltaMs);
  }

  server_event_presentation_anchor_ = event_tick + presentation_delta;
  return ServerMovementTimingDecision{
      .presentation_tick = server_event_presentation_anchor_,
      .presentation_delta_ms = presentation_delta,
      .apply_now = parent_timeline ||
                   signed_delta(runtime_current_tick,
                                server_event_presentation_anchor_) >= 0,
  };
}

void CMovementData::ResetServerMovementTimeline(
    const std::uint32_t event_tick,
    const std::uint32_t server_movement_tick) noexcept {
  server_event_presentation_anchor_ = event_tick;
  server_movement_timestamp_baseline_ = server_movement_tick;
  server_timing_bias_history_.fill(0);
  server_timing_bias_head_ = 0;
  server_timing_bias_ms_ = 0;
  runtime_flags_ |= 0x80000000u;
}

void CMovementData::QueueDeferredAuthoritativeMovement(
    const std::uint32_t presentation_tick, const std::uint32_t event_type,
    const std::uint32_t opcode, const MovementInfo& movement_info,
    const float auxiliary_f32) {
  auto event = MakeEvent(presentation_tick, event_type);
  event.needs_ack = false;
  event.auxiliary_f32 = auxiliary_f32;
  event.deferred_authoritative_movement =
      std::make_shared<const MovementInfo>(movement_info);
  event.deferred_authoritative_opcode = opcode;
  QueueAndNotify(std::move(event));
}

bool CMovementData::SetStrafeDirection(const bool strafe_left) {
  constexpr std::uint32_t kPendingStrafeClearMask =
      ~(openwow::game::kMoveFlagPendingStrafeStop |
        openwow::game::kMoveFlagPendingStrafeLeft |
        openwow::game::kMoveFlagPendingStrafeRight);
  constexpr std::uint32_t kDirectionMask =
      openwow::game::kMoveFlagForward | openwow::game::kMoveFlagBackward |
      openwow::game::kMoveFlagStrafeLeft | openwow::game::kMoveFlagStrafeRight;
  constexpr std::uint32_t kStrafeBitsClear =
      ~(openwow::game::kMoveFlagStrafeLeft | openwow::game::kMoveFlagStrafeRight);

  runtime_flags_ &= kPendingStrafeClearMask;

  if ((runtime_flags2_ & static_cast<std::uint16_t>(
           openwow::game::kMoveFlag2NoStrafe)) != 0) {
    return false;
  }

  const bool is_falling =
      (runtime_flags_ & openwow::game::kMoveFlagFalling) != 0;

  if (is_falling && (runtime_flags_ & kDirectionMask) != 0) {
    if (strafe_left) {
      if ((runtime_flags_ & openwow::game::kMoveFlagStrafeLeft) == 0) {
        runtime_flags_ |= openwow::game::kMoveFlagPendingStrafeLeft;
        return false;
      }
    } else {
      if ((runtime_flags_ & openwow::game::kMoveFlagStrafeRight) == 0) {
        runtime_flags_ |= openwow::game::kMoveFlagPendingStrafeRight;
      }
    }
    return false;
  }

  const bool need_snapshot = is_falling;
  if (strafe_left) {
    runtime_flags_ = (runtime_flags_ & kStrafeBitsClear) |
                     openwow::game::kMoveFlagStrafeLeft;
  } else {
    runtime_flags_ = (runtime_flags_ & kStrafeBitsClear) |
                     openwow::game::kMoveFlagStrafeRight;
  }

  prev_position_ = transform_position_;
  prev_orientation_ = scalar_facing_;
  prev_pitch_ = runtime_pitch_;
  interpolation_progress_ = 0.0f;
  ComputeDirectionVectors(need_snapshot);
  current_speed_ = CalculateCurrentSpeed(need_snapshot);

  return true;
}

bool CMovementData::HandleRemoteForwardStart(const bool forward) {
  if (!SetForwardDirection(forward)) {
    return false;
  }

  has_pending_runtime_notification_ = true;
  if (runtime_notification_callback_) {
    runtime_notification_callback_(*this);
  }

  return true;
}

bool CMovementData::HandleRemoteJump() {
  if (!TryStartJump()) {
    return false;
  }

  has_pending_runtime_notification_ = true;
  if (runtime_notification_callback_) {
    runtime_notification_callback_(*this);
  }
  return true;
}

bool CMovementData::HandleRemoteStrafeStart(const bool strafe_left) {
  if (!SetStrafeDirection(strafe_left)) {
    return false;
  }

  has_pending_runtime_notification_ = true;
  if (runtime_notification_callback_) {
    runtime_notification_callback_(*this);
  }

  return true;
}

void CMovementData::SetTurnDirection(const bool turn_left) {
  constexpr std::uint32_t kTurnBitsClear =
      ~(openwow::game::kMoveFlagTurnLeft | openwow::game::kMoveFlagTurnRight);

  if (turn_left) {
    runtime_flags_ = (runtime_flags_ & kTurnBitsClear) |
                     openwow::game::kMoveFlagTurnLeft;
  } else {
    runtime_flags_ = (runtime_flags_ & kTurnBitsClear) |
                     openwow::game::kMoveFlagTurnRight;
  }

  runtime_flags2_ &= ~static_cast<std::uint16_t>(
      openwow::game::kMoveFlag2InterpolatedTurning);

  SnapshotStateForDirectionRecompute();
}

void CMovementData::HandleRemoteTurnStart(const bool turn_left) {
  SetTurnDirection(turn_left);

  has_pending_runtime_notification_ = true;
  if (runtime_notification_callback_) {
    runtime_notification_callback_(*this);
  }
}

void CMovementData::HandleRemotePitchStart(const bool pitch_up) {
  SetPitchDirection(pitch_up);

  has_pending_runtime_notification_ = true;
  if (runtime_notification_callback_) {
    runtime_notification_callback_(*this);
  }
}

void CMovementData::HandleRemotePoseSnapshot() {
  has_pending_runtime_notification_ = true;
  if (runtime_notification_callback_) {
    runtime_notification_callback_(*this);
  }
}

bool CMovementData::HandleRemoteAscendDescend(const bool ascending) {
  if (!SetAscendDescend(ascending)) {
    return false;
  }
  HandleRemotePoseSnapshot();
  return true;
}

bool CMovementData::HandleRemoteStopAscendDescend() {
  if ((runtime_flags_ & 0x0Fu) != 0u) {
    return false;
  }
  HandleRemotePoseSnapshot();
  return true;
}

void CMovementData::HandleRemoteStartSwim() {
  ResetMovementBaseState();
  (void)RecalculateStateFlags();
  HandleRemotePoseSnapshot();
}

void CMovementData::HandleRemoteHoverSnapshot() {
  if ((runtime_flags_ & openwow::game::kMoveFlagHover) != 0u) {
    (void)TryStartJump(true);
  } else {
    (void)TryInitRemoteMovement();
  }
  HandleRemotePoseSnapshot();
}

bool CMovementData::HandleRemoteWaterWalkSnapshot() {
  if ((runtime_flags_ & openwow::game::kMoveFlagWaterwalking) == 0u) {
    (void)TryInitRemoteMovement();
  }
  return true;
}

bool CMovementData::HandleRemoteCanFlySnapshot() {
  if ((runtime_flags_ & openwow::game::kMoveFlagCanFly) != 0u) {
    return true;
  }
  if ((runtime_flags_ & openwow::game::kMoveFlagFlying) != 0u) {
    DisableFlyMode();
  }
  runtime_flags_ &= ~openwow::game::kMoveFlagCanFly;
  return true;
}

bool CMovementData::HandleRemoteGravitySnapshot() {
  if (!remote_gravity_changed_) {
    return true;
  }
  remote_gravity_changed_ = false;
  RefreshGravityState();
  return true;
}

bool CMovementData::HandleRemoteCanTransitionSwimFly(const bool enabled) {
  constexpr std::uint16_t kTransitionBit = static_cast<std::uint16_t>(
      openwow::game::kMoveFlag2CanTransitionBetweenSwimAndFly);

  if (enabled) {
    runtime_flags2_ |= kTransitionBit;
  } else {
    runtime_flags2_ &= ~kTransitionBit;
  }

  return true;
}

bool CMovementData::HandleRemoteChangeTransportSeat() {
  has_pending_runtime_notification_ = true;
  if (runtime_notification_callback_) {
    runtime_notification_callback_(*this);
  }

  return true;
}

bool CMovementData::HandleRemoteSpeedAck(
    const bool spline_enabled, const float new_speed,
    const openwow::game::SpeedType speed_type) {
  if (!spline_enabled) {
    ClearParentMovement();
  }

  if (!SetSpeed(speed_type, new_speed)) {
    return false;
  }

  has_pending_runtime_notification_ = true;
  if (runtime_notification_callback_) {
    runtime_notification_callback_(*this);
  }

  return true;
}

void CMovementData::HandleRemoteRootAck() {
  ForceApplyRoot();

  has_pending_runtime_notification_ = true;
  if (runtime_notification_callback_) {
    runtime_notification_callback_(*this);
  }
}

void CMovementData::HandleRemoteUnrootAck() {
  ForceRemoveRoot(true);

  has_pending_runtime_notification_ = true;
  if (runtime_notification_callback_) {
    runtime_notification_callback_(*this);
  }
}

void CMovementData::ResetMovementBaseState() {

  constexpr std::uint32_t kClearMask =
      openwow::game::kMoveFlagFlying |
      openwow::game::kMoveFlagSplineElevation |
      openwow::game::kMoveFlagSwimming;
  runtime_flags_ = (runtime_flags_ & ~kClearMask) |
                   openwow::game::kMoveFlagSwimming;

  const bool was_falling =
      (runtime_flags_ & openwow::game::kMoveFlagFalling) != 0u;
  if (was_falling) {

    runtime_flags_ &= ~(openwow::game::kMoveFlagFalling |
                        openwow::game::kMoveFlagFallingFar);
  }

  if ((runtime_flags_ & openwow::game::kMoveFlagPendingRoot) != 0u) {
    constexpr std::uint32_t kApplyDeferredRootKeepMask = 0xFF203F00u;
    runtime_flags_ =
        (runtime_flags_ | openwow::game::kMoveFlagRoot) &
        kApplyDeferredRootKeepMask;
  }

  SnapshotStateForDirectionRecompute();

  if ((runtime_flags_ & openwow::game::kMoveFlagFalling) == 0u) {
    current_speed_ = CalculateCurrentSpeed(false);
  }
}

void CMovementData::ResetClientControlTransition() {
  constexpr std::uint32_t kPendingClientControlReset = 0x00100000u;
  if ((runtime_flags_ & kPendingClientControlReset) == 0u) {
    return;
  }

  const std::uint32_t old_flags = runtime_flags_;
  std::uint32_t resolved_flags = old_flags | 0x00000800u;
  if ((old_flags & 0x00001000u) != 0u) {
    resolved_flags &= 0xFFFFCFFFu;
  }

  runtime_flags_ = (resolved_flags | 0x00080000u) & 0xFF203F00u;
  interpolation_progress_ = 0.0f;
  SnapshotStateForDirectionRecompute();
  ComputeDirectionVectors(true);

  if (!has_pending_runtime_notification_) {
    has_pending_runtime_notification_ = true;
    if (runtime_notification_callback_) {
      runtime_notification_callback_(*this);
    }
  }

  runtime_flags_ = resolved_flags & 0xFF203F00u;
  ClearQueuedMovementPreview();
  if ((resolved_flags & 0x00001000u) == 0u) {
    current_speed_ = CalculateCurrentSpeed(false);
  }
}

std::uint32_t CMovementData::RecalculateStateFlags() {
  const std::uint32_t old_flags = runtime_flags_;

  if ((runtime_flags_ & openwow::game::kMoveFlagPendingStop) != 0u) {
    runtime_flags_ &= ~(openwow::game::kMoveFlagForward |
                         openwow::game::kMoveFlagBackward |
                         openwow::game::kMoveFlagPendingStop);
    SnapshotStateForDirectionRecompute();

    const bool is_active_mover = update_callbacks_.is_active_mover
                                     ? update_callbacks_.is_active_mover(*this)
                                     : false;
    if (is_active_mover &&
        !event_queue_.HasEventInTypeRange(
            static_cast<std::uint32_t>(MoveEventType::kStartForward),
            static_cast<std::uint32_t>(MoveEventType::kStartBackward))) {
      if (update_callbacks_.clear_input_control_on_stop) {
        update_callbacks_.clear_input_control_on_stop(*this);
      }
    }
  }

  if ((runtime_flags_ & openwow::game::kMoveFlagPendingStrafeStop) != 0u) {

    runtime_flags_ &= ~(openwow::game::kMoveFlagStrafeLeft |
                         openwow::game::kMoveFlagStrafeRight |
                         openwow::game::kMoveFlagPendingStrafeStop);
    SnapshotStateForDirectionRecompute();
  }

  if ((runtime_flags_ & (openwow::game::kMoveFlagPendingForward |
                          openwow::game::kMoveFlagPendingBackward)) != 0u) {

    const bool forward =
        (runtime_flags_ & openwow::game::kMoveFlagPendingForward) != 0u;
    runtime_flags_ &= ~(openwow::game::kMoveFlagForward |
                         openwow::game::kMoveFlagBackward |
                         openwow::game::kMoveFlagPendingStop |
                         openwow::game::kMoveFlagPendingForward |
                         openwow::game::kMoveFlagPendingBackward);
    runtime_flags_ |= forward ? openwow::game::kMoveFlagForward
                               : openwow::game::kMoveFlagBackward;
    SnapshotStateForDirectionRecompute();
  }

  if ((runtime_flags_ & (openwow::game::kMoveFlagPendingStrafeLeft |
                          openwow::game::kMoveFlagPendingStrafeRight)) != 0u) {
    const bool strafe_left =
        (runtime_flags_ & openwow::game::kMoveFlagPendingStrafeLeft) != 0u;
    runtime_flags_ &= ~(openwow::game::kMoveFlagStrafeLeft |
                         openwow::game::kMoveFlagStrafeRight |
                         openwow::game::kMoveFlagPendingStrafeLeft |
                         openwow::game::kMoveFlagPendingStrafeRight);
    runtime_flags_ |= strafe_left ? openwow::game::kMoveFlagStrafeLeft
                                   : openwow::game::kMoveFlagStrafeRight;
    SnapshotStateForDirectionRecompute();
  }

  constexpr std::uint32_t kAllPendingMask =
      openwow::game::kMoveFlagPendingStop |

      openwow::game::kMoveFlagPendingStrafeStop |

      openwow::game::kMoveFlagPendingForward |

      openwow::game::kMoveFlagPendingBackward |

      openwow::game::kMoveFlagPendingStrafeLeft |

      openwow::game::kMoveFlagPendingStrafeRight |

      openwow::game::kMoveFlagPendingRoot;

  runtime_flags_ &= ~kAllPendingMask;

  return (old_flags ^ runtime_flags_) & 0x0Fu;
}

void CMovementData::HandleRemoteTeleportReset() {
  HandleRemoteStartSwim();
}

void CMovementData::HandleRemoteStopSwimReset() {
  ApplyStopSwimState();
  ClearQueuedMovementPreview();

  if (!has_pending_runtime_notification_) {
    has_pending_runtime_notification_ = true;
    if (runtime_notification_callback_) {
      runtime_notification_callback_(*this);
    }
  }
}

int CMovementData::DispatchDueEvents(
    const openwow::game::ObjectManager& objects,
    const std::uint32_t timestamp) {
  const auto active_mover = [this] {
    return update_callbacks_.is_active_mover
               ? update_callbacks_.is_active_mover(*this)
               : false;
  };
  const auto emit = [&](const std::uint16_t opcode,
                        const CPlayerMoveEvent& event) {
    if (dispatch_movement_opcode_callback_) {
      dispatch_movement_opcode_callback_(*this, opcode, timestamp, event);
    }
  };

  const auto is_basic_suppressed_type = [](const std::uint32_t type) {
    return type <= 5u || type == 9u || type == 10u ||
           type == static_cast<std::uint32_t>(MoveEventType::kEnableFlyMode) ||
           type == static_cast<std::uint32_t>(MoveEventType::kEnableSwimMode);
  };
  while (event_queue_.HasEvents() &&
         !TickBefore(timestamp, event_queue_.PeekFront().timestamp)) {
    const CPlayerMoveEvent& queued = event_queue_.PeekFront();
    if (event_processing_gate_callback_ &&
        !event_processing_gate_callback_(*this, queued)) {
      return 2;
    }
    if (queued.deferred_authoritative_movement &&
        !dispatch_deferred_authoritative_movement_callback_) {
      return 2;
    }

    CPlayerMoveEvent event = event_queue_.PopFront();
    const std::uint32_t type = event.event_type;

    bool discard_for_state = false;
    if ((runtime_flags_ & 0x00000200u) != 0u) {
      discard_for_state = is_basic_suppressed_type(type) || type == 21u ||
                          type == 22u || type == 34u;
    } else if ((runtime_flags_ & 0x00000800u) != 0u) {
      discard_for_state =
          is_basic_suppressed_type(type) || type == 21u || type == 22u;
    } else if ((runtime_flags_ & 0x00100000u) != 0u) {
      discard_for_state = is_basic_suppressed_type(type);
    }
    if (discard_for_state) {
      continue;
    }

    if (event.deferred_authoritative_movement) {
      dispatch_deferred_authoritative_movement_callback_(*this, event);
      continue;
    }

    if (event.has_runtime_snapshot &&
        (event.runtime_flags & openwow::game::kMoveFlagSplineEnabled) == 0u) {
      runtime_flags2_ &= 0xFF7Fu;
      ClearParentMovement();
      if ((runtime_flags_ & openwow::game::kMoveFlagFalling) != 0u &&
          (event.runtime_flags & openwow::game::kMoveFlagFalling) == 0u) {
        UpdateDirectionConditional();
      }
    }

    switch (static_cast<MoveEventType>(type)) {
      case MoveEventType::kStartForward:
        (void)SetForwardDirection(true);
        emit(0x00B5u, event);
        break;
      case MoveEventType::kStartBackward:
        (void)SetForwardDirection(false);
        emit(0x00B6u, event);
        break;
      case MoveEventType::kStopForwardBackward:
        (void)ExecStopForwardBackward();
        emit(0x00B7u, event);
        break;
      case MoveEventType::kStartStrafeLeft:
        (void)SetStrafeDirection(true);
        emit(0x00B8u, event);
        break;
      case MoveEventType::kStartStrafeRight:
        (void)SetStrafeDirection(false);
        emit(0x00B9u, event);
        break;
      case MoveEventType::kStopStrafe:
        ClearStrafeAndRecalculate();
        emit(0x00BAu, event);
        break;
      case MoveEventType::kStartAscend:
        if (SetAscendDescend(true)) emit(0x0359u, event);
        break;
      case MoveEventType::kStartDescend:
        if (SetAscendDescend(false)) emit(0x03A7u, event);
        break;
      case MoveEventType::kStopVerticalExplicit:
        if (ClearAscendDescend()) emit(0x035Au, event);
        break;
      case MoveEventType::kHeartbeat:
        if (TryInitRemoteMovement() && event.needs_ack) {
          emit(0x00EEu, event);
        }
        break;
      case MoveEventType::kJump:
        if (TryStartJump()) {
          emit(0x00BBu, event);
        }
        break;
      case MoveEventType::kStartTurnLeft:
        SetTurnDirection(true);
        emit(0x00BCu, event);
        break;
      case MoveEventType::kStartTurnRight:
        SetTurnDirection(false);
        emit(0x00BDu, event);
        break;
      case MoveEventType::kStopTurn:
      case MoveEventType::kScheduledTurnStop:
        (void)ProcessPendingTurnStop();
        emit(0x00BEu, event);
        break;
      case MoveEventType::kStartPitchUp:
        SetPitchDirection(true);
        emit(0x00BFu, event);
        break;
      case MoveEventType::kStartPitchDown:
        SetPitchDirection(false);
        emit(0x00C0u, event);
        break;
      case MoveEventType::kStopPitch:
      case MoveEventType::kScheduledPitchStop:
        (void)ProcessPendingPitchStop();
        emit(0x00C1u, event);
        break;
      case MoveEventType::kStartRun:
        runtime_flags_ &= ~openwow::game::kMoveFlagWalking;
        ApplyDirectionStateTransition(SpeedRefreshTiming::kAfterDirection);
        emit(0x00C2u, event);
        break;
      case MoveEventType::kStartWalk:
        runtime_flags_ |= openwow::game::kMoveFlagWalking;
        ApplyDirectionStateTransition(SpeedRefreshTiming::kAfterDirection);
        emit(0x00C3u, event);
        break;
      case MoveEventType::kSetFacing:
        ApplySetFacingEvent(event.cos_angle);
        emit(0x00DAu, event);
        break;
      case MoveEventType::kSetPitch:
        if (!SetPitchAndTestSteepFall(event.sin_angle)) {
          emit(0x00DBu, event);
        }
        break;
      case MoveEventType::kStartSwim:
        ResetMovementBaseState();
        (void)RecalculateStateFlags();
        emit(0x00CAu, event);
        break;
      case MoveEventType::kStopSwim:
        ApplyStopSwimState();
        emit(0x00CBu, event);
        break;
      case MoveEventType::kSetWalkSpeed:
      case MoveEventType::kSetRunBackSpeed:
      case MoveEventType::kSetSwimSpeed:
      case MoveEventType::kSetSwimBackSpeed:
      case MoveEventType::kSetTurnRate:
      case MoveEventType::kSetFlightSpeed:
      case MoveEventType::kSetFlightBackSpeed:
      case MoveEventType::kSetRunSpeed:
      case MoveEventType::kSetPitchRate: {
        constexpr std::array<openwow::game::SpeedType, 9> kSpeedTypes{
            openwow::game::kSpeedRun,        openwow::game::kSpeedRunBack,
            openwow::game::kSpeedWalk,       openwow::game::kSpeedSwim,
            openwow::game::kSpeedSwimBack,   openwow::game::kSpeedFlight,
            openwow::game::kSpeedFlightBack, openwow::game::kSpeedTurnRate,
            openwow::game::kSpeedPitchRate};
        constexpr std::array<std::uint16_t, 9> kSpeedOpcodes{
            0x00E3u, 0x00E5u, 0x02DBu, 0x00E7u, 0x02DDu,
            0x0382u, 0x0384u, 0x02DFu, 0x045Du};
        const std::size_t index = type - 23u;
        (void)SetSpeed(kSpeedTypes[index], event.auxiliary_f32);
        emit(kSpeedOpcodes[index], event);
        break;
      }
      case MoveEventType::kGravityEnable:
        (void)SetGravityEnabledAndRefresh(true);
        emit(0x04D1u, event);
        break;
      case MoveEventType::kGravityDisable:
        (void)SetGravityEnabledAndRefresh(false);
        emit(0x04CFu, event);
        break;
      case MoveEventType::kKnockBack:
        ApplyKnockBackState(objects, event.cos_angle, event.sin_angle,
                            event.auxiliary_f32, event.jump_z_speed);
        emit(0x00F0u, event);
        break;
      case MoveEventType::kFeatherFallEnable:
        SetFallingSlowState(true);
        emit(0x02CFu, event);
        break;
      case MoveEventType::kFeatherFallDisable:
        SetFallingSlowState(false);
        emit(0x02CFu, event);
        break;
      case MoveEventType::kHoverEnable:
        SetHoverStateAndRefresh(true, !event.needs_ack || active_mover());
        emit(0x00F6u, event);
        break;
      case MoveEventType::kHoverDisable:
        SetHoverStateAndRefresh(false, !event.needs_ack || active_mover());
        emit(0x00F6u, event);
        break;
      case MoveEventType::kWaterWalkEnable:
        runtime_flags_ |= openwow::game::kMoveFlagWaterwalking;
        emit(0x02D0u, event);
        break;
      case MoveEventType::kWaterWalkDisable:
        runtime_flags_ &= ~openwow::game::kMoveFlagWaterwalking;
        if (!event.needs_ack || active_mover()) {
          (void)TryInitRemoteMovement();
        }
        emit(0x02D0u, event);
        break;
      case MoveEventType::kRoot:
        ForceApplyRoot();
        emit(0x00E9u, event);
        break;
      case MoveEventType::kUnroot:
        ForceRemoveRoot(!event.needs_ack || active_mover());
        emit(0x00EBu, event);
        break;
      case MoveEventType::kChangeTransportSeat:

        if (event.has_runtime_snapshot) {
          runtime_flags2_ = static_cast<std::uint16_t>(
              (runtime_flags2_ & ~kTeleportPacketOwnedFlags2Mask) |
              (event.runtime_flags2 & kTeleportPacketOwnedFlags2Mask));
          RestoreRuntimeSnapshot(event);
        }
        (void)ForceSetTransport(objects, event.runtime_transport_guid,
                                event.runtime_transport_seat);
        if (transport_guid_ == event.runtime_transport_guid) {
          FinishTeleportArrival();
        }
        emit(0x00C7u, event);
        break;
      case MoveEventType::kEnableFlyMode:
        if (EnableFlyMode()) {
          (void)RecalculateStateFlags();
          emit(0x0346u, event);
        }
        break;
      case MoveEventType::kEnableSwimMode:
        DisableFlyMode();
        emit(0x0346u, event);
        break;
      case MoveEventType::kCanFlyEnable:
        runtime_flags_ |= openwow::game::kMoveFlagCanFly;
        emit(0x0345u, event);
        break;
      case MoveEventType::kCanFlyDisable:
        if ((runtime_flags_ & openwow::game::kMoveFlagFlying) != 0u) {
          DisableFlyMode();
        }
        runtime_flags_ &= ~openwow::game::kMoveFlagCanFly;
        emit(0x0345u, event);
        break;
      case MoveEventType::kTimeSync:
        emit(0x0391u, event);
        break;
      case MoveEventType::kDismissControlledVehicle:
        ProcessPendingMovementStops();
        emit(0x046Du, event);
        break;
      case MoveEventType::kBoundedTurnFacing: {
        const bool turn_left = ScheduleTurnToFacing(
            timestamp, NormalizeSignedAngle(event.cos_angle - scalar_facing_));
        SetTurnDirection(turn_left);
        emit(turn_left ? 0x00BCu : 0x00BDu, event);
        break;
      }
      case MoveEventType::kSetVehiclePitch: {
        const bool pitch_up = SchedulePitchToTarget(
            timestamp, NormalizeSignedAngle(event.sin_angle - runtime_pitch_));
        SetPitchDirection(pitch_up);
        emit(pitch_up ? 0x00BFu : 0x00C0u, event);
        break;
      }
      case MoveEventType::kVehicleSeatSwitch: {
        const std::uint64_t guid =
            static_cast<std::uint64_t>(event.auxiliary_u32) |
            (static_cast<std::uint64_t>(event.auxiliary_u32_secondary) << 32u);
        (void)ForceSetTransport(objects, guid, event.auxiliary_u8);
        emit(0x049Bu, event);
        break;
      }
      case MoveEventType::kSwimFlyTransitionEnable:
        runtime_flags2_ |= 0x4000u;
        emit(0x0340u, event);
        break;
      case MoveEventType::kSwimFlyTransitionDisable:
        runtime_flags2_ &= 0xBFFFu;
        emit(0x0340u, event);
        break;
      case MoveEventType::kSetCollisionHeight:
        collision_height_product_ = event.auxiliary_f32;
        emit(0x0517u, event);
        break;
      case MoveEventType::kFallLand:

        emit(0x00C9u, event);
        break;
      default:
        break;
    }

    if (type != static_cast<std::uint32_t>(
                    MoveEventType::kChangeTransportSeat) &&
        event.has_runtime_snapshot) {
      RestoreRuntimeSnapshot(event);
      (void)ForceSetTransport(objects, event.runtime_transport_guid,
                              event.runtime_transport_seat);
    }
    RefreshQueuedMovementPreview(timestamp);
  }

  return event_queue_.HasEvents() ||
                 (runtime_flags_ & 0x00C010FFu) != 0u
             ? 1
             : 0;
}

int CMovementData::FlushQueuedEvents(
    const openwow::game::ObjectManager& objects) {
  int result = 0;
  while (event_queue_.HasEvents()) {
    const std::size_t previous_size = event_queue_.Size();
    result = DispatchDueEvents(objects, event_queue_.PeekFront().timestamp);
    if (result == 2 || event_queue_.Size() >= previous_size) {
      return result;
    }
  }
  return result;
}

CMovementData::UpdateResult CMovementData::Update(
    const openwow::game::ObjectManager& objects,
    const std::uint32_t end_time,
    const std::uint32_t current_time,
    RuntimeTimelineState* const runtime_timeline) {

  const std::uint32_t total_remaining = end_time - current_time;

  RuntimeTimelineState& timeline = runtime_timeline != nullptr
                                       ? *runtime_timeline
                                       : fallback_runtime_timeline_;
  const auto is_active_mover = [this] {
    return update_callbacks_.is_active_mover
               ? update_callbacks_.is_active_mover(*this)
               : false;
  };
  const auto parent_allows_active_advance = [this] {
    return !has_parent_movement_ ||
           (parent_movement_flags_ & kParentAllowStopFlag) != 0u;
  };

  std::uint32_t cursor = current_time;
  std::uint32_t consumed = 0;
  std::uint32_t capped_remaining = total_remaining;
  UpdateResult update_result = UpdateResult::kCompleted;
  timeline.current_tick = cursor;

  if (total_remaining > kMaxUpdateStepMs) {

    const std::uint32_t excess = total_remaining - kMaxUpdateStepMs;
    cursor = current_time + excess;
    timeline.current_tick = cursor;
    if (is_active_mover()) {
      timeline.active_mover_deadline += excess;

      if (update_callbacks_.skip_excess_time) {
        update_callbacks_.skip_excess_time(*this, excess);
      }
    }

    capped_remaining = kMaxUpdateStepMs;
  } else {

    if (total_remaining == 0) {
      goto post_loop;
    }

  }

  for (;;) {

    std::uint32_t step = capped_remaining - consumed;

    if (event_queue_.HasEvents()) {
      const auto& head = event_queue_.PeekFront();
      const std::int32_t available =
          static_cast<std::int32_t>(head.timestamp - cursor);
      const std::uint32_t clamped_available =
          (available > 0) ? static_cast<std::uint32_t>(available) : 0u;
      if (step > clamped_available) {
        step = clamped_available;
      }
    }

    const bool active_mover = is_active_mover();
    if (active_mover && parent_allows_active_advance() &&
        (runtime_flags_ & kActiveMoverMotionMask) != 0u) {
      const std::uint32_t budget_remaining =
          timeline.active_mover_deadline - cursor;
      if (budget_remaining < step) {
        step = budget_remaining;
      }
    }

    cursor += step;
    timeline.current_tick = cursor;

    if (step > 0) {

      if ((runtime_flags_ & kActiveMotionFlagMask) != 0) {

        if ((runtime_flags_ & kVehicleControlTransferFlag) != 0) {
          timeline.active_mover_deadline += step;
        } else {

          if (update_callbacks_.process_movement_loop) {
            update_callbacks_.process_movement_loop(*this, cursor, step);
          }

          if (active_mover && parent_allows_active_advance() &&
              (runtime_flags_ & kActiveMoverMotionMask) != 0 &&
              static_cast<std::int32_t>(
                  cursor - timeline.active_mover_deadline) >= 0) {
            if (update_callbacks_.send_heartbeat) {
              update_callbacks_.send_heartbeat(*this, cursor);
            }
            timeline.active_mover_deadline = cursor + 500u;
          }
        }
      }

      consumed += step;
    }

    int dispatch_result = 0;
    if (update_callbacks_.dispatch_events) {
      dispatch_result = update_callbacks_.dispatch_events(*this, cursor);
    } else {
      dispatch_result = DispatchDueEvents(objects, cursor);
    }

    if (dispatch_result == 1) {

      if (consumed < capped_remaining) {
        continue;
      }
      break;
    }

    if (dispatch_result == 0) {

      if (update_callbacks_.finalize_stopped) {
        update_callbacks_.finalize_stopped(*this, true);
      }
      break;
    }

    if (active_mover) {
      timeline.active_mover_deadline += capped_remaining - consumed;
    }
    update_result = dispatch_result == 2 ? UpdateResult::kAborted
                                         : UpdateResult::kEventsPending;
    break;
  }

post_loop:

  if (update_callbacks_.vehicle_post_update) {
    update_callbacks_.vehicle_post_update(*this, end_time);
  }

  {
    const auto& pos = transform_position_;
    if (std::isnan(pos[0]) || std::isnan(pos[1]) || std::isnan(pos[2])) {
      diagnostics::Log(diagnostics::LogLevel::kError, "Mover at invalid position");
    }
  }

  return update_result;
}

}
