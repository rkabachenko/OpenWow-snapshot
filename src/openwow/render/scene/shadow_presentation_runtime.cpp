#include "openwow/render/scene/shadow_presentation_runtime.h"

#include "openwow/render/world/doodads/doodad_renderer.h"
#include "openwow/render/m2/m2_system.h"
#include "openwow/render/world/terrain/terrain_renderer.h"
#include "openwow/world/coordinates/frustum.h"
#include "openwow/world/presentation/world_presentation_snapshot.h"

#include <algorithm>
#include <cstddef>
#include <type_traits>
#include <vector>

namespace openwow::render {
namespace {

constexpr double kShadowCasterRenderMicroseconds = 1.15;

ShadowQuality ResolveQuality(const std::uint8_t quality) {
  return static_cast<ShadowQuality>(
      std::min<std::uint8_t>(quality,
                             static_cast<std::uint8_t>(ShadowQuality::Ultra)));
}

constexpr std::uint64_t kFnv1aOffsetBasis = 0xcbf29ce484222325ull;
constexpr std::uint64_t kFnv1aPrime = 0x100000001b3ull;

[[nodiscard]] std::uint64_t HashBytes(std::uint64_t hash, const void *const data,
                                      const std::size_t size) {
  const auto *const bytes = static_cast<const std::uint8_t *>(data);
  for (std::size_t index = 0; index < size; ++index) {
    hash ^= bytes[index];
    hash *= kFnv1aPrime;
  }
  return hash;
}

template <typename T>
[[nodiscard]] std::uint64_t HashValue(const std::uint64_t hash, const T &value) {
  static_assert(std::is_trivially_copyable_v<T>);
  return HashBytes(hash, &value, sizeof(T));
}

[[nodiscard]] std::uint64_t HashMatrix(const std::uint64_t hash, const float *const matrix) {
  return HashBytes(hash, matrix, sizeof(float) * 16u);
}

constexpr std::int8_t kStaticInstancingClassNoResidue = 1;
constexpr std::int8_t kStaticInstancingClassTransparentResidue = 2;

}

ShadowPresentationRuntime::ShadowPresentationRuntime(m2::M2System& m2_system)
    : m2_system_(m2_system), data_(std::make_unique<ShadowRenderData>()) {}

ShadowPresentationRuntime::~ShadowPresentationRuntime() { Shutdown(); }

bool ShadowPresentationRuntime::Initialize() {
  if (initialized_) {
    return true;
  }
  data_->SetType(ShadowType::ShadowMap);
  data_->SetEnabled(true);

  InvalidateShadowReuse();
  initialized_ = data_->CreateShadowMap();
  if (initialized_) {
    resolution_ =
        static_cast<std::uint16_t>(data_->GetShadowMapResolution());
  }

  return true;
}

void ShadowPresentationRuntime::Shutdown() {
  if (!data_) {
    return;
  }
  data_->DestroyShadowMap();
  data_->ClearCasters();
  casters_.clear();
  instance_ids_.clear();
  InvalidateShadowReuse();
  resolution_ = 0;
  initialized_ = false;
}

void ShadowPresentationRuntime::ResetMap() {
  if (data_) {
    data_->ClearCasters();
    casters_.clear();
    instance_ids_.clear();
    InvalidateShadowReuse();
  }
}

void ShadowPresentationRuntime::InvalidateShadowReuse() noexcept {
  has_rendered_key_ = false;
  has_previous_content_hash_ = false;
}

void ShadowPresentationRuntime::ApplySettings(const world::WorldPresentationSnapshot &snapshot) {
  const auto &settings = snapshot.shadows;
  data_->SetEnabled(settings.enabled);
  data_->SetQuality(ResolveQuality(settings.quality));
  data_->SetShadowDistance(std::max(settings.distance, 1.0f));
  data_->SetShadowBias(std::max(settings.depth_bias, 0.0f));
  data_->SetLightDirection(snapshot.environment.light_direction[0],
                           snapshot.environment.light_direction[1],
                           snapshot.environment.light_direction[2]);

  if (settings.map_resolution != resolution_) {
    data_->DestroyShadowMap();
    data_->SetShadowMapResolution(settings.map_resolution);
    resolution_ = static_cast<std::uint16_t>(data_->GetShadowMapResolution());
    initialized_ = false;

    InvalidateShadowReuse();
  }
  if (settings.enabled && !initialized_) {
    InvalidateShadowReuse();
    initialized_ = data_->CreateShadowMap();
  }
}

void ShadowPresentationRuntime::Render(const world::WorldPresentationSnapshot &snapshot,
                                       const std::uint8_t shadow_view, DoodadRenderer &doodads,
                                       TerrainRenderer &terrain) {
  if (!data_) {
    return;
  }
  ApplySettings(snapshot);
  if (!initialized_ || !snapshot.shadows.enabled) {
    InvalidateShadowReuse();
    terrain.SetShadowRenderData(nullptr);
    return;
  }

  const auto &camera = snapshot.camera.position;
  world::Frustum camera_frustum{};
  for (std::size_t plane = 0; plane < camera_frustum.planes.size(); ++plane) {
    std::copy_n(snapshot.camera.frustum_planes.begin() + plane * 4u, 4u,
                camera_frustum.planes[plane].begin());
  }
  const float max_distance_squared = snapshot.shadows.distance * snapshot.shadows.distance;
  constexpr std::size_t kMinInstancedShadowGroupSize = 2u;
  casters_.clear();
  instance_ids_.clear();
  instanced_groups_.clear();

  ShadowFrameKey frame_key{};
  frame_key.reusable = true;
  std::uint64_t caster_hash = kFnv1aOffsetBasis;
  doodads.VisitVisibleInstances(camera_frustum, camera[0], camera[1], camera[2],
                                snapshot.camera.forward, [&](const DoodadInstance &instance) {
                                  if (instance.m2_instance_id == 0u || instance.alpha <= 0.0f) {
                                    return;
                                  }
                                  const float dx = instance.bounding_center[0] - camera[0];
                                  const float dy = instance.bounding_center[1] - camera[1];
                                  const float dz = instance.bounding_center[2] - camera[2];
                                  if (dx * dx + dy * dy + dz * dz > max_distance_squared) {
                                    return;
                                  }
                                  if (instance.shadow_class_memo < 0) {
                                    const auto shadow_class =
                                        m2_system_.QueryShadowClass(instance.m2_instance_id);
                                    if (shadow_class.status != m2::M2ResultStatus::kReady) {
                                      return;
                                    }
                                    instance.shadow_class_memo =
                                        static_cast<std::int32_t>(shadow_class.shadow_class);
                                  }
                                  const float radius =
                                      instance.has_bounding_radius
                                          ? std::max(instance.bounding_radius, 0.01f)
                                          : std::max(instance.scale, 0.01f);
                                  caster_hash = HashValue(caster_hash, instance.m2_instance_id);
                                  caster_hash = HashValue(caster_hash, instance.m2_model_id);
                                  caster_hash = HashValue(caster_hash, instance.model_matrix);
                                  caster_hash = HashValue(caster_hash, instance.alpha);
                                  caster_hash = HashValue(caster_hash, instance.tint_color[3]);
                                  caster_hash = HashValue(
                                      caster_hash, instance.admission_cache.distance_alpha);
                                  caster_hash = HashValue(
                                      caster_hash, instance.wmo_color_is_ambient_substitute);
                                  caster_hash =
                                      HashValue(caster_hash, instance.static_instancing_state);
                                  caster_hash =
                                      HashValue(caster_hash, instance.render_ready_latched);

                                  if (!(instance.static_instancing_state ==
                                            kStaticInstancingClassNoResidue ||
                                        (instance.static_instancing_state ==
                                             kStaticInstancingClassTransparentResidue &&
                                         instance.render_ready_latched))) {
                                    frame_key.reusable = false;
                                  }
                                  casters_.push_back(ShadowCasterEntry{
                                      .entityId = instance.m2_instance_id,
                                      .worldX = instance.bounding_center[0],
                                      .worldY = instance.bounding_center[1],
                                      .worldZ = instance.bounding_center[2],
                                      .radius = radius,
                                      .height = radius * 2.0f,
                                      .isValid = true,
                                  });

                                  if (instance.static_instancing_state ==
                                          kStaticInstancingClassNoResidue &&
                                      instance.alpha == 1.0f) {
                                    auto &group = instanced_groups_[instance.m2_model_id];
                                    if (group.records.empty()) {
                                      group.exemplar_instance_id = instance.m2_instance_id;
                                    }
                                    group.records.push_back(
                                        {.transform = instance.model_matrix,
                                         .color = {1.0f, 1.0f, 1.0f, 1.0f}});
                                    group.member_ids.push_back(instance.m2_instance_id);
                                    return;
                                  }
                                  instance_ids_.push_back(instance.m2_instance_id);
                                });

  bool has_instanced_groups = false;
  for (auto &[model_id, group] : instanced_groups_) {
    (void)model_id;
    if (group.records.size() >= kMinInstancedShadowGroupSize) {
      has_instanced_groups = true;
    } else {

      instance_ids_.insert(instance_ids_.end(), group.member_ids.begin(),
                           group.member_ids.end());
      group.records.clear();
      group.member_ids.clear();
    }
  }

  data_->SetCasters(casters_);
  if (instance_ids_.empty() && !has_instanced_groups) {
    InvalidateShadowReuse();
    terrain.SetShadowRenderData(nullptr);
    return;
  }

  if (!data_->PrepareShadowPass(
          snapshot.camera.view.data(), snapshot.camera.projection.data(),
          snapshot.camera.near_clip,
          std::min(snapshot.camera.far_clip, std::max(snapshot.shadows.distance, 1.0f)))) {
    InvalidateShadowReuse();
    terrain.SetShadowRenderData(nullptr);
    return;
  }

  frame_key.caster_count = static_cast<std::uint32_t>(casters_.size());
  std::uint64_t hash = caster_hash;
  hash = HashValue(hash, snapshot.map_generation.value);
  hash = HashValue(hash, snapshot.camera.view);
  hash = HashValue(hash, snapshot.camera.projection);
  hash = HashValue(hash, snapshot.camera.position);
  hash = HashValue(hash, snapshot.camera.forward);
  hash = HashValue(hash, snapshot.camera.near_clip);
  hash = HashValue(hash, snapshot.camera.far_clip);
  hash = HashValue(hash, snapshot.shadows.enabled);
  hash = HashValue(hash, snapshot.shadows.quality);
  hash = HashValue(hash, snapshot.shadows.map_resolution);
  hash = HashValue(hash, snapshot.shadows.distance);
  hash = HashValue(hash, snapshot.shadows.depth_bias);
  hash = HashMatrix(hash, data_->GetLightView());
  hash = HashMatrix(hash, data_->GetLightProj());
  frame_key.content_hash = hash;

  frame_key.previous_content_hash = previous_content_hash_;
  const bool previous_frame_known = has_previous_content_hash_;
  previous_content_hash_ = hash;
  has_previous_content_hash_ = true;

  if (has_rendered_key_ && previous_frame_known && frame_key.reusable &&
      frame_key == rendered_key_) {
    terrain.SetShadowRenderData(data_.get());
    return;
  }

  data_->BeginShadowDepthPass(shadow_view);

  render_results_scratch_.resize(instance_ids_.size());
  m2_system_.RenderInstanceBatch(shadow_view, instance_ids_,
                                 RenderMatrix4x4View{data_->GetLightView(), 16u},
                                 m2::M2RenderPassScope::kOpaqueOnly, m2_system_.frame_job_system(),
                                 kShadowCasterRenderMicroseconds,
                                 render_results_scratch_);

  for (auto &[model_id, group] : instanced_groups_) {
    (void)model_id;
    if (group.records.size() < kMinInstancedShadowGroupSize) {
      continue;
    }
    (void)m2_system_.RenderInstancedGroup(shadow_view, group.exemplar_instance_id,
                                          group.records, m2::M2BatchUniforms{});
  }

  rendered_key_ = frame_key;
  has_rendered_key_ = previous_frame_known;
  terrain.SetShadowRenderData(data_.get());
}

}
