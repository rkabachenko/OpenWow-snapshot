#pragma once

#include "openwow/world/coordinates/world_geometry.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace openwow::world {

struct LiquidFringeVertex {
  Vec3 position{};
  Vec3 normal{0.0f, 0.0f, 1.0f};
  std::array<float, 2> texcoord{};

  float depth{0.0f};
};

struct WaterHeightfield {
  float origin_x{0.0f};
  float origin_z{0.0f};
  float step_x{0.0f};
  float step_z{0.0f};
  std::uint32_t vertex_rows{0};
  std::uint32_t vertex_cols{0};
  std::vector<float> heights;

  std::vector<float> vertex_depths;

  float depth_lane_u{0.0f};
  std::vector<std::array<float, 2>> vertex_texcoords;
  std::vector<std::uint8_t> render_mask;
  std::vector<Vec3> positions;
  std::uint8_t type{0};
  std::uint8_t depth_level{2};

  std::uint32_t liquid_type_id{0};

  std::uint32_t render_liquid_type_id{0};
  bool authored_wmo{false};

  std::array<std::uint8_t, 4> vertex_color{255u, 255u, 255u, 255u};

  std::vector<LiquidFringeVertex> fringe_vertices;
  std::vector<std::uint32_t> fringe_indices;

  Vec3 bounds_min{0.0f, 0.0f, 0.0f};
  Vec3 bounds_max{0.0f, 0.0f, 0.0f};
};

struct WaterHeightfieldSample {
  float height{0.0f};
  std::uint8_t type{0};
  std::uint32_t liquid_type_id{0};
};

[[nodiscard]] inline std::size_t WaterHeightfieldVertexIndex(
    const WaterHeightfield& surface, const std::uint32_t row,
    const std::uint32_t column) noexcept {
  return static_cast<std::size_t>(row) * surface.vertex_cols + column;
}

[[nodiscard]] inline bool IsWaterHeightfieldCellRendered(
    const WaterHeightfield& surface, const std::uint32_t row,
    const std::uint32_t column) noexcept {
  if (surface.render_mask.empty()) {
    return true;
  }
  const std::size_t expected_size =
      static_cast<std::size_t>(surface.vertex_rows - 1u) *
      (surface.vertex_cols - 1u);
  if (surface.render_mask.size() != expected_size) {
    return true;
  }
  const std::uint32_t cell_columns = surface.vertex_cols - 1u;
  const std::size_t index =
      static_cast<std::size_t>(row) * cell_columns + column;
  return index < surface.render_mask.size() && surface.render_mask[index] != 0u;
}

[[nodiscard]] inline std::optional<WaterHeightfieldSample>
SampleWaterHeightfield(const WaterHeightfield& surface, const float x,
                       const float z) noexcept {
  if (surface.vertex_rows < 2u || surface.vertex_cols < 2u ||
      surface.heights.size() !=
          static_cast<std::size_t>(surface.vertex_rows) * surface.vertex_cols ||
      surface.step_x == 0.0f || surface.step_z == 0.0f) {
    return std::nullopt;
  }

  const float end_x = surface.origin_x +
                      static_cast<float>(surface.vertex_rows - 1u) *
                          surface.step_x;
  const float end_z = surface.origin_z +
                      static_cast<float>(surface.vertex_cols - 1u) *
                          surface.step_z;
  if (x < std::min(surface.origin_x, end_x) ||
      x > std::max(surface.origin_x, end_x) ||
      z < std::min(surface.origin_z, end_z) ||
      z > std::max(surface.origin_z, end_z)) {
    return std::nullopt;
  }

  const float row_coordinate = (x - surface.origin_x) / surface.step_x;
  const float column_coordinate = (z - surface.origin_z) / surface.step_z;
  if (row_coordinate < 0.0f ||
      row_coordinate > static_cast<float>(surface.vertex_rows - 1u) ||
      column_coordinate < 0.0f ||
      column_coordinate > static_cast<float>(surface.vertex_cols - 1u)) {
    return std::nullopt;
  }

  const auto resolve_cell = [](const float coordinate,
                               const std::uint32_t vertex_count) {
    if (coordinate >= static_cast<float>(vertex_count - 1u)) {
      return std::pair{vertex_count - 2u, 1.0f};
    }
    const auto cell = static_cast<std::uint32_t>(coordinate);
    return std::pair{cell, coordinate - static_cast<float>(cell)};
  };
  const auto [row, row_fraction] =
      resolve_cell(row_coordinate, surface.vertex_rows);
  const auto [column, column_fraction] =
      resolve_cell(column_coordinate, surface.vertex_cols);
  if (!IsWaterHeightfieldCellRendered(surface, row, column)) {
    return std::nullopt;
  }

  const float h00 =
      surface.heights[WaterHeightfieldVertexIndex(surface, row, column)];
  const float h10 =
      surface.heights[WaterHeightfieldVertexIndex(surface, row, column + 1u)];
  const float h01 =
      surface.heights[WaterHeightfieldVertexIndex(surface, row + 1u, column)];
  const float h11 = surface.heights[
      WaterHeightfieldVertexIndex(surface, row + 1u, column + 1u)];
  const float near_height = h00 + (h10 - h00) * column_fraction;
  const float far_height = h01 + (h11 - h01) * column_fraction;
  return WaterHeightfieldSample{
      .height = near_height + (far_height - near_height) * row_fraction,
      .type = surface.type,
      .liquid_type_id = surface.liquid_type_id,
  };
}

}
