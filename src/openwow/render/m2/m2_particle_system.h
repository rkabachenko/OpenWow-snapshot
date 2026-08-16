#pragma once

#include "openwow/core/client_crt_random.h"
#include "openwow/data/model/m2_model.h"
#include "openwow/render/models/animation/spline.h"
#include "openwow/render/api/math/render_math_types.h"

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace openwow::render::m2 {

class M2Animator;

inline constexpr std::uint32_t kM2ParticleFlagEmitterLocalSpace = 0x00000010u;

inline constexpr std::uint32_t kM2ParticleFlagSortByViewDepth = 0x00000002u;

inline constexpr std::uint32_t kM2ParticleFlagScaleByEmitterMatrix = 0x00000020u;
inline constexpr std::uint32_t kM2ParticleFlagPositionDeltaTrail = 0x00000040u;

inline constexpr std::uint32_t kM2ParticleFlagAlignToEmitterPlane = 0x00001000u;

inline constexpr std::uint32_t kM2ParticleFlagSphereEmitStraightUp = 0x00000100u;
inline constexpr std::uint32_t kM2ParticleFlagRandomTumbleAxesSign = 0x00000200u;
inline constexpr std::uint32_t kM2ParticleFlagTailLengthClampToAge = 0x00000400u;
inline constexpr std::uint32_t kM2ParticleFlagFollowEmitterMotion = 0x00004000u;
inline constexpr std::uint32_t kM2ParticleFlagRandomTextureTile = 0x00010000u;
inline constexpr std::uint32_t kM2ParticleFlagHeadGeometry = 0x00020000u;
inline constexpr std::uint32_t kM2ParticleFlagTailGeometry = 0x00040000u;
inline constexpr std::uint32_t kM2ParticleFlagIndependentScaleVariation = 0x00800000u;

struct M2Particle {
  bx::Vec3 position{0.0f, 0.0f, 0.0f};
  bx::Vec3 velocity{0.0f, 0.0f, 0.0f};
  float age_ms{0.0f};
  float max_age_ms{1000.0f};
  float spin_rad{0.0f};
  float spin_speed{0.0f};
  bx::Quaternion orientation{0.0f, 0.0f, 0.0f, 1.0f};
  bx::Vec3 angular_velocity{0.0f, 0.0f, 0.0f};
  std::uint16_t visual_random_seed{0};
  std::uint32_t tile_index{0};
};

struct M2EmitterState {
  float emission_accumulator{0.0f};
  bool spline_path_end_initialized{false};
  bool spline_path_end_dirty{false};
  float spline_path_end{0.0f};
  std::vector<M2Particle> particles;

  float trail_delta_accumulator_sec{0.0f};
  bx::Vec3 trail_current_position{0.0f, 0.0f, 0.0f};
  bx::Vec3 trail_previous_position{0.0f, 0.0f, 0.0f};
  bool trail_has_history{false};
  bx::Vec3 trail_delta{0.0f, 0.0f, 0.0f};

  RenderMatrix4x4 emitter_world_matrix{kRenderIdentityMatrix4x4};

  float emitter_matrix_x_scale{0.0f};
};

struct ParticleVertex {
  float x, y, z;
  std::uint32_t color;
  float u, v;

  static bgfx::VertexLayout layout;
  static void InitLayout();
};

struct M2ParticleDrawInput {
  std::uint32_t emitter_index = 0;
  std::uint32_t first_vertex = 0;
  std::uint32_t vertex_count = 0;
  std::uint32_t texture_index = 0;
  std::uint8_t blending_type = 0;
  std::uint32_t flags = 0;
};

struct M2ParticleDrawRange {
  std::uint32_t first_emitter = 0;
  std::uint32_t emitter_count = 0;
  std::uint32_t first_vertex = 0;
  std::uint32_t vertex_count = 0;
  std::uint32_t texture_index = 0;
  std::uint8_t blending_type = 0;
  std::uint8_t render_particle_type = 0;
  std::uint8_t sort_blend_index = 0;
  std::uint32_t flag_sort_key = 0;
  std::uint32_t flags = 0;
};

struct M2ParticleRandomVector {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

class M2ParticleRandomState {
public:
  M2ParticleRandomState() noexcept = default;
  explicit M2ParticleRandomState(std::uint32_t seed) noexcept {
    Seed(seed);
  }

  void Seed(std::uint32_t seed) noexcept;
  [[nodiscard]] std::uint32_t value() const noexcept {
    return value_;
  }
  [[nodiscard]] std::uint32_t packed_indices() const noexcept {
    return packed_indices_;
  }

  [[nodiscard]] std::uint32_t NextU32() noexcept;
  [[nodiscard]] float NextUnitFloat() noexcept;
  [[nodiscard]] float NextSignedUnitFloat() noexcept;
  [[nodiscard]] float NextRange(float range) noexcept;
  [[nodiscard]] float NextSignedRange(float range) noexcept;
  [[nodiscard]] M2ParticleRandomVector NextUnitVector() noexcept;
  [[nodiscard]] std::uint16_t NextU16() noexcept;
  [[nodiscard]] std::uint32_t NextIndex(std::uint32_t count) noexcept;

private:
  std::uint32_t value_{0};
  std::uint32_t packed_indices_{0};
};

class M2ParticleSystem {
public:
  M2ParticleSystem() = default;

  void Init(const openwow::data::model::M2Model *model);

  void Update(float delta_seconds, int animation_index, std::uint32_t time_ms,
              const std::vector<float> &bone_mats, std::size_t bone_count,
              const M2Animator &animator, float density_scale,
              const RenderMatrix4x4 &model_mtx,
              const std::optional<bx::Vec3> &camera_world_position);

  const std::vector<ParticleVertex> &BuildVertices(const bx::Vec3 &cam_right,
                                                   const bx::Vec3 &cam_up);

  std::size_t TotalParticleCount() const;

  const std::vector<ParticleVertex> &vertices() const {
    return vertices_;
  }

  std::size_t emitter_count() const {
    return emitters_.size();
  }

  std::size_t emitter_particle_count(std::size_t index) const {
    return (index < emitters_.size()) ? emitters_[index].particles.size() : 0;
  }
  std::uint32_t emitter_render_vertex_count(std::size_t index) const {
    return index < emitter_render_vertex_counts_.size() ? emitter_render_vertex_counts_[index] : 0u;
  }

  static std::optional<std::uint64_t> BlendStateForType(std::uint8_t blending_type);

private:
  const openwow::data::model::M2Model *model_{nullptr};
  std::vector<M2EmitterState> emitters_;
  std::vector<openwow::render::CSpline> emitter_splines_;
  std::vector<ParticleVertex> vertices_;
  std::vector<std::uint32_t> emitter_render_vertex_counts_;
  core::ClientCrtRandom client_random_;
  M2ParticleRandomState random_;

  std::uint64_t state_generation_{0};

  bool vertices_valid_{false};
  std::uint64_t vertices_state_generation_{0};
  std::array<std::uint32_t, 3> vertices_cam_right_bits_{};
  std::array<std::uint32_t, 3> vertices_cam_up_bits_{};

  openwow::data::model::M2Vec3 SampleParticleLifetimeVec3(
      const openwow::data::model::M2ParticleLifetimeTrack<openwow::data::model::M2Vec3> &track,
      float t) const;
  openwow::data::model::M2Vec2 SampleParticleLifetimeVec2(
      const openwow::data::model::M2ParticleLifetimeTrack<openwow::data::model::M2Vec2> &track,
      float t) const;
  float SampleParticleLifetimeU16Norm(
      const openwow::data::model::M2ParticleLifetimeTrack<std::uint16_t> &track, float t) const;
};

[[nodiscard]] std::optional<std::uint8_t>
ResolveM2ParticleRenderTypeFromBlendMode(std::uint8_t blending_type);
[[nodiscard]] std::uint8_t
ResolveM2ParticleRetailSortBlendIndex(std::uint8_t render_particle_type,
                                      bool force_additive_sort);
[[nodiscard]] std::uint32_t ResolveM2ParticleRetailFlagSortKey(std::uint32_t flags,
                                                               std::uint8_t blending_type);
[[nodiscard]] bool IsM2ParticleAtlasDimensionValid(std::uint32_t value) noexcept;
[[nodiscard]] bool IsM2ParticleAtlasValid(std::uint32_t rows, std::uint32_t columns) noexcept;
[[nodiscard]] std::uint32_t ResolveM2ParticleTwinkleSampleIndex(
    float age_seconds, float twinkle_speed, std::uintptr_t particle_address) noexcept;
[[nodiscard]] float SampleM2ParticleTwinkleTable(std::uint32_t index) noexcept;
[[nodiscard]] std::uint16_t SampleM2ParticleU16LifetimeTrack(
    const openwow::data::model::M2ParticleLifetimeTrack<std::uint16_t> &track,
    float lifetime_fraction, std::uint16_t default_value);
[[nodiscard]] std::uint32_t
ComputeM2ParticleEmitterCapacity(float emission_rate, float emission_rate_variation,
                                 float lifespan_seconds, float lifespan_variation_seconds) noexcept;
[[nodiscard]] std::optional<std::vector<M2ParticleDrawRange>>
BuildM2ParticleDrawRanges(const std::vector<M2ParticleDrawInput> &inputs, bool batch_particles,
                          bool force_additive_sort);

struct ParticleShaderHandles {
  bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle sampler = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle flags = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle fog_params = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle fog_color = BGFX_INVALID_HANDLE;
};

ParticleShaderHandles LoadParticleProgram();

void DestroyParticleProgram(ParticleShaderHandles &handles);

}
