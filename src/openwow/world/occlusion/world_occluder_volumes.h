#pragma once

#include "openwow/world/coordinates/frustum.h"
#include "openwow/world/coordinates/world_geometry.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace openwow::world {

class WorldOccluderVolumes {
 public:
  void Clear();

  void Rebuild(std::uint32_t map_id, std::span<const float, 3> eye,
               std::span<const float, 3> view_forward,
               float terrain_aperture_depth, const Frustum& frustum,
               bool second_pass_only = false);

  [[nodiscard]] bool empty() const { return volumes_.empty(); }

  [[nodiscard]] bool IsPolygonOccluded(
      std::span<const std::array<float, 3>> polygon) const;

  [[nodiscard]] bool IsSphereOccluded(std::span<const float, 4> sphere) const;

 private:
  struct Volume {
    std::uint32_t first_plane{0u};
    std::uint32_t plane_count{0u};
  };

  void AddVolume(std::span<const float, 3> eye,
                 std::span<const std::array<float, 3>> polygon,
                 bool camera_apex_only, std::span<const float, 3> view_forward,
                 float terrain_aperture_depth);

  std::vector<Vec4> planes_;
  std::vector<Volume> volumes_;
  std::vector<std::array<float, 3>> clipped_;
};

}
