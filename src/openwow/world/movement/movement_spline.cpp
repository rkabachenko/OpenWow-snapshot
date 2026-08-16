
#include "openwow/world/movement/movement_spline.h"

#include "openwow/foundation/math/vec3_exact_compare.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace openwow::world {

namespace {

constexpr float kLengthEpsilon = 1.0e-6f;
constexpr float kRetailPointMergeDistanceSquared = 0.00077160494f;

float Vec3Length(const game::Vec3& value) {
  return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

game::Vec3 BuildAuthoritativeSplineStart(const game::MovementUpdate& update) {
  if (update.movement.IsOnTransport() && !update.movement.transport.guid.IsEmpty()) {
    return {update.movement.transport.offset_x, update.movement.transport.offset_y,
            update.movement.transport.offset_z};
  }

  return {update.movement.x, update.movement.y, update.movement.z};
}

float BuildAuthoritativeSplineFacing(const game::MovementUpdate& update) {
  if (update.movement.IsOnTransport() && !update.movement.transport.guid.IsEmpty()) {
    return update.movement.transport.offset_o;
  }

  return update.movement.orientation;
}

bool PointsEqual(const game::Vec3& left, const game::Vec3& right) {
  return openwow::math::vec3::AllComponentsEqual(&left.x, &right.x);
}

float DistanceSquared(const game::Vec3& left, const game::Vec3& right) {
  const auto delta = left - right;
  return delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
}

float DistanceSquaredXY(const game::Vec3& left, const game::Vec3& right) {
  const float x = left.x - right.x;
  const float y = left.y - right.y;
  return x * x + y * y;
}

std::vector<game::Vec3> BuildClippedPath(
    const game::MonsterMoveInfo& info,
    const std::optional<game::Vec3>& current_position);

std::vector<game::Vec3> BuildClippedPath(
    const game::MonsterMoveInfo& info,
    const std::optional<game::Vec3>& current_position) {
  std::vector<game::Vec3> wire_path;
  wire_path.reserve(info.waypoints.size() + 1u);
  wire_path.push_back(info.position);
  wire_path.insert(wire_path.end(), info.waypoints.begin(), info.waypoints.end());

  if (!current_position || wire_path.size() < 2u) {
    return wire_path;
  }

  const std::size_t segment_count = wire_path.size() - 1u;
  std::size_t behind_count = 0u;
  std::size_t ahead_count = 0u;
  std::optional<std::size_t> straddling_segment;

  for (std::size_t segment = 0; segment < segment_count; ++segment) {
    const auto edge = wire_path[segment + 1u] - wire_path[segment];
    const auto from_pose_to_start = wire_path[segment] - *current_position;
    const auto from_pose_to_end = wire_path[segment + 1u] - *current_position;
    const float start_projection =
        edge.x * from_pose_to_start.x + edge.y * from_pose_to_start.y +
        edge.z * from_pose_to_start.z;
    const float end_projection =
        edge.x * from_pose_to_end.x + edge.y * from_pose_to_end.y +
        edge.z * from_pose_to_end.z;

    if (start_projection < 0.0f || end_projection < 0.0f) {
      if (start_projection > 0.0f || end_projection > 0.0f) {
        straddling_segment = segment;
        break;
      }
      ++behind_count;
    } else {
      ++ahead_count;
    }
  }

  if (ahead_count == segment_count) {
    if (DistanceSquaredXY(wire_path.front(), *current_position) < 1.0f) {
      return wire_path;
    }
    wire_path.insert(wire_path.begin(), *current_position);
    return wire_path;
  }

  if (behind_count == segment_count) {
    if (DistanceSquared(wire_path.back(), *current_position) <
        kRetailPointMergeDistanceSquared) {
      return {*current_position};
    }
    return {*current_position, wire_path.back()};
  }

  if (!straddling_segment.has_value()) {

    straddling_segment = std::min(behind_count, segment_count - 1u);
  }

  const std::size_t segment = *straddling_segment;
  const auto edge = wire_path[segment + 1u] - wire_path[segment];
  const float length_squared =
      edge.x * edge.x + edge.y * edge.y + edge.z * edge.z;
  const auto from_start = *current_position - wire_path[segment];
  const float t = length_squared > kLengthEpsilon
                      ? std::clamp((from_start.x * edge.x +
                                    from_start.y * edge.y +
                                    from_start.z * edge.z) /
                                       length_squared,
                                   0.0f, 1.0f)
                      : 0.0f;
  const auto projected = wire_path[segment] + edge * t;

  std::vector<game::Vec3> clipped;
  clipped.reserve(wire_path.size() - segment);
  if (DistanceSquared(projected, wire_path[segment + 1u]) >=
      kRetailPointMergeDistanceSquared) {
    clipped.push_back(projected);
  } else if (DistanceSquared(*current_position, wire_path[segment + 1u]) >=
             kRetailPointMergeDistanceSquared) {
    clipped.push_back(*current_position);
  }

  for (std::size_t index = segment + 1u; index < wire_path.size(); ++index) {
    if (clipped.empty() ||
        DistanceSquared(clipped.back(), wire_path[index]) >=
            kRetailPointMergeDistanceSquared) {
      clipped.push_back(wire_path[index]);
    }
  }
  if (clipped.empty()) {
    clipped.push_back(*current_position);
  }
  return clipped;
}

}

game::Vec3 CatmullRom::EvalVec3(const game::Vec3& p0, const game::Vec3& p1,
                                  const game::Vec3& p2, const game::Vec3& p3,
                                  float t) {
  return {Eval(p0.x, p1.x, p2.x, p3.x, t),
          Eval(p0.y, p1.y, p2.y, p3.y, t),
          Eval(p0.z, p1.z, p2.z, p3.z, t)};
}

game::Vec3 CatmullRom::EvalTangent(const game::Vec3& p0, const game::Vec3& p1,
                                     const game::Vec3& p2, const game::Vec3& p3,
                                     float t) {
  return {EvalDerivative(p0.x, p1.x, p2.x, p3.x, t),
          EvalDerivative(p0.y, p1.y, p2.y, p3.y, t),
          EvalDerivative(p0.z, p1.z, p2.z, p3.z, t)};
}

float CatmullRom::SegmentLength(const game::Vec3& p0, const game::Vec3& p1,
                                 const game::Vec3& p2, const game::Vec3& p3,
                                 int steps) {
  float length = 0.0f;
  game::Vec3 prev = EvalVec3(p0, p1, p2, p3, 0.0f);
  for (int i = 1; i <= steps; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(steps);
    const game::Vec3 cur = EvalVec3(p0, p1, p2, p3, t);
    length += Vec3Length(cur - prev);
    prev = cur;
  }
  return length;
}

void MoveSpline::Initialize(const game::MonsterMoveInfo& info, float start_facing,
                            std::optional<game::Vec3> current_position,
                            std::optional<float> target_facing,
                            const float run_speed) {
  spline_id_     = info.spline_id;
  spline_flags_  = info.spline_flags;
  duration_ms_   = info.duration;
  time_passed_ms_= 0;
  catmull_rom_   = info.catmull_rom;
  active_        = true;
  finished_      = false;
  start_facing_  = start_facing;
  base_rate_ = 1.0f;
  playback_speed_ = 1.0f;
  entered_cycle_ = false;
  has_coordinate_parent_binding_ =
      info.has_transport && !info.transport.IsEmpty();
  coordinate_parent_ = has_coordinate_parent_binding_
                           ? info.transport
                           : game::ObjectGuid{};
  coordinate_parent_seat_ =
      has_coordinate_parent_binding_ ? info.transport_seat : -1;

  points_ = BuildClippedPath(info, current_position);

  if (IsCyclic() && points_.size() > 2u &&
      DistanceSquared(points_[1], points_.back()) >=
          kRetailPointMergeDistanceSquared) {
    points_.push_back(points_[1]);
  }
  start_pos_ = points_.empty() ? info.position : points_.front();
  final_destination_ = points_.empty() ? info.position : points_.back();
  RebuildControlPoints();
  RebuildArcLengthCache();
  ApplyMonsterMoveSpeedCap(run_speed);

  switch (info.move_type) {
    case game::MonsterMoveType::kFacingSpot:
      facing_mode_ = SplineFacingMode::kSpot;
      facing_spot_ = info.facing_spot;
      break;
    case game::MonsterMoveType::kFacingTarget:
      facing_mode_ = SplineFacingMode::kTarget;
      facing_target_ = info.facing_target_guid;
      facing_angle_ = target_facing.value_or(start_facing);
      break;
    case game::MonsterMoveType::kFacingAngle:
      facing_mode_ = SplineFacingMode::kAngle;
      facing_angle_ = info.facing_angle;
      break;
    case game::MonsterMoveType::kStop:
      Stop();
      return;
    default:
      facing_mode_ = SplineFacingMode::kNone;
      break;
  }

  if (info.spline_flags & game::SplineFlag::kParabolic) {
    vertical_accel_ = info.vertical_acceleration;
    parabolic_start_ms_ = info.parabolic_start_time;
  } else {
    vertical_accel_ = 0.0f;
    parabolic_start_ms_ = 0;
  }

  if (info.spline_flags & game::SplineFlag::kAnimation) {
    animation_id_         = info.animation_id;
    anim_start_time_ms_   = info.anim_start_time;
  } else {
    animation_id_         = 0;
    anim_start_time_ms_   = 0;
  }

  if (info.move_type == game::MonsterMoveType::kStop) {
    Stop();
  }
}

void MoveSpline::Initialize(const game::MovementUpdate& update,
                            const std::optional<float> target_facing) {
  const auto start_position = BuildAuthoritativeSplineStart(update);
  has_coordinate_parent_binding_ =
      update.movement.IsOnTransport() &&
      !update.movement.transport.guid.IsEmpty();
  coordinate_parent_ = has_coordinate_parent_binding_
                           ? update.movement.transport.guid
                           : game::ObjectGuid{};
  coordinate_parent_seat_ = has_coordinate_parent_binding_
                                ? update.movement.transport.seat
                                : -1;

  spline_id_ = update.spline.spline_id;
  spline_flags_ = update.spline.flags;
  duration_ms_ = update.spline.duration;
  time_passed_ms_ = duration_ms_ == 0
                        ? 0
                        : std::min(update.spline.time_passed, update.spline.duration);

  catmull_rom_ = update.spline.mode == 1u;
  active_ = update.spline.active && duration_ms_ > 0 && time_passed_ms_ < duration_ms_;
  finished_ = !active_ && duration_ms_ > 0 && time_passed_ms_ >= duration_ms_;
  start_facing_ = BuildAuthoritativeSplineFacing(update);
  entered_cycle_ = (spline_flags_ & game::SplineFlag::kEnterCycle) == 0u;

  std::vector<game::Vec3> wire_controls;
  wire_controls.reserve(update.spline.point_count());
  for (std::size_t index = 0; index + 2 < update.spline.waypoints.size(); index += 3) {
    wire_controls.push_back({update.spline.waypoints[index],
                             update.spline.waypoints[index + 1],
                             update.spline.waypoints[index + 2]});
  }

  const game::Vec3 destination{update.spline.dest_x, update.spline.dest_y, update.spline.dest_z};
  final_destination_ = destination;
  if (wire_controls.size() >= 4u) {

    control_points_ = wire_controls;
    points_.assign(wire_controls.begin() + 1, wire_controls.end() - 1);
  } else {
    points_.clear();
    points_.push_back(start_position);
    if (!PointsEqual(start_position, destination)) {
      points_.push_back(destination);
    }
    RebuildControlPoints();
  }

  start_pos_ = points_.empty() ? start_position : points_.front();
  RebuildArcLengthCache();

  facing_mode_ = SplineFacingMode::kNone;
  facing_target_ = 0;
  facing_angle_ = 0.0f;
  facing_spot_ = {};
  switch (update.spline.facing_type) {
    case game::SplineFacing::kTarget:
      facing_mode_ = SplineFacingMode::kTarget;
      facing_target_ = update.spline.facing_target.GetRawValue();
      facing_angle_ = target_facing.value_or(start_facing_);
      break;
    case game::SplineFacing::kAngle:
      facing_mode_ = SplineFacingMode::kAngle;
      facing_angle_ = update.spline.facing_angle;
      break;
    case game::SplineFacing::kPoint:
      facing_mode_ = SplineFacingMode::kSpot;
      facing_spot_ = {update.spline.facing_x, update.spline.facing_y, update.spline.facing_z};
      break;
    case game::SplineFacing::kNone:
      break;
  }

  if (update.spline.flags & game::SplineFlag::kParabolic) {
    vertical_accel_ = update.spline.vertical_acceleration;
    parabolic_start_ms_ =
        update.spline.effect_start_time > 0 ? static_cast<std::uint32_t>(update.spline.effect_start_time)
                                            : 0u;
  } else {
    vertical_accel_ = 0.0f;
    parabolic_start_ms_ = 0;
  }

  animation_id_ = 0;
  anim_start_time_ms_ = 0;
  base_rate_ = update.spline.duration_mod;
  playback_speed_ = update.spline.duration_mod_next;
}

void MoveSpline::Initialize(const std::vector<game::Vec3>& points,
                              std::uint32_t duration_ms,
                              std::uint32_t spline_flags,
                              bool catmull_rom,
                              float start_facing) {
  spline_id_      = 0;
  spline_flags_   = spline_flags;
  duration_ms_    = duration_ms;
  time_passed_ms_ = 0;
  catmull_rom_    = catmull_rom;
  active_         = !points.empty() && duration_ms > 0;
  finished_       = !active_;
  points_         = points;
  start_pos_      = points.empty() ? game::Vec3{} : points.front();
  final_destination_ = points.empty() ? game::Vec3{} : points.back();
  start_facing_   = start_facing;
  facing_mode_    = SplineFacingMode::kNone;
  vertical_accel_ = 0.0f;
  parabolic_start_ms_ = 0;
  animation_id_       = 0;
  anim_start_time_ms_ = 0;
  base_rate_ = 1.0f;
  playback_speed_ = 1.0f;
  entered_cycle_ = false;
  has_coordinate_parent_binding_ = false;
  coordinate_parent_ = {};
  coordinate_parent_seat_ = -1;
  RebuildControlPoints();
  RebuildArcLengthCache();
}

void MoveSpline::Stop() {
  active_   = false;
  finished_ = true;
  duration_ms_ = 0;
  time_passed_ms_ = 0;
  base_rate_ = 1.0f;
  playback_speed_ = 1.0f;
  entered_cycle_ = false;
}

void MoveSpline::ApplyMonsterMoveSpeedCap(const float run_speed) {
  constexpr float kMonsterMoveSpeedCapRunMultiplier = 4.0f;
  constexpr float kMonsterMoveGroundSpeedCap = 28.0f;
  constexpr float kMonsterMoveSmoothPathSpeedCap = 50.0f;
  constexpr float kMonsterMoveMinimumPathLength = 0.16666667f;
  constexpr float kMonsterMoveMinimumSpeed = 9.536743e-07f;

  if (points_.size() < 2u) {
    Stop();
    return;
  }
  float polyline_length = 0.0f;
  for (std::size_t index = 0; index + 1u < points_.size(); ++index) {
    polyline_length += Vec3Length(points_[index + 1u] - points_[index]);
  }
  if (polyline_length <= kMonsterMoveMinimumPathLength) {
    Stop();
    return;
  }

  float speed_cap = run_speed * kMonsterMoveSpeedCapRunMultiplier;
  if (speed_cap > kMonsterMoveGroundSpeedCap) {
    speed_cap = kMonsterMoveGroundSpeedCap;
  }
  if ((spline_flags_ & (game::SplineFlag::kFlying |
                        game::SplineFlag::kCatmullRom)) != 0u) {
    speed_cap = kMonsterMoveSmoothPathSpeedCap;
  }

  float speed = speed_cap;
  if (duration_ms_ != 0u) {
    const float wire_speed =
        polyline_length / (static_cast<float>(duration_ms_) * 0.001f);
    if (wire_speed < speed_cap) {
      speed = wire_speed;
    }
  }
  if (speed <= kMonsterMoveMinimumSpeed) {
    Stop();
    return;
  }
  const float capped_duration =
      std::round(polyline_length / speed * 1000.0f);
  duration_ms_ = capped_duration > 1.0f
                     ? static_cast<std::uint32_t>(capped_duration)
                     : 1u;
}

void MoveSpline::RebuildControlPoints() {
  control_points_.clear();
  if (points_.empty()) {
    return;
  }
  if (!catmull_rom_ || points_.size() == 1u) {
    control_points_ = points_;
    return;
  }

  control_points_.reserve(points_.size() + 3u);
  if (IsCyclic() && entered_cycle_ && points_.size() > 2u) {
    control_points_.push_back(points_[points_.size() - 2u]);
  } else {

    control_points_.push_back({points_.front().x - std::cos(start_facing_),
                               points_.front().y - std::sin(start_facing_),
                               points_.front().z});
  }
  control_points_.insert(control_points_.end(), points_.begin(), points_.end());
  if (IsCyclic() && points_.size() > 1u) {
    const std::size_t next_loop_point =
        entered_cycle_ ? 1u : std::min<std::size_t>(2u, points_.size() - 1u);
    control_points_.push_back(points_[next_loop_point]);
  } else {
    control_points_.push_back(points_.back());
  }
}

bool MoveSpline::Update(std::uint32_t dt_ms) {
  if (!active_ || finished_) return false;

  time_passed_ms_ += dt_ms;
  const auto effective_duration = EffectiveDurationMs();
  const auto completion_time = effective_duration;
  if (time_passed_ms_ >= completion_time) {
    if (IsCyclic()) {
      if (effective_duration > 0u) {
        time_passed_ms_ %= effective_duration;
      } else {
        time_passed_ms_ = 0;
      }
      base_rate_ = playback_speed_;
      playback_speed_ = 1.0f;
      if ((spline_flags_ & game::SplineFlag::kEnterCycle) != 0u) {
        EnterCyclicLoop();
      }
      return true;
    }
    time_passed_ms_ = completion_time;
    active_ = false;
    finished_ = true;
    return false;
  }
  return true;
}

void MoveSpline::EnterCyclicLoop() {
  spline_flags_ &= ~game::SplineFlag::kEnterCycle;
  if (entered_cycle_ || points_.size() < 3u) {
    return;
  }

  points_.erase(points_.begin());
  entered_cycle_ = true;
  start_pos_ = points_.front();
  RebuildControlPoints();
  RebuildArcLengthCache();
}

std::uint32_t MoveSpline::EffectiveDurationMs() const {
  const float scaled = static_cast<float>(duration_ms_) * base_rate_ + 0.5f;
  return scaled > 0.0f ? static_cast<std::uint32_t>(scaled) : 0u;
}

void MoveSpline::SyncAnimationSpeed(float target_progress) {
  if (duration_ms_ == 0) {
    playback_speed_ = 1.0f;
    return;
  }

  const float effective_duration = static_cast<float>(EffectiveDurationMs());
  if (effective_duration < 0.01f) {
    playback_speed_ = 1.0f;
    return;
  }

  const float current_progress =
      static_cast<float>(static_cast<std::int32_t>(time_passed_ms_)) /
      effective_duration;

  float delta = target_progress - current_progress;
  if (delta > 0.5f) delta -= 1.0f;
  if (delta < -0.5f) delta += 1.0f;

  const auto delta_frames =
      static_cast<std::int64_t>(delta * static_cast<float>(duration_ms_));
  const auto remaining = static_cast<std::uint32_t>(
      static_cast<std::int64_t>(duration_ms_) - delta_frames);

  float speed = static_cast<float>(remaining) /
                static_cast<float>(duration_ms_);

  speed = std::clamp(speed, 0.5f, 2.0f);
  playback_speed_ = speed;
}

void MoveSpline::RebuildArcLengthCache() {
  segment_lengths_.clear();
  total_arc_length_ = 0.0f;

  if (points_.size() < 2) {
    return;
  }

  segment_lengths_.reserve(points_.size() - 1);
  for (std::size_t segment = 0; segment + 1 < points_.size(); ++segment) {
    float segment_length = 0.0f;
    if (catmull_rom_) {
      segment_length = CatmullRom::SegmentLength(
          control_points_[segment], control_points_[segment + 1u],
          control_points_[segment + 2u], control_points_[segment + 3u]);
    } else {
      segment_length = Vec3Length(points_[segment + 1] - points_[segment]);
    }

    segment_lengths_.push_back(segment_length);
    total_arc_length_ += segment_length;
  }
}

MoveSpline::ArcLengthSegment MoveSpline::SelectArcLengthSegment(float t) const {
  ArcLengthSegment selected{};
  if (segment_lengths_.empty()) {
    return selected;
  }

  const float clamped_t = std::clamp(t, 0.0f, 1.0f);
  const int last_segment = static_cast<int>(segment_lengths_.size()) - 1;
  if (last_segment == 0) {
    selected.local_t = clamped_t;
    selected.length = segment_lengths_.front();
    return selected;
  }

  if (clamped_t >= 1.0f) {
    selected.index = last_segment;
    selected.local_t = 1.0f;
    selected.length = segment_lengths_.back();
    return selected;
  }

  const float target_length = total_arc_length_ * clamped_t;
  float accumulated_length = 0.0f;
  for (int segment = 0; segment < last_segment; ++segment) {
    const float segment_length = segment_lengths_[segment];
    if (accumulated_length + segment_length > target_length) {
      selected.index = segment;
      selected.length = segment_length;
      if (segment_length > kLengthEpsilon) {
        selected.local_t =
            (target_length - accumulated_length) / segment_length;
      }
      return selected;
    }
    accumulated_length += segment_length;
  }

  selected.index = last_segment;
  selected.length = segment_lengths_.back();
  if (selected.length > kLengthEpsilon) {
    selected.local_t =
        (target_length - accumulated_length) / selected.length;
  }
  return selected;
}

game::Vec3 MoveSpline::GetCurrentPosition() const {
  if (points_.empty()) return {};
  if (points_.size() == 1 || duration_ms_ == 0) return points_.front();

  const auto effective_duration = EffectiveDurationMs();
  if (effective_duration == 0u) return points_.back();
  const float t = std::clamp(
      static_cast<float>(time_passed_ms_) / static_cast<float>(effective_duration),
      0.0f, 1.0f);

  game::Vec3 pos = EvaluatePosition(t);

  if (IsParabolic() && time_passed_ms_ >= parabolic_start_ms_) {
    pos.z += ComputeParabolicZ(time_passed_ms_);
  } else if (IsFalling()) {
    pos.z = ComputeFallingZ(time_passed_ms_);
  }

  return pos;
}

game::Vec3 MoveSpline::GetCurrentVelocity() const {
  if (points_.size() < 2 || duration_ms_ == 0) return {};
  if (segment_lengths_.empty() || total_arc_length_ <= kLengthEpsilon) return {};

  const auto effective_duration = EffectiveDurationMs();
  if (effective_duration == 0u) return {};
  const float t = std::clamp(
      static_cast<float>(time_passed_ms_) / static_cast<float>(effective_duration),
      0.0f, 1.0f);
  const ArcLengthSegment segment = SelectArcLengthSegment(t);
  if (segment.length <= kLengthEpsilon) return {};

  const game::Vec3 tangent = catmull_rom_
      ? EvaluateCatmullRomTangent(segment)
      : EvaluateLinearTangent(segment);
  const float seconds = static_cast<float>(effective_duration) * 0.001f;
  const float scale = total_arc_length_ / (segment.length * seconds);
  return tangent * scale;
}

game::Vec3 MoveSpline::GetFinalDestination() const {
  return final_destination_;
}

void MoveSpline::ResolveArrivalTargetFacing(
    const std::optional<game::Vec3> target_position) {
  if (facing_mode_ != SplineFacingMode::kTarget) {
    return;
  }
  if (!target_position.has_value()) {

    facing_mode_ = SplineFacingMode::kNone;
    return;
  }

  const game::Vec3 pos = GetCurrentPosition();
  const game::Vec3 d = *target_position - pos;
  facing_angle_ = std::atan2(d.y, d.x);
  facing_mode_ = SplineFacingMode::kAngle;
}

float MoveSpline::GetCurrentFacing() const {
  float facing = start_facing_;
  const bool apply_final_facing = finished_ || duration_ms_ == 0u;
  switch (apply_final_facing ? facing_mode_ : SplineFacingMode::kNone) {
    case SplineFacingMode::kAngle:
      facing = facing_angle_;
      break;
    case SplineFacingMode::kSpot: {
      game::Vec3 pos = GetCurrentPosition();
      game::Vec3 d = facing_spot_ - pos;
      facing = std::atan2(d.y, d.x);
      break;
    }
    case SplineFacingMode::kTarget:

      facing = facing_angle_;
      break;
    default: {

      constexpr std::uint32_t kNoAutoRotateMask =
          game::SplineFlag::kOrientFixed | game::SplineFlag::kFalling;
      if ((spline_flags_ & kNoAutoRotateMask) != 0) {
        facing = start_facing_;
        break;
      }

      game::Vec3 vel = GetCurrentVelocity();
      if (vel.x * vel.x + vel.y * vel.y > 0.0018490001f) {
        facing = std::atan2(vel.y, vel.x);
      } else {
        facing = start_facing_;
      }
      break;
    }
  }

  if (!apply_final_facing &&
      (spline_flags_ & game::SplineFlag::kBackward) != 0u) {
    facing -= std::numbers::pi_v<float>;
  }
  const float full_circle = 2.0f * std::numbers::pi_v<float>;
  facing = std::fmod(facing, full_circle);
  if (facing < 0.0f) {
    facing += full_circle;
  }
  return facing;
}

bool MoveSpline::IsCyclic() const {
  return (spline_flags_ & game::SplineFlag::kCyclic) != 0;
}

bool MoveSpline::IsParabolic() const {
  return (spline_flags_ & game::SplineFlag::kParabolic) != 0;
}

bool MoveSpline::IsFalling() const {
  return (spline_flags_ & game::SplineFlag::kFalling) != 0;
}

float MoveSpline::ComputeParabolicZ(std::uint32_t time_ms) const {
  if (!IsParabolic() || time_ms < parabolic_start_ms_) return 0.0f;

  const float dt = static_cast<float>(time_ms - parabolic_start_ms_) * 0.001f;
  const float effect_duration =
      static_cast<float>(duration_ms_ - std::min(duration_ms_, parabolic_start_ms_)) *
      0.001f;

  return 0.5f * vertical_accel_ * dt * (effect_duration - dt);
}

float MoveSpline::ComputeFallingZ(const std::uint32_t time_ms) const {
  if (!IsFalling()) {
    const auto effective_duration = EffectiveDurationMs();
    const float t = effective_duration == 0u
                        ? 1.0f
                        : std::clamp(static_cast<float>(time_ms) /
                                         static_cast<float>(effective_duration),
                                     0.0f, 1.0f);
    return EvaluatePosition(t).z;
  }

  const float seconds = static_cast<float>(time_ms) * 0.001f;
  const float terminal_time = MoveConst::kTerminalVelocity / MoveConst::kGravity;
  const float displacement = seconds > terminal_time
      ? 0.5f * MoveConst::kGravity * terminal_time * terminal_time +
            (seconds - terminal_time) * MoveConst::kTerminalVelocity
      : 0.5f * MoveConst::kGravity * seconds * seconds;
  const float floor = final_destination_.z;
  return std::max(start_pos_.z - displacement, floor);
}

game::Vec3 MoveSpline::EvaluatePosition(float t) const {
  const ArcLengthSegment segment = SelectArcLengthSegment(t);
  if (catmull_rom_) {
    return EvaluateCatmullRom(segment);
  }
  return EvaluateLinear(segment);
}

game::Vec3 MoveSpline::EvaluateLinear(const ArcLengthSegment& segment) const {
  const auto n = static_cast<int>(points_.size());
  if (n < 2) return points_.empty() ? game::Vec3{} : points_[0];

  const int seg = std::clamp(segment.index, 0, n - 2);
  return LerpVec3(points_[seg], points_[seg + 1], segment.local_t);
}

game::Vec3 MoveSpline::EvaluateCatmullRom(
    const ArcLengthSegment& segment) const {
  if (control_points_.size() < 4u) {
    return points_.empty() ? game::Vec3{} : points_.front();
  }
  const auto seg = static_cast<std::size_t>(std::clamp(
      segment.index, 0, static_cast<int>(points_.size()) - 2));
  return CatmullRom::EvalVec3(control_points_[seg], control_points_[seg + 1u],
                              control_points_[seg + 2u], control_points_[seg + 3u],
                              segment.local_t);
}

game::Vec3 MoveSpline::EvaluateLinearTangent(
    const ArcLengthSegment& segment) const {
  const auto n = static_cast<int>(points_.size());
  if (n < 2) return {};

  const int seg = std::clamp(segment.index, 0, n - 2);
  return points_[seg + 1] - points_[seg];
}

game::Vec3 MoveSpline::EvaluateCatmullRomTangent(
    const ArcLengthSegment& segment) const {
  if (control_points_.size() < 4u) return {};
  const auto seg = static_cast<std::size_t>(std::clamp(
      segment.index, 0, static_cast<int>(points_.size()) - 2));
  return CatmullRom::EvalTangent(control_points_[seg], control_points_[seg + 1u],
                                 control_points_[seg + 2u], control_points_[seg + 3u],
                                 segment.local_t);
}

bool MovementSplineManager::ShouldStartMonsterSpline(
    const game::MonsterMoveInfo& info) const {
  return info.move_type != game::MonsterMoveType::kStop &&
         !info.waypoints.empty();
}

void MovementSplineManager::ApplyMonsterMove(const game::MonsterMoveInfo& info,
                                             const float start_facing,
                                             std::optional<game::Vec3> current_position,
                                             std::optional<float> target_facing) {
  const std::uint64_t guid = info.mover.GetRawValue();

  if (info.move_type == game::MonsterMoveType::kStop) {

    splines_.erase(guid);
    return;
  }

  if (!ShouldStartMonsterSpline(info) || info.waypoints.empty()) {
    return;
  }

  auto& spline = splines_[guid];
  spline.Initialize(info, start_facing, current_position, target_facing,
                    GetSpeeds(guid).run);

  if (!spline.IsActive()) {
    splines_.erase(guid);
  }
}

void MovementSplineManager::SyncAuthoritativeSpline(const std::uint64_t guid,
                                                    const game::MovementUpdate& update,
                                                    const std::optional<float> target_facing) {
  SetSpeeds(guid, UnitSpeeds{
                      .walk = update.speeds[game::kSpeedWalk],
                      .run = update.speeds[game::kSpeedRun],
                      .runBack = update.speeds[game::kSpeedRunBack],
                      .swim = update.speeds[game::kSpeedSwim],
                      .swimBack = update.speeds[game::kSpeedSwimBack],
                      .flight = update.speeds[game::kSpeedFlight],
                      .flightBack = update.speeds[game::kSpeedFlightBack],
                      .turn = update.speeds[game::kSpeedTurnRate],
                      .pitch = update.speeds[game::kSpeedPitchRate],
                  });
  if (!update.IsLiving() || !update.movement.HasSplineEnabled() || !update.spline.active) {
    splines_.erase(guid);
    return;
  }

  auto& spline = splines_[guid];
  spline.Initialize(update, target_facing);
}

const MoveSpline* MovementSplineManager::GetSpline(std::uint64_t guid) const {
  auto it = splines_.find(guid);
  return it != splines_.end() ? &it->second : nullptr;
}

MoveSpline* MovementSplineManager::GetSpline(std::uint64_t guid) {
  auto it = splines_.find(guid);
  return it != splines_.end() ? &it->second : nullptr;
}

bool MovementSplineManager::HasActiveSpline(std::uint64_t guid) const {
  auto it = splines_.find(guid);
  return it != splines_.end() && it->second.IsActive();
}

void MovementSplineManager::CancelSpline(const std::uint64_t guid) {
  splines_.erase(guid);
}

void MovementSplineManager::SyncFlightSplineAnimation(std::uint64_t guid,
                                                      float target_progress) {
  auto* spline = GetSpline(guid);
  if (spline && spline->IsActive()) {
    spline->SyncAnimationSpeed(target_progress);
  }
}

void MovementSplineManager::SetSpeeds(std::uint64_t guid, const UnitSpeeds& speeds) {
  speeds_[guid] = speeds;
}

UnitSpeeds MovementSplineManager::GetSpeeds(std::uint64_t guid) const {
  auto it = speeds_.find(guid);
  if (it != speeds_.end()) return it->second;
  return UnitSpeeds{};
}

void MovementSplineManager::Update(const std::uint32_t client_time_ms) {
  if (!last_update_tick_ms_) {
    last_update_tick_ms_ = client_time_ms;
    return;
  }

  const auto signed_delta = static_cast<std::int32_t>(
      client_time_ms - *last_update_tick_ms_);
  last_update_tick_ms_ = client_time_ms;
  if (signed_delta <= 0) {
    return;
  }
  const auto dt_ms = static_cast<std::uint32_t>(signed_delta);

  for (auto it = splines_.begin(); it != splines_.end();) {
    if (it->second.IsFinished()) {
      it = splines_.erase(it);
      continue;
    }
    it->second.Update(dt_ms);
    ++it;
  }

}

std::optional<game::Vec3> MovementSplineManager::GetWorldPosition(
    std::uint64_t guid) const {
  auto sp_it = splines_.find(guid);
  if (sp_it != splines_.end()) {
    return sp_it->second.GetCurrentPosition();
  }

  return std::nullopt;
}

void MovementSplineManager::RemoveEntity(std::uint64_t guid) {
  splines_.erase(guid);
  speeds_.erase(guid);
}

void MovementSplineManager::Clear() {
  splines_.clear();
  speeds_.clear();
  last_update_tick_ms_.reset();
}

std::size_t MovementSplineManager::ActiveSplineCount() const {
  std::size_t count = 0;
  for (const auto& [guid, sp] : splines_) {
    if (sp.IsActive()) ++count;
  }
  return count;
}

}
