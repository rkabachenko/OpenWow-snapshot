#include "openwow/world/presentation/world_presentation_snapshot.h"

#include "openwow/world/streaming/world_map.h"

#include <algorithm>
#include <array>
#include <limits>
#include <span>

namespace openwow::world {

WorldPresentationSnapshot WorldMap::BuildPresentationSnapshot(
    const CameraSnapshot& camera) const {
  WorldPresentationSnapshot snapshot{};
  snapshot.map_generation = world_staging_generation_;
  snapshot.map_id = map_id_;
  snapshot.minimally_valid = !map_name_.empty() && wdt_loaded_ &&
                             camera.map_generation == world_staging_generation_;
  snapshot.camera = camera;
  snapshot.environment = environment_;
  snapshot.environment.weather = active_weather_type_;
  snapshot.environment.weather_density = active_weather_density_;

  (void)ResolveAreaEnvironmentAtPosition(camera.position[0], camera.position[1],
                                         camera.position[2],
                                         AreaEnvironmentProbe::kCameraRoom);
  const AreaEnvironmentQueryCache* const camera_cache_entry =
      last_area_environment_resolution_.valid ? &last_area_environment_resolution_
                                              : nullptr;

  snapshot.world_models.reserve(wmo_instances_.size());
  for (const auto& [placement, instance] : wmo_instances_) {

    WorldPresentationItem& item = snapshot.world_models.emplace_back();
    item.stable_id = instance.placement_stable_id;
    item.resource_key = instance.wmo_path;
    item.transform = instance.model_matrix;
    item.visible = instance.visible;
    if (const auto cached = wmo_cache_.find(instance.wmo_path);
        cached != wmo_cache_.end()) {
      world::Frustum frustum{};
      for (std::size_t plane = 0; plane < frustum.planes.size(); ++plane) {
        std::copy_n(camera.frustum_planes.begin() + plane * 4u, 4u,
                    frustum.planes[plane].begin());
      }
      const Matrix4 view_projection = Multiply(camera.view, camera.projection);
      std::array<std::uint16_t, 4u> seed_storage{};
      std::size_t seed_count = 0u;
      std::span<const std::uint16_t> seeds;
      if (camera_cache_entry != nullptr) {
        const auto append_seed = [&](const std::optional<WmoAreaGroupRef>& ref) {
          if (!ref.has_value() || ref->placement != placement ||
              ref->group_index > std::numeric_limits<std::uint16_t>::max() ||
              ref->group_index >= cached->second.visibility.group_count()) {
            return;
          }
          const auto group = static_cast<std::uint16_t>(ref->group_index);
          if (std::find(seed_storage.begin(), seed_storage.begin() + seed_count,
                        group) == seed_storage.begin() + seed_count) {
            seed_storage[seed_count++] = group;
          }
        };
        append_seed(camera_cache_entry->containing_group);
        append_seed(camera_cache_entry->alternate_group);
        append_seed(camera_cache_entry->secondary_group);
        append_seed(camera_cache_entry->secondary_alternate_group);
        seeds = std::span<const std::uint16_t>(seed_storage.data(), seed_count);
        item.wmo_seed_groups.assign(seeds.begin(), seeds.end());
      }
      ComputeVisibleWmoGroups(
          cached->second.visibility, instance.model_matrix,
          view_projection, instance.placement_world_bounds,
          instance.group_world_bounds, frustum,
          camera.position[0], camera.position[1], camera.position[2],
          seeds, instance.visibility_workspace, item.visible_subresources,
          &item.wmo_visible_group_paths, &item.wmo_sky_visibility);
    }
  }
  return snapshot;
}

}
