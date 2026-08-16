#include "openwow/render/m2/m2_ribbon_emitter.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

namespace openwow::render::m2 {

namespace {

constexpr float kM2RibbonMinimumEdgeLifetimeSeconds = 0.25f;
constexpr float kM2RibbonFirstUpdateStepEpsilonSeconds = 0.0001f;
constexpr float kM2RibbonGravityAgeScale = 2.0f;

[[nodiscard]] bool IsValidM2RibbonEmitterDef(const RibbonEmitterDef &def) noexcept {
  return std::isfinite(def.edges_per_second) && def.edges_per_second >= 0.0f &&
         std::isfinite(def.edge_lifetime_seconds) && std::isfinite(def.gravity) &&
         ComputeM2RibbonSegmentCapacity(def.edges_per_second, def.edge_lifetime_seconds) != 0u;
}

[[nodiscard]] bool M2RibbonVec3IsFinite(const RenderVec3 &values) noexcept {
  return std::isfinite(values[0]) && std::isfinite(values[1]) &&
         std::isfinite(values[2]);
}

[[nodiscard]] float M2RibbonVec3Length(const RenderVec3 &values) noexcept {
  return std::sqrt(values[0] * values[0] + values[1] * values[1] + values[2] * values[2]);
}

[[nodiscard]] std::optional<RenderVec3>
M2RibbonNormalizeVec3(const RenderVec3 &values) noexcept {
  if (!M2RibbonVec3IsFinite(values)) {
    return std::nullopt;
  }
  const float length = M2RibbonVec3Length(values);
  if (!(length > 0.0f) || !std::isfinite(length)) {
    return std::nullopt;
  }
  return RenderVec3{values[0] / length, values[1] / length, values[2] / length};
}

[[nodiscard]] RenderVec3 M2RibbonNormalizeOr(const RenderVec3 &src,
                                             const RenderVec3 &fallback) noexcept {
  if (const auto normalized = M2RibbonNormalizeVec3(src)) {
    return *normalized;
  }
  return fallback;
}

[[nodiscard]] RenderVec3 M2RibbonAdd(const RenderVec3 &a, const RenderVec3 &b) noexcept {
  return {a[0] + b[0], a[1] + b[1], a[2] + b[2]};
}

[[nodiscard]] RenderVec3 M2RibbonSub(const RenderVec3 &a, const RenderVec3 &b) noexcept {
  return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

[[nodiscard]] RenderVec3 M2RibbonMul(const RenderVec3 &values, const float scalar) noexcept {
  return {values[0] * scalar, values[1] * scalar, values[2] * scalar};
}

[[nodiscard]] RenderVec3
M2RibbonHermitePoint(const RenderVec3 &previous_point, const RenderVec3 &current_point,
                     const RenderVec3 &previous_tangent, const RenderVec3 &current_tangent,
                     const float interpolation) noexcept {
  const float t = std::clamp(interpolation, 0.0f, 1.0f);
  const float one_minus_t = 1.0f - t;
  RenderVec3 out{};
  for (std::size_t axis = 0; axis < 3u; ++axis) {
    out[axis] = (current_point[axis] - one_minus_t * current_tangent[axis]) * t +
                (t * previous_tangent[axis] + previous_point[axis]) * one_minus_t;
  }
  return out;
}

}

float NormalizeM2RibbonEdgesPerSecond(const float edges_per_second) noexcept {
  if (!std::isfinite(edges_per_second) || edges_per_second < 0.0f) {
    return 0.0f;
  }
  return std::ceil(edges_per_second);
}

float NormalizeM2RibbonEdgeLifetime(const float edge_lifetime_seconds) noexcept {
  if (!std::isfinite(edge_lifetime_seconds)) {
    return 0.0f;
  }
  return std::max(edge_lifetime_seconds, kM2RibbonMinimumEdgeLifetimeSeconds);
}

std::uint32_t ComputeM2RibbonSegmentCapacity(const float edges_per_second,
                                             const float edge_lifetime_seconds) noexcept {
  if (!std::isfinite(edges_per_second) || !std::isfinite(edge_lifetime_seconds) ||
      edges_per_second < 0.0f) {
    return 0u;
  }

  const float edge_rate = NormalizeM2RibbonEdgesPerSecond(edges_per_second);
  const float edge_lifetime = NormalizeM2RibbonEdgeLifetime(edge_lifetime_seconds);
  if (!(edge_lifetime > 0.0f)) {
    return 0u;
  }

  const float live_edge_count = std::ceil(edge_lifetime * edge_rate);
  const float segment_count = std::max(live_edge_count + 2.0f, 0.0f);
  if (segment_count >= static_cast<float>(std::numeric_limits<std::uint32_t>::max())) {
    return std::numeric_limits<std::uint32_t>::max();
  }
  return static_cast<std::uint32_t>(segment_count);
}

void M2RibbonEmitter::Initialize(const RibbonEmitterDef &def) {
  def_ = def;
  segment_capacity_ =
      ComputeM2RibbonSegmentCapacity(def_.edges_per_second, def_.edge_lifetime_seconds);
  active_segments_.clear();
  segments_.clear();
  current_segment_ = {};
  emit_phase_ = 0.0f;
  has_pose_ = false;
  has_current_segment_ = false;
  has_updated_ = false;
  enabled_ = IsValidM2RibbonEmitterDef(def_) && segment_capacity_ != 0u;
}

void M2RibbonEmitter::Reset() {
  active_segments_.clear();
  segments_.clear();
  current_segment_ = {};
  emit_phase_ = 0.0f;
  has_pose_ = false;
  has_current_segment_ = false;
  has_updated_ = false;
}

void M2RibbonEmitter::SetEnabled(const bool enabled) {
  if (!enabled && enabled_) {
    Reset();
  }
  enabled_ = enabled && IsValidM2RibbonEmitterDef(def_) && segment_capacity_ != 0u;
}

void M2RibbonEmitter::Update(const float dt_seconds, const RenderVec3 &emitter_position,
                             const RenderVec3 &emitter_width_axis,
                             const RenderVec3 &emitter_tangent_axis,
                             const float height_above, const float height_below) {
  if (!enabled_ || segment_capacity_ == 0u || !(dt_seconds > 0.0f) ||
      !std::isfinite(dt_seconds) || !M2RibbonVec3IsFinite(emitter_position) ||
      !M2RibbonVec3IsFinite(emitter_width_axis) || !M2RibbonVec3IsFinite(emitter_tangent_axis) ||
      !std::isfinite(height_above) || !std::isfinite(height_below)) {
    return;
  }

  const float edge_rate = NormalizeM2RibbonEdgesPerSecond(def_.edges_per_second);
  const float edge_lifetime = NormalizeM2RibbonEdgeLifetime(def_.edge_lifetime_seconds);
  StoreEmitterPose(emitter_position, emitter_width_axis, emitter_tangent_axis);

  float step_seconds = dt_seconds;
  if (!has_updated_ && edge_rate > 0.0f) {
    step_seconds = (1.0f / edge_rate) + kM2RibbonFirstUpdateStepEpsilonSeconds;
  }
  has_updated_ = true;
  step_seconds = std::clamp(step_seconds, 0.0f, edge_lifetime);

  AgeSegments(step_seconds);

  if (edge_rate > 0.0f) {
    const float previous_phase = emit_phase_;
    const float edge_progress = step_seconds * edge_rate + previous_phase;
    const auto edge_count = static_cast<std::uint32_t>(std::floor(edge_progress));
    const float phase_delta = edge_progress - previous_phase;
    for (std::uint32_t edge_index = 0; edge_index < edge_count; ++edge_index) {
      const float edge_phase = static_cast<float>(edge_index + 1u);
      const float interpolation =
          phase_delta > 0.0f ? (edge_phase - previous_phase) / phase_delta : 1.0f;
      AddInterpolatedSegment(interpolation, -step_seconds * interpolation, height_above,
                             height_below);
    }
    emit_phase_ = edge_progress - std::floor(edge_progress);
  }

  current_segment_ = BuildInterpolatedSegment(1.0f, 0.0f, height_above, height_below);
  has_current_segment_ = true;
  RebuildRenderableSegments();
  RecalcTexCoords();
}

void M2RibbonEmitter::StoreEmitterPose(const RenderVec3 &position, const RenderVec3 &width_axis,
                                       const RenderVec3 &tangent_axis) {
  static constexpr RenderVec3 kDefaultWidthAxis{0.0f, 1.0f, 0.0f};
  static constexpr RenderVec3 kDefaultTangentAxis{0.0f, 0.0f, 1.0f};

  if (!has_pose_) {
    previous_position_ = position;
    current_position_ = position;
    previous_width_axis_ = M2RibbonNormalizeOr(width_axis, kDefaultWidthAxis);
    current_width_axis_ = previous_width_axis_;
    previous_tangent_axis_ = M2RibbonNormalizeOr(tangent_axis, kDefaultTangentAxis);
    current_tangent_axis_ = previous_tangent_axis_;
    has_pose_ = true;
    return;
  }

  previous_position_ = current_position_;
  previous_width_axis_ = current_width_axis_;
  previous_tangent_axis_ = current_tangent_axis_;
  current_position_ = position;
  current_width_axis_ = M2RibbonNormalizeOr(width_axis, kDefaultWidthAxis);
  current_tangent_axis_ = M2RibbonNormalizeOr(tangent_axis, kDefaultTangentAxis);
}

RibbonSegment M2RibbonEmitter::BuildInterpolatedSegment(const float interpolation,
                                                        const float age_seconds,
                                                        const float height_above,
                                                        const float height_below) const {
  const auto previous_bottom =
      M2RibbonSub(previous_position_, M2RibbonMul(previous_width_axis_, height_below));
  const auto current_bottom =
      M2RibbonSub(current_position_, M2RibbonMul(current_width_axis_, height_below));
  const auto previous_top =
      M2RibbonAdd(previous_position_, M2RibbonMul(previous_width_axis_, height_above));
  const auto current_top =
      M2RibbonAdd(current_position_, M2RibbonMul(current_width_axis_, height_above));
  const auto delta = M2RibbonSub(current_position_, previous_position_);
  const float travel_distance = M2RibbonVec3Length(delta);
  const auto previous_tangent = M2RibbonMul(previous_tangent_axis_, travel_distance);
  const auto current_tangent = M2RibbonMul(current_tangent_axis_, travel_distance);

  RibbonSegment segment{};
  segment.bottom_position = M2RibbonHermitePoint(previous_bottom, current_bottom, previous_tangent,
                                                current_tangent, interpolation);
  segment.top_position = M2RibbonHermitePoint(previous_top, current_top, previous_tangent,
                                             current_tangent, interpolation);
  segment.age_seconds = age_seconds;
  return segment;
}

void M2RibbonEmitter::AddInterpolatedSegment(const float interpolation, const float age_seconds,
                                             const float height_above, const float height_below) {
  if (segment_capacity_ == 0u) {
    return;
  }
  while (active_segments_.size() + 1u >= segment_capacity_) {
    active_segments_.erase(active_segments_.begin());
  }
  active_segments_.push_back(
      BuildInterpolatedSegment(interpolation, age_seconds, height_above, height_below));
}

void M2RibbonEmitter::AgeSegments(const float dt_seconds) {
  const float edge_lifetime = NormalizeM2RibbonEdgeLifetime(def_.edge_lifetime_seconds);
  if (!(edge_lifetime > 0.0f)) {
    active_segments_.clear();
    return;
  }

  auto dst = active_segments_.begin();
  for (auto src = active_segments_.begin(); src != active_segments_.end(); ++src) {
    if (src->age_seconds + dt_seconds > edge_lifetime) {
      continue;
    }

    const float age_before = src->age_seconds;
    src->age_seconds += dt_seconds;
    const float gravity_delta =
        (kM2RibbonGravityAgeScale * def_.gravity * age_before * dt_seconds) +
        (def_.gravity * dt_seconds * dt_seconds);
    src->bottom_position[2] += gravity_delta;
    src->top_position[2] += gravity_delta;

    if (dst != src) {
      *dst = *src;
    }
    ++dst;
  }
  active_segments_.erase(dst, active_segments_.end());
}

void M2RibbonEmitter::RebuildRenderableSegments() {
  segments_ = active_segments_;
  if (!active_segments_.empty() && has_current_segment_) {
    segments_.push_back(current_segment_);
  }
}

void M2RibbonEmitter::RecalcTexCoords() {
  const float edge_lifetime = NormalizeM2RibbonEdgeLifetime(def_.edge_lifetime_seconds);
  if (segments_.empty() || !(edge_lifetime > 0.0f)) {
    return;
  }

  for (auto &segment : segments_) {
    segment.tex_coord = segment.age_seconds / edge_lifetime;
  }
}

void M2RibbonEmitterSystem::AddEmitter(const RibbonEmitterDef &def) {
  M2RibbonEmitter emitter;
  emitter.Initialize(def);
  emitters_.push_back(std::move(emitter));
}

bool M2RibbonEmitterSystem::UpdateEmitter(const std::uint32_t emitter_index,
                                          const float dt_seconds,
                                          const RenderVec3 &emitter_position,
                                          const RenderVec3 &emitter_width_axis,
                                          const RenderVec3 &emitter_tangent_axis,
                                          const float height_above, const float height_below) {
  if (emitter_index >= emitters_.size()) {
    return false;
  }
  emitters_[emitter_index].Update(dt_seconds, emitter_position, emitter_width_axis,
                                  emitter_tangent_axis, height_above, height_below);
  return true;
}

std::uint32_t M2RibbonEmitterSystem::GetTotalSegmentCount() const {
  std::uint32_t total = 0;
  for (const auto &emitter : emitters_) {
    total += emitter.GetSegmentCount();
  }
  return total;
}

bool M2RibbonEmitterSystem::SetEmitterEnabled(const std::uint32_t emitter_index,
                                              const bool enabled) {
  if (emitter_index >= emitters_.size()) {
    return false;
  }
  emitters_[emitter_index].SetEnabled(enabled);
  return true;
}

void M2RibbonEmitterSystem::Clear() {
  emitters_.clear();
}

}
