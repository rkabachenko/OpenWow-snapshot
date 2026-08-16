#pragma once

#include "openwow/data/wmo/wmo_file.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace openwow::world {

struct WmoVertexColorPreparation {
  std::size_t transparent_vertex_count{0u};
  bool enabled{false};
};

[[nodiscard]] WmoVertexColorPreparation BuildWmoVertexColorPreparation(
    std::span<const data::wmo::WmoRenderBatch> batches,
    std::uint16_t transparent_batch_count,
    std::uint32_t mohd_flags) noexcept;

[[nodiscard]] data::wmo::WmoVertexColor PrepareWmoVertexColor(
    const data::wmo::WmoVertexColor &source, std::size_t vertex_index,
    const WmoVertexColorPreparation &preparation) noexcept;

[[nodiscard]] data::wmo::WmoVertexColor PrepareWmoPortalVertexColor(
    const data::wmo::WmoRoot& root, const data::wmo::WmoGroup& group,
    const data::wmo::Vec3f& position,
    const data::wmo::WmoVertexColor& prepared_color,
    std::size_t vertex_index,
    const WmoVertexColorPreparation& preparation) noexcept;

struct WmoVertexColorQueryResult {
  data::wmo::WmoVertexColor color{};
  bool outdoor{false};
};

struct WmoVertexColorQueryView {
  std::span<const data::wmo::WmoTriangleMaterial> triangle_materials;
  std::span<const std::uint16_t> indices;
  std::span<const data::wmo::Vec3f> vertices;
  std::span<const data::wmo::WmoVertexColor> vertex_colors;
  WmoVertexColorPreparation preparation{};
  std::uint32_t mohd_flags{0u};
  std::uint32_t ambient_color{0u};
};

[[nodiscard]] std::optional<WmoVertexColorQueryResult>
InterpolateWmoVertexColor(const WmoVertexColorQueryView &view,
                          const std::array<float, 3> &local_position,
                          std::uint16_t triangle_index) noexcept;

[[nodiscard]] std::optional<WmoVertexColorQueryResult>
InterpolateWmoGroupVertexColor(const data::wmo::WmoRoot &root,
                               const data::wmo::WmoGroup &group,
                               const std::array<float, 3> &local_position,
                               std::uint16_t triangle_index) noexcept;

[[nodiscard]] constexpr std::uint32_t PackWmoVertexColorBgra(
    const data::wmo::WmoVertexColor color) noexcept {
  return static_cast<std::uint32_t>(color.b) |
         (static_cast<std::uint32_t>(color.g) << 8u) |
         (static_cast<std::uint32_t>(color.r) << 16u) |
         (static_cast<std::uint32_t>(color.a) << 24u);
}

}
