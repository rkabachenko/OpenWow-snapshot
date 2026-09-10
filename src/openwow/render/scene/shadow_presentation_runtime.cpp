#include "openwow/render/scene/shadow_presentation_runtime.h"

#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/game/object_presentation_snapshot.h"
#include "openwow/render/m2/m2_system.h"
#include "openwow/render/scene/object_renderer.h"
#include "openwow/render/world/doodads/doodad_renderer.h"
#include "openwow/render/world/terrain/terrain_renderer.h"
#include "openwow/world/coordinates/frustum.h"
#include "openwow/world/presentation/world_presentation_snapshot.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <string>

#include <bx/math.h>

namespace openwow::render {
namespace {

constexpr double kShadowCasterRenderMicroseconds = 1.15;
constexpr std::array<float, kWorldShadowProductCount>
    kExteriorRefreshDistanceSquared{0.0f, 4.0f, 16.0f, 1024.0f};
constexpr std::array<float, kWorldShadowProductCount> kExteriorSnapGrid{
    0.0f, 2.0f, 4.0f, 16.0f};

[[nodiscard]] bool IsFinite(const RenderVec3& value) {
  return std::all_of(value.begin(), value.end(),
                     [](const float component) {
                       return std::isfinite(component);
                     });
}

[[nodiscard]] bool IsBasicCharacterCaster(const game::TypeID type) {
  return type == game::TypeID::kPlayer || type == game::TypeID::kUnit;
}

[[nodiscard]] bool IsEnvironmentalObjectCaster(const game::TypeID type) {
  return IsBasicCharacterCaster(type) || type == game::TypeID::kGameObject ||
         type == game::TypeID::kCorpse;
}

}

ShadowPresentationRuntime::ShadowPresentationRuntime(m2::M2System& m2_system)
    : m2_system_(m2_system), data_(std::make_unique<ShadowRenderData>()) {}

ShadowPresentationRuntime::~ShadowPresentationRuntime() { Shutdown(); }

bool ShadowPresentationRuntime::Initialize() {
  if (initialized_) {
    return true;
  }
  data_->Configure(0u, 1024u);
  initialized_ = data_->CreateResources();
  if (initialized_) {
    quality_ = 0u;
    resolution_ = 1024u;
    SetActiveWorldShadowRenderData(data_.get());
  }
  InvalidateProducts();
  return initialized_;
}

void ShadowPresentationRuntime::Shutdown() {
  if (!data_) {
    return;
  }
  SetActiveWorldShadowRenderData(nullptr);
  data_->DestroyResources();
  doodad_instance_ids_.clear();
  object_entity_ids_.clear();
  doodad_results_.clear();
  InvalidateProducts();
  quality_ = 0u;
  resolution_ = 0u;
  initialized_ = false;
}

void ShadowPresentationRuntime::ResetMap() {
  map_generation_ = 0u;
  InvalidateProducts();
}

void ShadowPresentationRuntime::InvalidateProducts() noexcept {
  product_published_.fill(false);
  product_targets_.fill({});
  if (data_) {
    data_->SetPublishedProductCount(0u);
  }
}

bool ShadowPresentationRuntime::ApplySettings(
    const world::WorldPresentationSnapshot& snapshot) {
  const auto& settings = snapshot.shadows;
  if (settings.quality != quality_ || settings.map_resolution != resolution_) {
    data_->Configure(settings.quality, settings.map_resolution);
    initialized_ = data_->CreateResources();
    InvalidateProducts();
    if (!initialized_) {
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kError,
          "ShadowPresentationRuntime: product configuration failed quality=" +
              std::to_string(settings.quality) + " resolution=" +
              std::to_string(settings.map_resolution));
      data_->Configure(0u, 1024u);
      initialized_ = data_->CreateResources();
      quality_ = settings.quality;
      resolution_ = settings.map_resolution;
      if (initialized_) {
        SetActiveWorldShadowRenderData(data_.get());
      } else {
        openwow::diagnostics::Log(
            openwow::diagnostics::LogLevel::kError,
            "ShadowPresentationRuntime: disabled fallback resource "
            "configuration failed");
      }
      return false;
    }
    quality_ = data_->quality();
    resolution_ = data_->resolution();
    SetActiveWorldShadowRenderData(data_.get());
  }

  const RenderVec3 light{snapshot.environment.light_direction[0],
                         snapshot.environment.light_direction[1],
                         snapshot.environment.light_direction[2]};
  const float light_delta_squared =
      (light[0] - light_direction_[0]) * (light[0] - light_direction_[0]) +
      (light[1] - light_direction_[1]) * (light[1] - light_direction_[1]) +
      (light[2] - light_direction_[2]) * (light[2] - light_direction_[2]);
  if (!has_light_direction_ || light_delta_squared > 1.0e-6f) {
    light_direction_ = light;
    has_light_direction_ = true;
    InvalidateProducts();
  }
  data_->SetLightDirection(light);

  if (snapshot.map_generation.value != map_generation_) {
    map_generation_ = snapshot.map_generation.value;
    InvalidateProducts();
  }
  return initialized_;
}

RenderVec3 ShadowPresentationRuntime::ResolveTarget(
    const world::WorldPresentationSnapshot& snapshot,
    const game::ObjectPresentationSnapshot& objects) const {
  if (objects.local_player.guid.GetRawValue() != 0u) {
    const auto player = std::lower_bound(
        objects.active.begin(), objects.active.end(),
        objects.local_player.guid.GetRawValue(),
        [](const game::ObjectPresentationRecord& record,
           const std::uint64_t raw_guid) {
          return record.handle.guid.GetRawValue() < raw_guid;
        });
    if (player != objects.active.end() &&
        player->handle == objects.local_player) {
      const RenderVec3 target{player->x, player->y, player->z};
      if (IsFinite(target)) {
        return target;
      }
    }
  }
  return {snapshot.camera.position[0], snapshot.camera.position[1],
          snapshot.camera.position[2]};
}

bool ShadowPresentationRuntime::ShouldRefreshProduct(
    const std::size_t product_index, const RenderVec3& target) const {
  if (product_index == 0u || quality_ >= 5u ||
      !product_published_[product_index]) {
    return true;
  }
  const RenderVec3& previous = product_targets_[product_index];
  const float dx = target[0] - previous[0];
  const float dy = target[1] - previous[1];
  const float dz = target[2] - previous[2];
  return dx * dx + dy * dy + dz * dz >=
         kExteriorRefreshDistanceSquared[product_index];
}

RenderVec3 ShadowPresentationRuntime::SnapExteriorTarget(
    const std::size_t product_index, const RenderVec3& target) {
  if (product_index == 0u || product_index >= kWorldShadowProductCount) {
    return target;
  }
  const float grid = kExteriorSnapGrid[product_index];
  return {std::floor(target[0] / grid) * grid,
          std::floor(target[1] / grid) * grid, target[2]};
}

void ShadowPresentationRuntime::Render(
    const world::WorldPresentationSnapshot& snapshot,
    const std::uint8_t first_shadow_view, DoodadRenderer& doodads,
    TerrainRenderer& terrain, ObjectRenderer& objects,
    const game::ObjectPresentationSnapshot& object_snapshot,
    const EnvironmentalCasterRenderer& render_environment) {
  if (!data_) {
    return;
  }
  terrain.SetPrecomputedShadowsEnabled(
      snapshot.shadows.precomputed_terrain_enabled);
  if (!ApplySettings(snapshot)) {
    terrain.SetShadowRenderData(nullptr);
    return;
  }
  terrain.SetShadowRenderData(data_.get());
  if (!snapshot.shadows.enabled || quality_ == 0u) {
    data_->SetPublishedProductCount(0u);
    return;
  }

  const RenderVec3 target = ResolveTarget(snapshot, object_snapshot);
  const std::size_t product_count = data_->active_product_count();
  for (std::size_t product_index = 0u; product_index < product_count;
       ++product_index) {
    const RenderVec3 product_target =
        SnapExteriorTarget(product_index, target);
    if (!ShouldRefreshProduct(product_index, product_target)) {
      continue;
    }
    if (!data_->PrepareProduct(product_index, product_target)) {
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kWarn,
          "ShadowPresentationRuntime: product preparation failed product=" +
              std::to_string(product_index));
      product_published_[product_index] = false;
      continue;
    }

    const auto view_value =
        static_cast<unsigned int>(first_shadow_view) + product_index;
    if (view_value > std::numeric_limits<std::uint8_t>::max()) {
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kError,
          "ShadowPresentationRuntime: shadow view range overflow");
      product_published_[product_index] = false;
      continue;
    }
    const auto view_id = static_cast<std::uint8_t>(view_value);
    data_->BeginShadowDepthPass(product_index, view_id);

    RenderMatrix4x4 view_projection{};
    bx::mtxMul(view_projection.data(), data_->light_view(product_index),
               data_->light_projection(product_index));
    world::Frustum frustum{};
    frustum.ExtractFromViewProj(
        std::span<const float, 16u>{view_projection});

    object_entity_ids_.clear();
    object_entity_ids_.reserve(object_snapshot.active.size());
    const bool include_environment = quality_ >= 3u;
    for (const auto& record : object_snapshot.active) {
      if (record.render_opacity <= 0.0f ||
          !(include_environment
                ? IsEnvironmentalObjectCaster(record.type_id)
                : IsBasicCharacterCaster(record.type_id))) {
        continue;
      }
      const float radius = std::max(2.0f, record.scale * 4.0f);
      if (frustum.TestSphere(record.x, record.y, record.z, radius)) {
        object_entity_ids_.push_back(record.handle.guid.GetRawValue());
      }
    }
    std::sort(object_entity_ids_.begin(), object_entity_ids_.end());
    objects.RenderShadowCasters(
        view_id, data_->light_view(product_index),
        data_->light_projection(product_index),
        static_cast<float>(resolution_), object_entity_ids_);
    objects.RenderMountShadowCasters(
        view_id, data_->light_view(product_index),
        data_->light_projection(product_index), object_snapshot,
        object_entity_ids_);

    if (include_environment) {
      doodad_instance_ids_.clear();
      doodads.VisitVisibleInstances(
          frustum, product_target[0], product_target[1], product_target[2],
          snapshot.camera.forward, [&](const DoodadInstance& instance) {
            if (instance.m2_instance_id != 0u && instance.alpha > 0.0f &&
                frustum.TestSphere(
                    instance.bounding_center[0], instance.bounding_center[1],
                    instance.bounding_center[2],
                    instance.has_bounding_radius
                        ? std::max(instance.bounding_radius, 0.01f)
                        : std::max(instance.scale, 0.01f))) {
              doodad_instance_ids_.push_back(instance.m2_instance_id);
            }
          });
      doodad_results_.assign(doodad_instance_ids_.size(), {});
      m2_system_.RenderInstanceBatch(
          view_id, doodad_instance_ids_,
          RenderMatrix4x4View{data_->light_view(product_index), 16u},
          m2::M2RenderPassScope::kShadowCaster,
          m2_system_.frame_job_system(), kShadowCasterRenderMicroseconds,
          doodad_results_);

      if (render_environment) {
        render_environment(view_id, data_->light_view(product_index),
                           data_->light_projection(product_index), frustum,
                           product_target, resolution_);
      }
    }
    product_targets_[product_index] = product_target;
    product_published_[product_index] = true;
  }

  std::size_t published_count = 0u;
  while (published_count < product_count &&
         product_published_[published_count]) {
    ++published_count;
  }
  data_->SetPublishedProductCount(published_count);
}

}
