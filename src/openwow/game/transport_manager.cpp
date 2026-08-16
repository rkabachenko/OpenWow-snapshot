
#include "openwow/game/transport_manager.h"

#include "openwow/game/movement_callbacks.h"
#include "openwow/foundation/math/quaternion_xyzw.h"
#include "openwow/foundation/math/row_major_mat4x4.h"
#include "openwow/render/api/math/render_matrix_math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>

namespace openwow::game {

namespace {

constexpr float kTransportStateCommitThreshold = 0.94999999f;
constexpr float kPackedTransportProgressScale = 1.0f / 65535.0f;
constexpr float kTransportTransitionOffsetBias = 0.5f;
constexpr std::uint16_t kMOTransportLatchedReadyDynFlag = 0x10u;
constexpr std::uint8_t kReadyGameObjectState = 1u;
constexpr float kFacingEpsilon = 0.00000023841858f;

using Quaternion = openwow::math::quaternion_xyzw::Quaternion;

[[nodiscard]] Quaternion ToQuaternion(const std::array<float, 4> &rotation) {
  return {
      rotation[0],
      rotation[1],
      rotation[2],
      rotation[3],
  };
}

[[nodiscard]] std::array<float, 4> ToArray(const Quaternion &rotation) {
  return {
      rotation.x,
      rotation.y,
      rotation.z,
      rotation.w,
  };
}

[[nodiscard]] Quaternion NormalizeQuaternion(Quaternion rotation) {
  const float length_sq = rotation.x * rotation.x + rotation.y * rotation.y +
                          rotation.z * rotation.z + rotation.w * rotation.w;
  if (length_sq <= kFacingEpsilon) {
    return {};
  }

  const float inverse_length = 1.0f / std::sqrt(length_sq);
  rotation.x *= inverse_length;
  rotation.y *= inverse_length;
  rotation.z *= inverse_length;
  rotation.w *= inverse_length;
  return rotation;
}

[[nodiscard]] Quaternion BuildYawQuaternion(const float facing) {
  const float half_facing = facing * 0.5f;
  return {
      0.0f,
      0.0f,
      std::sin(half_facing),
      std::cos(half_facing),
  };
}

[[nodiscard]] Quaternion SlerpQuaternion(const Quaternion& lhs,
                                        const Quaternion& rhs,
                                        const float t) {
  return openwow::math::quaternion_xyzw::QuatSlerp(lhs, rhs, t);
}

[[nodiscard]] float FacingFromQuaternion(const Quaternion &rotation) {
  const float yaw_cosine = 1.0f - (rotation.z * rotation.z + rotation.y * rotation.y) * 2.0f;
  const float yaw_sine = 2.0f * (rotation.w * rotation.z + rotation.y * rotation.x);

  if (std::fabs(yaw_cosine) >= kFacingEpsilon) {
    if (std::fabs(yaw_sine) >= kFacingEpsilon) {
      return std::atan2(yaw_sine, yaw_cosine);
    }

    return yaw_cosine > 0.0f ? 0.0f : 3.1415927f;
  }

  return yaw_sine >= 0.0f ? 1.5707963f : 4.7123890f;
}

}

Transport::Transport(ObjectGuid guid, std::uint32_t entry, std::uint32_t displayId,
                     const TransportPathInfo &path)
    : guid_(guid) {
  Rebind(entry, displayId, path);
}

void Transport::Rebind(const std::uint32_t entry, const std::uint32_t displayId,
                       const TransportPathInfo &path) {
  entry_ = entry;
  displayId_ = displayId;
  path_ = path;
  pathTimerMs_ = 0;
  game_object_animation_clock_ = {};
  mo_transport_animation_clock_ = {};

  if (path_.totalCycleTime == 0 && !path_.nodes.empty()) {
    const auto &last = path_.nodes.back();
    path_.totalCycleTime = last.arrivalTime + last.delay;
  }

  InvalidateCache();
}

void Transport::RefreshLivePosePath(const TransportPathInfo &path) {

  path_ = path;
  InvalidateCache();
}

void Transport::ResetHandlerCommittedState() {
  game_object_animation_clock_.committed_state = -1;
}

std::uint32_t Transport::GetPathNodeCount() const {
  return static_cast<std::uint32_t>(path_.nodes.size());
}

void Transport::SetPathTimer(std::uint32_t timerMs) {
  if (timerMs != pathTimerMs_) {
    pathTimerMs_ = timerMs;
    InvalidateCache();
  }
}

std::uint32_t
Transport::GameObjectAnimationStateClock::StateDuration(const std::uint8_t state) const {
  if (cycle_duration_ms == 0) {
    return 0;
  }

  if (stopped_duration_ms == 0) {
    return cycle_duration_ms;
  }

  return state == 0 ? stopped_duration_ms : cycle_duration_ms - stopped_duration_ms;
}

void Transport::GameObjectAnimationStateClock::Seed(const std::uint32_t absolute_tick,
                                                    const std::uint8_t current_state,
                                                    const std::uint16_t packed_progress) {
  absolute_tick_ms = absolute_tick;
  active_state = current_state;
  committed_state = static_cast<std::int32_t>(current_state);
  transition_offset_ms = 0;

  const float seeded_progress = static_cast<float>(packed_progress) * kPackedTransportProgressScale;
  const float seeded_time = static_cast<float>(StateDuration(current_state)) * seeded_progress;
  const auto elapsed = static_cast<std::uint32_t>(seeded_time);
  anchor_tick_ms = absolute_tick_ms - elapsed;
  initialized = true;
}

std::uint32_t
Transport::GameObjectAnimationStateClock::Resolve(const std::uint32_t absolute_tick,
                                                  const std::uint8_t current_state) const {
  if (!initialized || cycle_duration_ms == 0) {
    return 0;
  }

  if (committed_state < 0) {
    return 0;
  }

  if (stopped_duration_ms == 0) {
    return cycle_duration_ms == 0 ? 0 : absolute_tick % cycle_duration_ms;
  }

  const auto elapsed = absolute_tick - anchor_tick_ms;
  std::uint32_t resolved = 0;

  if (current_state == static_cast<std::uint8_t>(committed_state)) {
    resolved = transition_offset_ms + elapsed;
  } else if (elapsed < transition_offset_ms) {
    resolved = transition_offset_ms - elapsed;
  }

  if (committed_state != 0) {
    resolved += stopped_duration_ms;
    if (resolved >= cycle_duration_ms) {
      return 0;
    }
    return resolved;
  }

  if (resolved > stopped_duration_ms) {
    return stopped_duration_ms;
  }

  return resolved;
}

float Transport::GameObjectAnimationStateClock::ComputeProgress(
    const std::uint32_t absolute_tick, const std::uint8_t current_state) const {
  const auto committed_state_value =
      committed_state < 0 ? current_state : static_cast<std::uint8_t>(committed_state);
  const auto state_duration = StateDuration(committed_state_value);
  if (state_duration == 0) {
    return 0.0f;
  }

  std::uint32_t resolved = Resolve(absolute_tick, current_state);
  if (committed_state_value != 0) {
    if (resolved == 0) {
      resolved = cycle_duration_ms;
    }
    resolved -= stopped_duration_ms;
  }

  if (current_state == committed_state_value) {
    return static_cast<float>(resolved) / static_cast<float>(state_duration);
  }

  return static_cast<float>(state_duration - resolved) / static_cast<float>(state_duration);
}

void Transport::GameObjectAnimationStateClock::RequestStateChange(
    const std::uint32_t absolute_tick, const std::uint8_t previous_state,
    const std::uint8_t current_state) {
  if (!initialized) {
    return;
  }

  active_state = current_state;
  absolute_tick_ms = absolute_tick;
  if (committed_state == static_cast<std::int32_t>(current_state) || stopped_duration_ms == 0 ||
      cycle_duration_ms == 0) {
    return;
  }

  const float progress = ComputeProgress(absolute_tick_ms, previous_state);
  if (progress >= kTransportStateCommitThreshold) {
    anchor_tick_ms = absolute_tick_ms;
    committed_state = static_cast<std::int32_t>(current_state);
    transition_offset_ms = 0;
    return;
  }

  const float transition_time =
      progress * static_cast<float>(StateDuration(previous_state)) - kTransportTransitionOffsetBias;
  transition_offset_ms = transition_time > 0.0f ? static_cast<std::uint32_t>(transition_time) : 0u;
  anchor_tick_ms = absolute_tick_ms;
}

void Transport::GameObjectAnimationStateClock::Advance(const std::uint32_t absolute_tick) {
  if (!initialized) {
    return;
  }

  absolute_tick_ms = absolute_tick;

  if (committed_state == static_cast<std::int32_t>(active_state)) {

    if (transition_offset_ms != 0) {
      const auto resolved = Resolve(absolute_tick_ms, active_state);
      const bool at_boundary =
          (committed_state == 0 && resolved >= stopped_duration_ms) ||
          (committed_state != 0 && resolved == 0);
      if (at_boundary) {
        transition_offset_ms = 0;
      }
    }
    return;
  }

  if (ComputeProgress(absolute_tick_ms, active_state) < kTransportStateCommitThreshold) {
    return;
  }

  anchor_tick_ms = absolute_tick_ms;
  committed_state = static_cast<std::int32_t>(active_state);
  transition_offset_ms = 0;
}

bool Transport::SupportsGameObjectAnimationClock() const {
  return !path_.nodes.empty() && path_.nodes.front().delay > 0 &&
         path_.totalCycleTime > path_.nodes.front().delay;
}

std::uint32_t Transport::WrapTimer(const std::uint32_t timerMs) const {
  if (path_.totalCycleTime == 0) {
    return timerMs;
  }

  return timerMs % path_.totalCycleTime;
}

std::uint32_t Transport::ResolveCurrentSegmentEnd(const std::uint32_t timerMs) const {
  if (path_.nodes.empty() || path_.totalCycleTime == 0) {
    return 0;
  }

  const auto wrapped_timer = WrapTimer(timerMs);
  const auto [segment_index, fraction] = FindSegment(wrapped_timer);
  const auto &node = path_.nodes[segment_index];
  const auto node_end = WrapTimer(node.arrivalTime + node.delay);
  if (fraction == 0.0f && node.delay > 0 && wrapped_timer >= node.arrivalTime &&
      wrapped_timer < node.arrivalTime + node.delay) {
    return node_end;
  }

  const auto next_index = (segment_index + 1) % path_.nodes.size();
  const auto next_arrival =
      next_index == 0 ? path_.totalCycleTime : path_.nodes[next_index].arrivalTime;
  return WrapTimer(next_arrival);
}

void Transport::SeedMOTransportAnimationClock(const std::uint32_t absolute_tick_ms,
                                              const std::uint16_t packed_progress) {
  auto &clock = mo_transport_animation_clock_;
  clock.cycle_duration_ms = path_.totalCycleTime;
  clock.absolute_tick_ms = absolute_tick_ms;
  clock.ready_state = false;
  clock.latched_to_segment_end = false;

  if (clock.cycle_duration_ms == 0) {
    clock.phase_offset_ms = 0;
    clock.latched_segment_end_ms = 0;
    clock.initialized = false;
    return;
  }

  const auto seeded_path_time = static_cast<std::uint32_t>(
      static_cast<float>(clock.cycle_duration_ms) *
      (static_cast<float>(packed_progress) * kPackedTransportProgressScale));
  const auto wrapped_tick = absolute_tick_ms % clock.cycle_duration_ms;
  if (seeded_path_time <= wrapped_tick) {
    clock.phase_offset_ms = seeded_path_time + clock.cycle_duration_ms - wrapped_tick;
  } else {
    clock.phase_offset_ms = seeded_path_time - wrapped_tick;
  }

  clock.latched_segment_end_ms = 0;
  clock.initialized = true;
}

void Transport::AlignMOTransportClockToPathTime(const std::uint32_t absolute_tick_ms,
                                                const std::uint32_t path_time_ms) {
  auto &clock = mo_transport_animation_clock_;
  clock.absolute_tick_ms = absolute_tick_ms;
  if (clock.cycle_duration_ms == 0) {
    return;
  }

  const auto wrapped_tick = absolute_tick_ms % clock.cycle_duration_ms;
  const auto wrapped_path_time = path_time_ms % clock.cycle_duration_ms;
  if (wrapped_path_time <= wrapped_tick) {
    clock.phase_offset_ms = wrapped_path_time + clock.cycle_duration_ms - wrapped_tick;
  } else {
    clock.phase_offset_ms = wrapped_path_time - wrapped_tick;
  }
}

void Transport::ApplyMOTransportReadyState(const std::uint32_t absolute_tick_ms,
                                           const bool ready_state) {
  auto &clock = mo_transport_animation_clock_;
  clock.absolute_tick_ms = absolute_tick_ms;
  if (!clock.initialized || clock.cycle_duration_ms == 0 || ready_state == clock.ready_state) {
    return;
  }

  const auto lookup_timer =
      clock.latched_to_segment_end
          ? WrapTimer(clock.latched_segment_end_ms + clock.cycle_duration_ms - 1u)
          : ResolveMOTransportPathTime(absolute_tick_ms);
  const auto current_segment_end = ResolveCurrentSegmentEnd(lookup_timer);
  if (ready_state) {
    clock.latched_segment_end_ms = current_segment_end;
  } else if (clock.latched_to_segment_end) {
    AlignMOTransportClockToPathTime(absolute_tick_ms, current_segment_end);
  }

  clock.ready_state = ready_state;
  clock.latched_to_segment_end = false;
}

void Transport::LatchMOTransportReadySegment(const std::uint32_t absolute_tick_ms) {
  auto &clock = mo_transport_animation_clock_;
  clock.absolute_tick_ms = absolute_tick_ms;
  if (!clock.initialized || clock.cycle_duration_ms == 0 || clock.latched_to_segment_end) {
    return;
  }

  const auto lookup_timer =
      WrapTimer(ResolveMOTransportPathTime(absolute_tick_ms) + clock.cycle_duration_ms - 1u);
  clock.latched_segment_end_ms = ResolveCurrentSegmentEnd(lookup_timer);
  clock.ready_state = true;
  clock.latched_to_segment_end = true;
}

void Transport::AdvanceMOTransportAnimationClock(const std::uint32_t absolute_tick_ms,
                                                 const std::uint32_t frame_delta_ms) {
  auto &clock = mo_transport_animation_clock_;
  clock.absolute_tick_ms = absolute_tick_ms;
  if (!clock.initialized || clock.cycle_duration_ms == 0 || !clock.ready_state ||
      clock.latched_to_segment_end) {
    return;
  }

  const auto current_path_time = ResolveMOTransportPathTime(absolute_tick_ms);
  if (DidMOTransportClockCrossBoundary(clock.latched_segment_end_ms,
                                       WrapTimer(clock.latched_segment_end_ms + frame_delta_ms),
                                       current_path_time)) {
    clock.latched_to_segment_end = true;
  }
}

std::uint32_t Transport::ResolveMOTransportPathTime(const std::uint32_t absolute_tick_ms) const {
  const auto &clock = mo_transport_animation_clock_;
  if (!clock.initialized || clock.cycle_duration_ms == 0) {
    return 0;
  }

  if (clock.latched_to_segment_end) {
    return clock.latched_segment_end_ms;
  }

  return (absolute_tick_ms + clock.phase_offset_ms) % clock.cycle_duration_ms;
}

bool Transport::DidMOTransportClockCrossBoundary(const std::uint32_t boundary_start,
                                                 const std::uint32_t boundary_end,
                                                 const std::uint32_t current_value) {
  if (boundary_end == boundary_start) {
    return boundary_end == current_value;
  }

  if (boundary_end <= boundary_start) {
    if (current_value > boundary_end && current_value <= boundary_start) {
      return false;
    }
  } else if (current_value <= boundary_start || current_value > boundary_end) {
    return false;
  }

  return true;
}

TransportVec3 Transport::InterpolatePosition() const {
  return InterpolatePosition(pathTimerMs_);
}

TransportVec3 Transport::InterpolatePosition(std::uint32_t timerMs) const {
  if (path_.nodes.empty()) {
    return {0.0f, 0.0f, 0.0f};
  }

  if (timerMs == cachedTimer_) {
    return cachedPos_;
  }

  TransportVec3 result;
  if (path_.nodes.size() == 1) {
    const auto &node = path_.nodes[0];
    result = {node.x, node.y, node.z};
  } else {
    std::uint32_t t = timerMs;
    if (path_.totalCycleTime > 0) {
      t = t % path_.totalCycleTime;
    }

    auto [segIdx, fraction] = FindSegment(t);
    const auto &nodeA = path_.nodes[segIdx];
    const std::size_t nextIdx = (segIdx + 1) % path_.nodes.size();
    const auto &nodeB = path_.nodes[nextIdx];
    result.x = nodeA.x + (nodeB.x - nodeA.x) * fraction;
    result.y = nodeA.y + (nodeB.y - nodeA.y) * fraction;
    result.z = nodeA.z + (nodeB.z - nodeA.z) * fraction;
  }

  if (path_.uses_local_animation) {
    auto world_transform = openwow::render::BuildRotationMatrix4x4Quaternion(
        openwow::render::RenderVec4{
            path_.base_pose.rotation[0], path_.base_pose.rotation[1],
            path_.base_pose.rotation[2], path_.base_pose.rotation[3]});
    world_transform[12] = path_.base_pose.position[0];
    world_transform[13] = path_.base_pose.position[1];
    world_transform[14] = path_.base_pose.position[2];

    const float local_point[3] = {result.x, result.y, result.z};
    float world_point[3]{};
    openwow::math::row_major_mat4x4::TransformPointByRowMajorAffine4x4Unbuffered(
        world_point, local_point, world_transform.data());
    result = {
        world_point[0],
        world_point[1],
        world_point[2],
    };
  }

  cachedPos_ = result;
  cachedTimer_ = timerMs;

  return result;
}

std::uint32_t Transport::GetCurrentMapId() const {
  return GetMapIdAtTimer(pathTimerMs_);
}

float Transport::GetFacing() const {
  const auto [x, y, z, w] = GetWorldRotationQuaternion();
  return FacingFromQuaternion({x, y, z, w});
}

std::array<float, 4> Transport::GetWorldRotationQuaternion() const {
  if (!path_.rotation_keyframes.empty()) {
    Quaternion local_rotation = {
        path_.rotation_keyframes.front().x,
        path_.rotation_keyframes.front().y,
        path_.rotation_keyframes.front().z,
        path_.rotation_keyframes.front().w,
    };

    if (path_.rotation_keyframes.size() > 1 && path_.totalCycleTime != 0) {
      const auto wrapped_timer = WrapTimer(pathTimerMs_);
      std::size_t current_index = path_.rotation_keyframes.size() - 1;
      std::size_t next_index = 0;

      for (std::size_t index = 0; index < path_.rotation_keyframes.size(); ++index) {
        const std::size_t candidate_next = (index + 1) % path_.rotation_keyframes.size();
        const auto current_time = path_.rotation_keyframes[index].timeIndex;
        auto next_time = path_.rotation_keyframes[candidate_next].timeIndex;
        if (candidate_next == 0 && next_time == 0 && path_.totalCycleTime != 0) {
          next_time = path_.totalCycleTime;
        }

        if (wrapped_timer >= current_time && wrapped_timer < next_time) {
          current_index = index;
          next_index = candidate_next;
          break;
        }
      }

      const auto &current = path_.rotation_keyframes[current_index];
      const auto &next = path_.rotation_keyframes[next_index];
      auto next_time = next.timeIndex;
      if (next_index == 0 && next_time == 0 && path_.totalCycleTime != 0) {
        next_time = path_.totalCycleTime;
      }

      const auto current_time = current.timeIndex;
      const auto duration = next_time > current_time ? next_time - current_time : 0u;
      const float factor = duration != 0u
                               ? static_cast<float>(wrapped_timer - current_time) /
                                     static_cast<float>(duration)
                               : 0.0f;
      local_rotation = SlerpQuaternion(
          {current.x, current.y, current.z, current.w},
          {next.x, next.y, next.z, next.w},
          std::clamp(factor, 0.0f, 1.0f));
    }

    if (path_.uses_local_animation) {
      local_rotation =
          openwow::math::quaternion_xyzw::Multiply(local_rotation, ToQuaternion(path_.base_pose.rotation));
    }

    return ToArray(NormalizeQuaternion(local_rotation));
  }

  if (path_.uses_local_animation) {
    return path_.base_pose.rotation;
  }

  if (path_.nodes.size() < 2) {
    return ToArray(BuildYawQuaternion(0.0f));
  }

  auto [segIdx, fraction] =
      FindSegment(path_.totalCycleTime > 0 ? pathTimerMs_ % path_.totalCycleTime : pathTimerMs_);
  (void)fraction;
  const auto &nodeA = path_.nodes[segIdx];
  const std::size_t nextIdx = (segIdx + 1) % path_.nodes.size();
  const auto &nodeB = path_.nodes[nextIdx];
  const float dx = nodeB.x - nodeA.x;
  const float dy = nodeB.y - nodeA.y;
  const float facing = (dx == 0.0f && dy == 0.0f) ? 0.0f : std::atan2(dy, dx);
  return ToArray(BuildYawQuaternion(facing));
}

void Transport::BuildWorldTransform(float *out_matrix_4x4) const {
  if (out_matrix_4x4 == nullptr) {
    return;
  }

  const auto position = InterpolatePosition();
  const auto rotation = GetWorldRotationQuaternion();
  auto world_transform = openwow::render::BuildRotationMatrix4x4Quaternion(
      openwow::render::RenderVec4{rotation[0], rotation[1], rotation[2], rotation[3]});
  world_transform[12] = position.x;
  world_transform[13] = position.y;
  world_transform[14] = position.z;
  std::copy(world_transform.begin(), world_transform.end(), out_matrix_4x4);
}

TransportMoveState Transport::GetMoveState() const {
  if (path_.nodes.empty() || path_.totalCycleTime == 0) {
    return TransportMoveState::Stopped;
  }

  std::uint32_t t = pathTimerMs_ % path_.totalCycleTime;

  for (const auto &node : path_.nodes) {
    if (node.delay > 0 && t >= node.arrivalTime && t < node.arrivalTime + node.delay) {
      return TransportMoveState::Stopped;
    }
  }

  return TransportMoveState::Moving;
}

std::size_t Transport::GetCurrentNodeIndex() const {
  if (path_.nodes.empty())
    return 0;
  auto [segIdx, fraction] =
      FindSegment(path_.totalCycleTime > 0 ? pathTimerMs_ % path_.totalCycleTime : pathTimerMs_);
  return segIdx;
}

std::uint32_t Transport::GetMapIdAtTimer(std::uint32_t timerMs) const {
  if (path_.nodes.empty())
    return 0;
  if (path_.nodes.size() == 1)
    return path_.nodes[0].mapId;

  std::uint32_t t = timerMs;
  if (path_.totalCycleTime > 0) {
    t = t % path_.totalCycleTime;
  }

  std::uint32_t mapId = path_.nodes[0].mapId;
  for (const auto &node : path_.nodes) {
    if (node.arrivalTime <= t) {
      mapId = node.mapId;
    } else {
      break;
    }
  }

  return mapId;
}

void Transport::AddPassenger(ObjectGuid playerGuid, const float offsetX, const float offsetY,
                             const float offsetZ, const float offsetO) {
  const auto raw_guid = playerGuid.GetRawValue();
  passengers_.insert(raw_guid);
  passenger_attachments_[raw_guid] = {
      .offset_x = offsetX,
      .offset_y = offsetY,
      .offset_z = offsetZ,
      .offset_o = offsetO,
  };
}

void Transport::RemovePassenger(ObjectGuid playerGuid) {
  const auto raw_guid = playerGuid.GetRawValue();
  passengers_.erase(raw_guid);
  passenger_attachments_.erase(raw_guid);
}

bool Transport::HasPassenger(ObjectGuid playerGuid) const {
  return passengers_.count(playerGuid.GetRawValue()) > 0;
}

bool Transport::MatchesPassengerAttachment(ObjectGuid playerGuid, const float offsetX,
                                           const float offsetY, const float offsetZ) const {
  const auto it = passenger_attachments_.find(playerGuid.GetRawValue());
  if (it == passenger_attachments_.end()) {
    return false;
  }

  const auto &attachment = it->second;
  return attachment.offset_x == offsetX && attachment.offset_y == offsetY &&
         attachment.offset_z == offsetZ;
}

std::size_t Transport::GetPassengerCount() const {
  return passengers_.size();
}

const std::unordered_set<std::uint64_t> &Transport::GetPassengers() const {
  return passengers_;
}

TransportWorldPos Transport::GetWorldPosition(float offsetX, float offsetY, float offsetZ,
                                              float offsetO) const {
  float world_transform[16]{};
  BuildWorldTransform(world_transform);
  const float local_offset[3] = {offsetX, offsetY, offsetZ};
  float world_offset[3]{};
  openwow::math::row_major_mat4x4::TransformPointByRowMajorAffine4x4Unbuffered(
      world_offset, local_offset, world_transform);

  const float facing = GetFacing();
  TransportWorldPos result;
  result.x = world_offset[0];
  result.y = world_offset[1];
  result.z = world_offset[2];
  result.o = Movement_NormalizeFacing0ToTau(facing + offsetO);
  result.mapId = GetCurrentMapId();

  return result;
}

void Transport::AdvanceTimer(const std::uint32_t elapsed_ms) {
  if (mo_transport_animation_clock_.initialized) {
    AdvanceMOTransportAnimationClock(
        mo_transport_animation_clock_.absolute_tick_ms + elapsed_ms,
        elapsed_ms);
    pathTimerMs_ = ResolveMOTransportPathTime(mo_transport_animation_clock_.absolute_tick_ms);
  } else if (game_object_animation_clock_.initialized) {
    game_object_animation_clock_.Advance(
        game_object_animation_clock_.absolute_tick_ms + elapsed_ms);
    pathTimerMs_ = game_object_animation_clock_.Resolve(
        game_object_animation_clock_.absolute_tick_ms, game_object_animation_clock_.active_state);
  } else {
    pathTimerMs_ += elapsed_ms;

    if (path_.totalCycleTime > 0) {
      pathTimerMs_ = pathTimerMs_ % path_.totalCycleTime;
    }
  }

  InvalidateCache();
}

void Transport::SetLivePoseMotion(const TransportVec3 &previous_position,
                                  const TransportVec3 &current_position,
                                  const std::uint32_t elapsed_ms) {
  pending_live_pose_motion_ = LivePoseMotion{previous_position, current_position, elapsed_ms};
}

TransportFrameAdvance Transport::AdvanceFrame(const float dt) {
  TransportFrameAdvance result;

  if (path_.live_pose) {
    if (!pending_live_pose_motion_.has_value()) {
      result.collision_start = InterpolatePosition();
      result.collision_end = result.collision_start;
      return result;
    }

    const LivePoseMotion motion = *pending_live_pose_motion_;
    pending_live_pose_motion_.reset();
    result.collision_start = motion.previous_position;
    result.collision_end = motion.current_position;

    result.collision_elapsed_ms = std::min(motion.elapsed_ms, 250u);
    return result;
  }

  if (path_.nodes.empty() || path_.totalCycleTime == 0 ||
      !std::isfinite(dt) || dt <= 0.0f) {
    result.collision_start = InterpolatePosition();
    result.collision_end = result.collision_start;
    return result;
  }

  const double elapsed_ms_double = static_cast<double>(dt) * 1000.0;
  const std::uint32_t elapsed_ms = static_cast<std::uint32_t>(
      std::min(elapsed_ms_double,
               static_cast<double>(std::numeric_limits<std::uint32_t>::max())));
  result.collision_elapsed_ms = std::min(elapsed_ms, 250u);

  const std::uint32_t skipped_ms = elapsed_ms - result.collision_elapsed_ms;
  if (skipped_ms != 0u) {
    AdvanceTimer(skipped_ms);
  }
  result.collision_start = InterpolatePosition();
  if (result.collision_elapsed_ms != 0u) {
    AdvanceTimer(result.collision_elapsed_ms);
  }
  result.collision_end = InterpolatePosition();

  const std::uint32_t wrapped_timer =
      path_.totalCycleTime > 0 ? pathTimerMs_ % path_.totalCycleTime : pathTimerMs_;
  const auto [segment_index, fraction] = FindSegment(wrapped_timer);
  (void)fraction;
  if (segment_index < path_.nodes.size()) {
    const std::uint32_t sequence_id = path_.nodes[segment_index].actionFlag;
    if (!lastEmittedSequenceId_.has_value() || *lastEmittedSequenceId_ != sequence_id) {
      lastEmittedSequenceId_ = sequence_id;
      result.sequence_change = sequence_id;
    }
  }

  return result;
}

void Transport::SyncFromServer(float , float , float , float ,
                               std::uint32_t , std::uint32_t pathTimerMs) {

  pathTimerMs_ = pathTimerMs;
  InvalidateCache();
}

void Transport::SyncGameObjectAnimationState(const std::uint32_t absolute_tick_ms,
                                             const std::uint8_t previous_state,
                                             const std::uint8_t current_state,
                                             const std::uint16_t packed_progress) {
  if (!SupportsGameObjectAnimationClock()) {
    return;
  }

  game_object_animation_clock_.cycle_duration_ms = path_.totalCycleTime;
  game_object_animation_clock_.stopped_duration_ms = path_.nodes.front().delay;

  if (!game_object_animation_clock_.initialized) {
    game_object_animation_clock_.Seed(absolute_tick_ms, current_state, packed_progress);
  } else if (previous_state != current_state) {
    game_object_animation_clock_.RequestStateChange(absolute_tick_ms, previous_state,
                                                    current_state);
  } else {
    game_object_animation_clock_.active_state = current_state;
    game_object_animation_clock_.absolute_tick_ms = absolute_tick_ms;
  }

  game_object_animation_clock_.Advance(absolute_tick_ms);
  pathTimerMs_ = game_object_animation_clock_.Resolve(game_object_animation_clock_.absolute_tick_ms,
                                                      game_object_animation_clock_.active_state);
  InvalidateCache();
}

void Transport::SyncMOTransportAnimationState(const std::uint32_t absolute_tick_ms,
                                              const std::uint8_t current_state,
                                              const std::uint16_t packed_progress,
                                              const std::uint16_t dynamic_flags) {
  if (path_.nodes.empty() || path_.totalCycleTime == 0) {
    return;
  }

  SeedMOTransportAnimationClock(absolute_tick_ms, packed_progress);
  if ((dynamic_flags & kMOTransportLatchedReadyDynFlag) != 0u) {
    LatchMOTransportReadySegment(absolute_tick_ms);
  } else {
    ApplyMOTransportReadyState(absolute_tick_ms, current_state == kReadyGameObjectState);
  }

  pathTimerMs_ = ResolveMOTransportPathTime(mo_transport_animation_clock_.absolute_tick_ms);
  InvalidateCache();
}

void Transport::ReplayMOTransportAnimationStateChange(const std::uint32_t absolute_tick_ms,
                                                      const std::uint8_t previous_state,
                                                      const std::uint8_t current_state) {
  if (!mo_transport_animation_clock_.initialized ||
      mo_transport_animation_clock_.cycle_duration_ms == 0) {
    return;
  }

  const bool current_ready = current_state == kReadyGameObjectState;
  if (previous_state == current_state) {
    ApplyMOTransportReadyState(absolute_tick_ms, !current_ready);
  }
  ApplyMOTransportReadyState(absolute_tick_ms, current_ready);
  pathTimerMs_ = ResolveMOTransportPathTime(mo_transport_animation_clock_.absolute_tick_ms);
  InvalidateCache();
}

std::pair<std::size_t, float> Transport::FindSegment(std::uint32_t t) const {
  if (path_.nodes.empty())
    return {0, 0.0f};
  if (path_.nodes.size() == 1)
    return {0, 0.0f};

  for (std::size_t i = 0; i < path_.nodes.size(); ++i) {
    const auto &node = path_.nodes[i];
    std::uint32_t nodeEnd = node.arrivalTime + node.delay;

    if (t >= node.arrivalTime && t < nodeEnd) {
      return {i, 0.0f};
    }

    std::size_t nextIdx = (i + 1) % path_.nodes.size();
    std::uint32_t nextArrival;

    if (nextIdx == 0) {

      nextArrival = path_.totalCycleTime;
    } else {
      nextArrival = path_.nodes[nextIdx].arrivalTime;
    }

    if (t >= nodeEnd && t < nextArrival) {
      std::uint32_t travelTime = nextArrival - nodeEnd;
      if (travelTime == 0)
        return {i, 0.0f};
      float fraction = static_cast<float>(t - nodeEnd) / static_cast<float>(travelTime);
      return {i, fraction};
    }
  }

  return {path_.nodes.size() - 1, 0.0f};
}

void Transport::InvalidateCache() const {
  cachedTimer_ = UINT32_MAX;
}

void TransportManager::OnTransportCreate(ObjectGuid guid, std::uint32_t entry,
                                         std::uint32_t displayId, const TransportPathInfo &path) {
  const auto [it, inserted] =
      transports_.try_emplace(guid.GetRawValue(), guid, entry, displayId, path);
  if (!inserted) {

    Transport &existing = it->second;
    if (existing.GetEntry() == entry && existing.GetDisplayId() == displayId &&
        existing.GetPath().pathId == path.pathId) {
      return;
    }
    existing.Rebind(entry, displayId, path);
  }
}

void TransportManager::OnTransportDestroy(ObjectGuid guid) {
  transports_.erase(guid.GetRawValue());
}

void TransportManager::OnTransportUpdate(ObjectGuid guid, float x, float y, float z, float facing,
                                         std::uint32_t mapId, std::uint32_t pathTimerMs) {
  auto it = transports_.find(guid.GetRawValue());
  if (it == transports_.end())
    return;
  it->second.SyncFromServer(x, y, z, facing, mapId, pathTimerMs);
}

const Transport *TransportManager::GetTransport(ObjectGuid guid) const {
  auto it = transports_.find(guid.GetRawValue());
  if (it == transports_.end())
    return nullptr;
  return &it->second;
}

Transport *TransportManager::GetTransportMutable(ObjectGuid guid) {
  auto it = transports_.find(guid.GetRawValue());
  if (it == transports_.end())
    return nullptr;
  return &it->second;
}

std::vector<const Transport *> TransportManager::GetAllTransports() const {
  std::vector<const Transport *> result;
  result.reserve(transports_.size());
  for (const auto &[_, t] : transports_) {
    result.push_back(&t);
  }
  return result;
}

std::vector<const Transport *> TransportManager::GetTransportsOnMap(std::uint32_t mapId) const {
  std::vector<const Transport *> result;
  for (const auto &[_, t] : transports_) {
    if (t.GetCurrentMapId() == mapId) {
      result.push_back(&t);
    }
  }
  return result;
}

std::size_t TransportManager::GetTransportCount() const {
  return transports_.size();
}

const Transport *TransportManager::FindTransportForPassenger(ObjectGuid playerGuid) const {
  for (const auto &[_, t] : transports_) {
    if (t.HasPassenger(playerGuid)) {
      return &t;
    }
  }
  return nullptr;
}

TransportManagerUpdate TransportManager::Update(const float dt) {
  constexpr float kTransportMotionEpsilon = 0.0000009536743f;
  TransportManagerUpdate result;
  for (auto &[_, t] : transports_) {
    const TransportFrameAdvance frame = t.AdvanceFrame(dt);
    if (frame.sequence_change.has_value()) {
      result.sequence_changes.push_back({t.GetGuid(), *frame.sequence_change});
    }
    if (t.GetPassengerCount() == 0 ||
        frame.collision_elapsed_ms == 0) {
      continue;
    }

    const float dx = frame.collision_end.x - frame.collision_start.x;
    const float dy = frame.collision_end.y - frame.collision_start.y;
    const float dz = frame.collision_end.z - frame.collision_start.z;
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!std::isfinite(distance) || distance < kTransportMotionEpsilon) {
      continue;
    }

    TransportMotionStep step;
    step.guid = t.GetGuid();
    step.elapsed_ms = frame.collision_elapsed_ms;
    step.direction = {dx / distance, dy / distance, dz / distance};

    step.distance = distance;
    t.BuildWorldTransform(step.world_transform.data());
    result.motion_steps.push_back(step);
  }
  return result;
}

void TransportManager::Clear() {
  transports_.clear();
}

}
