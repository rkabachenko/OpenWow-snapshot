#pragma once

#include "openwow/render/api/math/render_math_types.h"
#include "openwow/render/m2/m2_public_types.h"
#include "openwow/render/scene/shadow_data.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace openwow::game {
struct ObjectPresentationSnapshot;
}

namespace openwow::world {
struct Frustum;
struct WorldPresentationSnapshot;
}

namespace openwow::render {

class DoodadRenderer;
class ObjectRenderer;
class TerrainRenderer;
namespace m2 {
class M2System;
}

class ShadowPresentationRuntime final {
 public:
  using EnvironmentalCasterRenderer = std::function<void(
      std::uint8_t view_id, const float* view_mtx, const float* projection_mtx,
      const world::Frustum& frustum, const RenderVec3& target,
      std::uint16_t resolution)>;

  explicit ShadowPresentationRuntime(m2::M2System& m2_system);
  ~ShadowPresentationRuntime();

  ShadowPresentationRuntime(const ShadowPresentationRuntime&) = delete;
  ShadowPresentationRuntime& operator=(const ShadowPresentationRuntime&) =
      delete;

  [[nodiscard]] bool Initialize();
  void Shutdown();
  void ResetMap();

  void Render(const world::WorldPresentationSnapshot& snapshot,
              std::uint8_t first_shadow_view, DoodadRenderer& doodads,
              TerrainRenderer& terrain, ObjectRenderer& objects,
              const game::ObjectPresentationSnapshot& object_snapshot,
              const EnvironmentalCasterRenderer& render_environment);

 private:
  [[nodiscard]] bool ApplySettings(
      const world::WorldPresentationSnapshot& snapshot);
  [[nodiscard]] RenderVec3 ResolveTarget(
      const world::WorldPresentationSnapshot& snapshot,
      const game::ObjectPresentationSnapshot& objects) const;
  [[nodiscard]] bool ShouldRefreshProduct(std::size_t product_index,
                                          const RenderVec3& target) const;
  [[nodiscard]] static RenderVec3 SnapExteriorTarget(
      std::size_t product_index, const RenderVec3& target);
  void InvalidateProducts() noexcept;

  m2::M2System& m2_system_;
  std::unique_ptr<ShadowRenderData> data_;
  std::vector<std::uint32_t> doodad_instance_ids_;
  std::vector<std::uint64_t> object_entity_ids_;
  std::vector<m2::M2RenderInstanceResult> doodad_results_;
  std::array<RenderVec3, kWorldShadowProductCount> product_targets_{};
  std::array<bool, kWorldShadowProductCount> product_published_{};
  RenderVec3 light_direction_{};
  std::uint64_t map_generation_{0u};
  std::uint8_t quality_{0u};
  std::uint16_t resolution_{0u};
  bool has_light_direction_{false};
  bool initialized_{false};
};

}
