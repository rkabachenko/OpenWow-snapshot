#pragma once

#include "openwow/render/api/math/render_math_types.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace openwow::render::m2 {

struct RibbonSegment {
  RenderVec3 bottom_position{};
  RenderVec3 top_position{};
  float tex_coord = 0.0f;
  float age_seconds = 0.0f;
};

struct RibbonEmitterDef {
  float edge_lifetime_seconds = 0.0f;
  float edges_per_second = 0.0f;
  float gravity = 0.0f;
};

[[nodiscard]] std::uint32_t
ComputeM2RibbonSegmentCapacity(float edges_per_second, float edge_lifetime_seconds) noexcept;
[[nodiscard]] float NormalizeM2RibbonEdgesPerSecond(float edges_per_second) noexcept;
[[nodiscard]] float NormalizeM2RibbonEdgeLifetime(float edge_lifetime_seconds) noexcept;

class M2RibbonEmitter {
public:
  M2RibbonEmitter() = default;

  void Initialize(const RibbonEmitterDef &def);
  void Update(float dt_seconds, const RenderVec3 &emitter_position,
              const RenderVec3 &emitter_width_axis, const RenderVec3 &emitter_tangent_axis,
              float height_above, float height_below);
  [[nodiscard]] const std::vector<RibbonSegment> &GetSegments() const {
    return segments_;
  }
  [[nodiscard]] std::uint32_t GetSegmentCount() const {
    return static_cast<std::uint32_t>(segments_.size());
  }
  [[nodiscard]] bool IsEnabled() const {
    return enabled_;
  }
  void SetEnabled(bool enabled);
  void Reset();

private:
  void StoreEmitterPose(const RenderVec3 &position, const RenderVec3 &width_axis,
                        const RenderVec3 &tangent_axis);
  void AddInterpolatedSegment(float interpolation, float age_seconds, float height_above,
                              float height_below);
  [[nodiscard]] RibbonSegment BuildInterpolatedSegment(float interpolation, float age_seconds,
                                                       float height_above,
                                                       float height_below) const;
  void AgeSegments(float dt_seconds);
  void RebuildRenderableSegments();
  void RecalcTexCoords();

  RibbonEmitterDef def_;
  std::vector<RibbonSegment> active_segments_;
  std::vector<RibbonSegment> segments_;
  RibbonSegment current_segment_{};
  float emit_phase_ = 0.0f;
  RenderVec3 previous_position_{};
  RenderVec3 current_position_{};
  RenderVec3 previous_width_axis_{0.0f, 1.0f, 0.0f};
  RenderVec3 current_width_axis_{0.0f, 1.0f, 0.0f};
  RenderVec3 previous_tangent_axis_{0.0f, 0.0f, 1.0f};
  RenderVec3 current_tangent_axis_{0.0f, 0.0f, 1.0f};
  std::uint32_t segment_capacity_ = 0;
  bool enabled_ = true;
  bool has_pose_ = false;
  bool has_current_segment_ = false;
  bool has_updated_ = false;
};

class M2RibbonEmitterSystem {
public:
  M2RibbonEmitterSystem() = default;

  void AddEmitter(const RibbonEmitterDef &def);
  bool UpdateEmitter(std::uint32_t emitter_index, float dt_seconds,
                     const RenderVec3 &emitter_position, const RenderVec3 &emitter_width_axis,
                     const RenderVec3 &emitter_tangent_axis,
                     float height_above, float height_below);
  [[nodiscard]] const std::vector<M2RibbonEmitter> &GetEmitters() const {
    return emitters_;
  }
  [[nodiscard]] std::uint32_t GetEmitterCount() const {
    return static_cast<std::uint32_t>(emitters_.size());
  }
  [[nodiscard]] std::uint32_t GetTotalSegmentCount() const;
  bool SetEmitterEnabled(std::uint32_t emitter_index, bool enabled);
  void Clear();

private:
  std::vector<M2RibbonEmitter> emitters_;
};

}
