#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "openwow/render/scene/occlusion/occlusion_depth_buffer.h"

namespace openwow::render::occlusion {

struct OccluderSourceMesh {
  const std::byte* vertex_data{nullptr};
  std::size_t vertex_count{0u};
  std::size_t vertex_stride{0u};
  std::size_t position_offset{0u};
  std::size_t normal_offset{0u};
  std::span<const std::uint16_t> indices;
};

[[nodiscard]] std::vector<OccluderPolygon> BuildOccluderPolygons(
    const OccluderSourceMesh& mesh, std::size_t first_index,
    std::size_t index_count, float minimum_area);

[[nodiscard]] float OccluderPolygonArea(const OccluderPolygon& polygon) noexcept;

}
