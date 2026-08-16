#pragma once

#include "openwow/data/wmo/wmo_file.h"
#include "openwow/world/coordinates/world_geometry.h"
#include "openwow/world/liquid/water_heightfield.h"
#include "openwow/world/wmo/wmo_visibility.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>

namespace openwow::world {

[[nodiscard]] std::uint32_t ResolveWmoGroupLiquidType(
    const data::wmo::WmoRoot &root, const data::wmo::WmoGroup &group);

[[nodiscard]] std::uint32_t ResolveWmoGroupRenderLiquidType(
    std::uint32_t liquid_type, std::uint32_t group_flags,
    std::uint32_t liquid_type_flags) noexcept;

struct WmoLiquidPointQueryResult {
  std::uint32_t liquid_type_id = 0u;
  float surface_height = 0.0f;
};

[[nodiscard]] std::optional<WmoLiquidPointQueryResult>
QueryWmoGroupLiquidAtLocalPosition(
    const data::wmo::WmoGroup &group, std::uint32_t resolved_liquid_type,
    const std::array<float, 3> &local_position,
    std::uint32_t liquid_type_flags) noexcept;

using WmoSiblingGroupResolver =
    std::function<const data::wmo::WmoGroup *(std::uint32_t)>;

[[nodiscard]] std::optional<WaterHeightfield>
BuildWmoGroupLiquidHeightfield(
    const data::wmo::WmoRoot &root, const data::wmo::WmoGroup &group,
    const Matrix4 &model_matrix, std::uint32_t liquid_type_flags = 0u,
    std::uint32_t liquid_vertex_format = 0u,
    std::uint32_t depth_attenuation_table = 0u,
    const WmoSiblingGroupResolver &resolve_sibling_group = {});

[[nodiscard]] bool IsWmoLiquidVisibleThroughGroupPaths(
    const WaterHeightfield& heightfield, std::uint16_t group_index,
    std::span<const WmoVisibleGroupPath> visible_group_paths,
    const Matrix4& world_view_projection) noexcept;

}
