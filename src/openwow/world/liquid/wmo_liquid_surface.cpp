#include "openwow/world/liquid/wmo_liquid_surface.h"

#include "openwow/platform/process/os_platform.h"
#include "openwow/world/liquid/liquid_polygon_clip.h"
#include "openwow/world/liquid/liquid_vertex_lookup.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <limits>

namespace openwow::world {
namespace {

constexpr float kRetailInverseLiquidCellSize = 0.24000001f;
constexpr float kLiquidCellSize = 1.0f / kRetailInverseLiquidCellSize;

constexpr std::uint8_t kMliqTileLiquidClassMask = 0x0fu;
constexpr std::uint8_t kMliqTileHole = 0x0fu;
constexpr std::uint8_t kMliqTileClippedFringe = 0x80u;

constexpr float kRetailFixedTexcoordScale = 1.0f / 256.0f;

[[nodiscard]] constexpr bool IsLiquidBodyTile(
    const std::uint8_t flags) noexcept {
  return (flags & kMliqTileLiquidClassMask) != kMliqTileHole &&
         (flags & kMliqTileClippedFringe) == 0u;
}

[[nodiscard]] constexpr bool HasLiquidTileClass(
    const std::uint8_t flags) noexcept {
  return (flags & kMliqTileLiquidClassMask) != kMliqTileHole;
}

[[nodiscard]] std::uint32_t ResolveLiquidType(
    const std::uint32_t group_flags, const std::uint32_t raw_type) noexcept {
  if (raw_type == 0u || raw_type >= 0x15u) {
    return raw_type;
  }
  switch ((raw_type - 1u) & 3u) {
    case 0u:
      return (group_flags & 0x80000u) != 0u ? 14u : 13u;
    case 1u:
      return 14u;
    case 2u:
      return 19u;
    default:
      return 20u;
  }
}

[[nodiscard]] std::uint32_t FirstVisibleLiquidType(
    const data::wmo::WmoLiquidData& liquid) noexcept {

  for (const std::uint8_t flags : liquid.tileFlags) {
    if (HasLiquidTileClass(flags)) {
      return static_cast<std::uint32_t>(
          (flags & kMliqTileLiquidClassMask) + 1u);
    }
  }
  return 0u;
}

[[nodiscard]] std::uint32_t NormalizeHeaderLiquidType(
    const data::wmo::WmoRoot &root, const data::wmo::WmoGroup &group) noexcept {
  if ((root.header.flags & data::wmo::kWmoFlagUseDbcLiquid) != 0u) {
    return group.header.liquidType;
  }

  constexpr std::uint32_t kLegacyLiquidTypeNone = 15u;
  if (group.header.liquidType == kLegacyLiquidTypeNone) {
    return 0u;
  }
  return group.header.liquidType + 1u;
}

[[nodiscard]] bool IsInteriorWmoLiquidGroup(
    const std::uint32_t group_flags,
    const std::uint32_t liquid_type_flags) noexcept {
  constexpr std::uint32_t kExteriorMask =
      data::wmo::kMogpExterior | data::wmo::kMogpExteriorLit;
  constexpr std::uint32_t kDisableInteriorWmoWater = 0x200u;
  return (group_flags & kExteriorMask) == 0u &&
         (liquid_type_flags & kDisableInteriorWmoWater) == 0u;
}

[[nodiscard]] std::array<std::uint8_t, 4> ResolveWmoLiquidVertexColor(
    const data::wmo::WmoRoot &root, const data::wmo::WmoGroup &group,
    const std::uint32_t liquid_type_flags) noexcept {
  constexpr std::array<std::uint8_t, 4> kExteriorLiquidColor{255u, 255u, 255u,
                                                             255u};
  if (!IsInteriorWmoLiquidGroup(group.header.flags, liquid_type_flags)) {
    return kExteriorLiquidColor;
  }
  const std::size_t material_index = group.liquid.header.materialId;
  if (material_index >= root.materials.size()) {

    return kExteriorLiquidColor;
  }
  const std::uint32_t diffuse = root.materials[material_index].diffuseColor;
  return {
      static_cast<std::uint8_t>((diffuse >> 16u) & 0xffu),
      static_cast<std::uint8_t>((diffuse >> 8u) & 0xffu),
      static_cast<std::uint8_t>(diffuse & 0xffu),
      static_cast<std::uint8_t>((diffuse >> 24u) & 0xffu),
  };
}

[[nodiscard]] Vec3 TransformPoint(
  const Matrix4 &model_matrix, const Vec3 &point) noexcept {
  return openwow::world::TransformPoint(point, model_matrix);
}

[[nodiscard]] float ReadVertexHeight(
    const std::vector<std::uint8_t> &vertex_data,
    const std::size_t vertex_index) noexcept {
  const std::size_t height_offset = vertex_index * 8u + 4u;
  const std::uint32_t height_bits =
      openwow::platform::LoadLittleEndian32(vertex_data.data() + height_offset);
  return std::bit_cast<float>(height_bits);
}

[[nodiscard]] std::uint8_t ReadVertexDepthByte(
    const std::vector<std::uint8_t> &vertex_data,
    const std::size_t vertex_index) noexcept {
  return vertex_data[vertex_index * 8u];
}

[[nodiscard]] std::array<float, 2> ReadVertexTextureCoordinates(
    const std::vector<std::uint8_t>& vertex_data,
    const std::size_t vertex_index) noexcept {
  const std::size_t offset = vertex_index * 8u;
  const auto read_signed_16 = [&](const std::size_t component_offset) {
    const std::uint16_t bits =
        static_cast<std::uint16_t>(vertex_data[offset + component_offset]) |
        static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(
                vertex_data[offset + component_offset + 1u])
            << 8u);
    return std::bit_cast<std::int16_t>(bits);
  };
  return {
      static_cast<float>(read_signed_16(0u)) * kRetailFixedTexcoordScale,
      static_cast<float>(read_signed_16(2u)) * kRetailFixedTexcoordScale,
  };
}

[[nodiscard]] bool CheckedElementCount(const std::uint32_t columns,
                                       const std::uint32_t rows,
                                       std::size_t &count) noexcept {
  if (columns != 0u &&
      static_cast<std::size_t>(rows) >
          std::numeric_limits<std::size_t>::max() /
              static_cast<std::size_t>(columns)) {
    return false;
  }
  count = static_cast<std::size_t>(columns) * rows;
  return true;
}

[[nodiscard]] bool HasCompleteVertexData(
    const data::wmo::WmoLiquidData &liquid,
    const std::size_t vertex_count) noexcept {
  return vertex_count <= std::numeric_limits<std::size_t>::max() / 8u &&
         liquid.vertexData.size() >= vertex_count * 8u;
}

[[nodiscard]] std::int32_t RetailFloatToI32(
    const float value) noexcept {
  const double promoted = static_cast<double>(value);
  if (!std::isfinite(promoted) ||
      promoted < static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
      promoted > static_cast<double>(std::numeric_limits<std::int32_t>::max())) {
    return std::numeric_limits<std::int32_t>::min();
  }
  return static_cast<std::int32_t>(value);
}

constexpr std::array<std::array<std::uint32_t, 2>, 4> kMliqTileCornerOffsets{
    {{0u, 0u}, {0u, 1u}, {1u, 1u}, {1u, 0u}}};

[[nodiscard]] std::array<std::int32_t, 2> ReadVertexRawTextureCoordinates(
    const std::vector<std::uint8_t> &vertex_data,
    const std::size_t vertex_index) noexcept {
  const std::array<float, 2> scaled =
      ReadVertexTextureCoordinates(vertex_data, vertex_index);
  return {static_cast<std::int32_t>(scaled[0] * 256.0f),
          static_cast<std::int32_t>(scaled[1] * 256.0f)};
}

[[nodiscard]] std::array<std::int32_t, 2> LerpRawTextureCoordinates(
    const std::array<std::int32_t, 2> &a, const std::array<std::int32_t, 2> &b,
    const float blend) noexcept {
  return {
      static_cast<std::int32_t>(static_cast<float>(a[0]) +
                                static_cast<float>(b[0] - a[0]) * blend),
      static_cast<std::int32_t>(static_cast<float>(a[1]) +
                                static_cast<float>(b[1] - a[1]) * blend),
  };
}

void BuildWmoGroupLiquidFringe(
    const data::wmo::WmoRoot &root, const data::wmo::WmoGroup &group,
    const Matrix4 &model_matrix, const bool has_authored_texture_coordinates,
    const bool has_depth_lane, const std::uint32_t depth_attenuation_table,
    const WmoSiblingGroupResolver &resolve_sibling_group,
    WaterHeightfield &heightfield) {
  if (!resolve_sibling_group) {
    return;
  }
  const auto &liquid = group.liquid;
  const std::uint32_t tile_columns = liquid.header.xTiles;
  const std::uint32_t tile_rows = liquid.header.yTiles;
  const std::uint32_t vertex_columns = liquid.header.xVerts;
  if (tile_columns == 0u || tile_rows == 0u || vertex_columns == 0u) {
    return;
  }
  std::size_t vertex_count = 0u;
  if (!CheckedElementCount(vertex_columns, liquid.header.yVerts,
                           vertex_count) ||
      !HasCompleteVertexData(liquid, vertex_count)) {
    return;
  }

  const std::size_t portal_ref_begin = group.header.portalStart;
  const std::size_t portal_ref_end =
      portal_ref_begin + group.header.portalCount;
  if (portal_ref_end > root.portalRefs.size()) {
    return;
  }

  LiquidPolygonClipper clipper;
  std::vector<std::array<std::int32_t, 2>> corner_texcoords;
  std::vector<std::array<std::int32_t, 2>> texcoord_memo;
  std::vector<bool> texcoord_memo_valid;

  std::vector<float> corner_depths;
  std::vector<float> depth_memo;
  std::vector<bool> depth_memo_valid;
  const auto lerp_depth = [](const float a, const float b,
                             const float blend) noexcept {
    return a + (b - a) * blend;
  };

  for (std::uint32_t row = 0u; row < tile_rows; ++row) {
    for (std::uint32_t column = 0u; column < tile_columns; ++column) {
      const std::size_t tile_index =
          static_cast<std::size_t>(row) * tile_columns + column;
      if (tile_index >= liquid.tileFlags.size()) {
        return;
      }
      const std::uint8_t flags = liquid.tileFlags[tile_index];
      if (!HasLiquidTileClass(flags) ||
          (flags & kMliqTileClippedFringe) == 0u) {
        continue;
      }

      clipper.Begin();
      corner_texcoords.clear();
      corner_depths.clear();
      bool corners_complete = true;
      for (std::size_t corner = 0u; corner < kMliqTileCornerOffsets.size();
           ++corner) {
        const std::uint32_t corner_column =
            column + kMliqTileCornerOffsets[corner][0];
        const std::uint32_t corner_row =
            row + kMliqTileCornerOffsets[corner][1];
        const std::size_t corner_vertex =
            static_cast<std::size_t>(corner_row) * vertex_columns +
            corner_column;
        if (corner_vertex >= vertex_count) {
          corners_complete = false;
          break;
        }

        const std::array<float, 3> local_position{
            liquid.header.baseCoord[0] +
                static_cast<float>(corner_column) * kLiquidCellSize,
            liquid.header.baseCoord[1] +
                static_cast<float>(corner_row) * kLiquidCellSize,
            ReadVertexHeight(liquid.vertexData, corner_vertex),
        };
        if (!clipper.AddVertex(local_position, static_cast<int>(corner))) {
          corners_complete = false;
          break;
        }
        corner_texcoords.push_back(
            has_authored_texture_coordinates
                ? ReadVertexRawTextureCoordinates(liquid.vertexData,
                                                  corner_vertex)
                : std::array<std::int32_t, 2>{0, 0});
        corner_depths.push_back(
            has_depth_lane
                ? RetailLiquidVertexAttenuation(
                      depth_attenuation_table,
                      ReadVertexDepthByte(liquid.vertexData, corner_vertex))
                : 0.0f);
      }
      if (!corners_complete) {
        continue;
      }
      clipper.Close();

      const float quad_min_x = clipper.vertices()[0].position[0];
      const float quad_min_y = clipper.vertices()[0].position[1];
      const float quad_max_x = clipper.vertices()[2].position[0];
      const float quad_max_y = clipper.vertices()[2].position[1];

      for (std::size_t ref_index = portal_ref_begin;
           ref_index < portal_ref_end; ++ref_index) {
        const data::wmo::WmoPortalRef &reference = root.portalRefs[ref_index];
        const data::wmo::WmoGroup *const neighbour =
            resolve_sibling_group(reference.groupIndex);
        if (neighbour == nullptr) {
          continue;
        }
        const auto &neighbour_liquid = neighbour->liquid.header;
        const float neighbour_max_x =
            neighbour_liquid.baseCoord[0] +
            static_cast<float>(neighbour_liquid.xTiles) * kLiquidCellSize;
        const float neighbour_max_y =
            neighbour_liquid.baseCoord[1] +
            static_cast<float>(neighbour_liquid.yTiles) * kLiquidCellSize;
        if (!(neighbour_liquid.baseCoord[0] < quad_max_x &&
              neighbour_liquid.baseCoord[1] < quad_max_y &&
              quad_min_x < neighbour_max_x && quad_min_y < neighbour_max_y)) {
          continue;
        }
        if (reference.portalIndex >= root.portals.size()) {
          continue;
        }
        const data::wmo::WmoPortal &portal =
            root.portals[reference.portalIndex];
        clipper.ClipToPlane({portal.normal[0], portal.normal[1],
                             portal.normal[2], portal.distance},
                            static_cast<float>(reference.side));
      }

      const std::vector<int> ring = clipper.BuildRing();
      if (ring.size() < 3u) {
        continue;
      }

      texcoord_memo.assign(clipper.vertices().size(),
                           std::array<std::int32_t, 2>{0, 0});
      texcoord_memo_valid.assign(clipper.vertices().size(), false);
      depth_memo.assign(clipper.vertices().size(), 0.0f);
      depth_memo_valid.assign(clipper.vertices().size(), false);

      const auto base_vertex =
          static_cast<std::uint32_t>(heightfield.fringe_vertices.size());
      for (const int ring_vertex : ring) {
        const auto &vertex =
            clipper.vertices()[static_cast<std::size_t>(ring_vertex)];
        LiquidFringeVertex emitted{};
        emitted.position = TransformPoint(
            model_matrix,
            {vertex.position[0], vertex.position[1], vertex.position[2]});

        emitted.normal = {0.0f, 0.0f, 1.0f};
        if (has_authored_texture_coordinates) {
          const auto raw = clipper.ResolveAttribute(
              ring_vertex, corner_texcoords, LerpRawTextureCoordinates,
              texcoord_memo, texcoord_memo_valid);
          emitted.texcoord = {
              static_cast<float>(raw[0]) * kRetailFixedTexcoordScale,
              static_cast<float>(raw[1]) * kRetailFixedTexcoordScale};
        }
        if (has_depth_lane) {
          emitted.depth = clipper.ResolveAttribute(
              ring_vertex, corner_depths, lerp_depth, depth_memo,
              depth_memo_valid);
        }
        heightfield.fringe_vertices.push_back(emitted);
      }

      for (std::uint32_t corner = 1u; corner + 1u < ring.size(); ++corner) {
        heightfield.fringe_indices.push_back(base_vertex);
        heightfield.fringe_indices.push_back(base_vertex + corner + 1u);
        heightfield.fringe_indices.push_back(base_vertex + corner);
      }
    }
  }
}

}

std::uint32_t ResolveWmoGroupLiquidType(const data::wmo::WmoRoot &root,
                                        const data::wmo::WmoGroup &group) {
  std::uint32_t resolved = ResolveLiquidType(
      group.header.flags, NormalizeHeaderLiquidType(root, group));
  if (resolved != 0u) {
    return resolved;
  }

  if (!group.hasLiquid || group.liquid.tileFlags.empty()) {
    return 0u;
  }

  return ResolveLiquidType(group.header.flags,
                           FirstVisibleLiquidType(group.liquid));
}

std::uint32_t ResolveWmoGroupRenderLiquidType(
    const std::uint32_t liquid_type, const std::uint32_t group_flags,
    const std::uint32_t liquid_type_flags) noexcept {

  constexpr std::uint32_t kWmoLiquidTypeWindow = 20u;
  constexpr std::uint32_t kWmoWaterInteriorLiquidType = 17u;
  if (IsInteriorWmoLiquidGroup(group_flags, liquid_type_flags) &&
      liquid_type - 1u < kWmoLiquidTypeWindow &&
      ((liquid_type - 1u) & 3u) == 0u) {
    return kWmoWaterInteriorLiquidType;
  }
  return liquid_type;
}

std::optional<WmoLiquidPointQueryResult>
QueryWmoGroupLiquidAtLocalPosition(
    const data::wmo::WmoGroup &group,
    const std::uint32_t resolved_liquid_type,
    const std::array<float, 3> &local_position,
    const std::uint32_t liquid_type_flags) noexcept {
  if (resolved_liquid_type == 0u) {
    return std::nullopt;
  }

  const auto &liquid = group.liquid;
  const std::uint32_t tile_columns = liquid.header.xTiles;
  const std::uint32_t tile_rows = liquid.header.yTiles;
  if (tile_columns == 0u || tile_rows == 0u) {
    return WmoLiquidPointQueryResult{
        .liquid_type_id = resolved_liquid_type,
        .surface_height = FLT_MAX,
    };
  }

  const float grid_y =
      (local_position[1] - liquid.header.baseCoord[1]) *
      kRetailInverseLiquidCellSize;
  const float grid_x =
      (local_position[0] - liquid.header.baseCoord[0]) *
      kRetailInverseLiquidCellSize;
  const float floored_row = std::floor(grid_y);
  const float floored_column = std::floor(grid_x);
  const std::int32_t row_i32 = RetailFloatToI32(floored_row);
  const std::int32_t column_i32 = RetailFloatToI32(floored_column);
  const std::int32_t tile_rows_i32 =
      std::bit_cast<std::int32_t>(tile_rows);
  const std::int32_t tile_columns_i32 =
      std::bit_cast<std::int32_t>(tile_columns);
  if (row_i32 < 0 || column_i32 < 0 || row_i32 >= tile_rows_i32 ||
      column_i32 >= tile_columns_i32) {
    return std::nullopt;
  }

  const auto row = static_cast<std::uint32_t>(row_i32);
  const auto column = static_cast<std::uint32_t>(column_i32);
  std::size_t tile_count = 0u;
  if (!CheckedElementCount(tile_columns, tile_rows, tile_count)) {
    return std::nullopt;
  }
  const std::size_t tile_index =
      static_cast<std::size_t>(row) * tile_columns + column;
  if (tile_index >= tile_count || tile_index >= liquid.tileFlags.size() ||
      !HasLiquidTileClass(liquid.tileFlags[tile_index])) {
    return std::nullopt;
  }

  if (liquid.header.xVerts == 0u || liquid.header.yVerts == 0u ||
      column + 1u >= liquid.header.xVerts ||
      row + 1u >= liquid.header.yVerts) {
    return std::nullopt;
  }
  std::size_t vertex_count = 0u;
  if (!CheckedElementCount(liquid.header.xVerts, liquid.header.yVerts,
                           vertex_count) ||
      !HasCompleteVertexData(liquid, vertex_count)) {
    return std::nullopt;
  }

  const std::size_t top_left =
      static_cast<std::size_t>(row) * liquid.header.xVerts + column;
  const std::size_t bottom_left = top_left + liquid.header.xVerts;
  if (bottom_left + 1u >= vertex_count) {
    return std::nullopt;
  }

  const float fraction_x = grid_x - floored_column;
  const float fraction_y = grid_y - floored_row;
  const float top_left_height = ReadVertexHeight(liquid.vertexData, top_left);
  const float top_height =
      (ReadVertexHeight(liquid.vertexData, top_left + 1u) -
       top_left_height) *
          fraction_x +
      top_left_height;
  const float bottom_left_height =
      ReadVertexHeight(liquid.vertexData, bottom_left);
  const float bottom_height =
      (ReadVertexHeight(liquid.vertexData, bottom_left + 1u) -
       bottom_left_height) *
          fraction_x +
      bottom_left_height;
  const float surface_height =
      (bottom_height - top_height) * fraction_y + top_height;
  const float comparison_height =
      surface_height + ((liquid_type_flags & 4u) != 0u ? 0.01f : 0.0f);

  if (!(local_position[2] <= comparison_height &&
        comparison_height != local_position[2])) {
    return std::nullopt;
  }

  return WmoLiquidPointQueryResult{
      .liquid_type_id = resolved_liquid_type,
      .surface_height = surface_height,
  };
}

std::optional<WaterHeightfield> BuildWmoGroupLiquidHeightfield(
    const data::wmo::WmoRoot &root, const data::wmo::WmoGroup &group,
    const Matrix4 &model_matrix, const std::uint32_t liquid_type_flags,
    const std::uint32_t liquid_vertex_format,
    const std::uint32_t depth_attenuation_table,
    const WmoSiblingGroupResolver &resolve_sibling_group) {
  if (!group.hasLiquid) {
    return std::nullopt;
  }

  const auto &liquid = group.liquid;
  std::size_t vertex_count = 0u;
  std::size_t tile_count = 0u;
  if (!CheckedElementCount(liquid.header.xVerts, liquid.header.yVerts,
                           vertex_count) ||
      !CheckedElementCount(liquid.header.xTiles, liquid.header.yTiles,
                           tile_count) ||
      vertex_count == 0u || tile_count == 0u ||
      !HasCompleteVertexData(liquid, vertex_count) ||
      liquid.tileFlags.size() < tile_count) {
    return std::nullopt;
  }

  WaterHeightfield heightfield{};

  heightfield.vertex_rows = liquid.header.xVerts;
  heightfield.vertex_cols = liquid.header.yVerts;
  heightfield.depth_level = 2u;
  heightfield.authored_wmo = true;

  std::uint32_t liquid_type = ResolveWmoGroupLiquidType(root, group);
  if (liquid_type == 0u) {
    liquid_type = 1u;
  }
  heightfield.liquid_type_id = liquid_type;
  heightfield.render_liquid_type_id = ResolveWmoGroupRenderLiquidType(
      liquid_type, group.header.flags, liquid_type_flags);

  constexpr std::uint32_t kLiquidVertexFormatAuthoredTexcoords = 1u;
  const bool has_authored_texture_coordinates =
      liquid_vertex_format == kLiquidVertexFormatAuthoredTexcoords;

  const bool has_depth_lane =
      liquid_vertex_format == 0u || liquid_vertex_format == 2u;

  heightfield.vertex_color =
      ResolveWmoLiquidVertexColor(root, group, liquid_type_flags);

  heightfield.depth_lane_u =
      IsInteriorWmoLiquidGroup(group.header.flags, liquid_type_flags) ? 1.0f
                                                                      : 0.0f;

  heightfield.heights.reserve(vertex_count);
  heightfield.positions.reserve(vertex_count);
  if (has_authored_texture_coordinates) {
    heightfield.vertex_texcoords.reserve(vertex_count);
  }
  if (has_depth_lane) {
    heightfield.vertex_depths.reserve(vertex_count);
  }

  for (std::uint32_t x_index = 0; x_index < liquid.header.xVerts; ++x_index) {
    for (std::uint32_t y_index = 0; y_index < liquid.header.yVerts; ++y_index) {
      const std::size_t vertex_index =
          static_cast<std::size_t>(y_index) * liquid.header.xVerts + x_index;
      const float local_height = ReadVertexHeight(liquid.vertexData, vertex_index);

      const Vec3 world_position = TransformPoint(
          model_matrix,
          {liquid.header.baseCoord[0] +
               static_cast<float>(x_index) * kLiquidCellSize,
           liquid.header.baseCoord[1] +
               static_cast<float>(y_index) * kLiquidCellSize,
           local_height});

      if (heightfield.positions.empty()) {
        heightfield.bounds_min = world_position;
        heightfield.bounds_max = world_position;
      } else {
        for (std::size_t axis = 0; axis < 3u; ++axis) {
          heightfield.bounds_min[axis] =
              std::min(heightfield.bounds_min[axis], world_position[axis]);
          heightfield.bounds_max[axis] =
              std::max(heightfield.bounds_max[axis], world_position[axis]);
        }
      }
      heightfield.positions.push_back(world_position);
      heightfield.heights.push_back(world_position[2]);
      if (has_authored_texture_coordinates) {

        heightfield.vertex_texcoords.push_back(
            ReadVertexTextureCoordinates(liquid.vertexData, vertex_index));
      }
      if (has_depth_lane) {
        heightfield.vertex_depths.push_back(RetailLiquidVertexAttenuation(
            depth_attenuation_table,
            ReadVertexDepthByte(liquid.vertexData, vertex_index)));
      }
    }
  }

  heightfield.render_mask.reserve(tile_count);
  bool has_visible_cell = false;
  for (std::uint32_t x_tile = 0; x_tile < liquid.header.xTiles; ++x_tile) {
    for (std::uint32_t y_tile = 0; y_tile < liquid.header.yTiles; ++y_tile) {
      const std::size_t tile_index =
          static_cast<std::size_t>(y_tile) * liquid.header.xTiles + x_tile;
      const std::uint8_t visible = static_cast<std::uint8_t>(
          IsLiquidBodyTile(liquid.tileFlags[tile_index]));
      heightfield.render_mask.push_back(visible);
      has_visible_cell = has_visible_cell || visible != 0u;
    }
  }

  BuildWmoGroupLiquidFringe(root, group, model_matrix,
                            has_authored_texture_coordinates,
                            has_depth_lane, depth_attenuation_table,
                            resolve_sibling_group, heightfield);

  if (!has_visible_cell && heightfield.fringe_indices.empty()) {
    return std::nullopt;
  }

  return heightfield;
}

bool IsWmoLiquidVisibleThroughGroupPaths(
    const WaterHeightfield& heightfield, const std::uint16_t group_index,
    const std::span<const WmoVisibleGroupPath> visible_group_paths,
    const Matrix4& world_view_projection) noexcept {
  if (heightfield.positions.empty()) {
    return false;
  }

  const Vec3& bounds_min = heightfield.bounds_min;
  const Vec3& bounds_max = heightfield.bounds_max;

  return std::any_of(
      visible_group_paths.begin(), visible_group_paths.end(),
      [&](const WmoVisibleGroupPath& visible_path) {
        return visible_path.group_index == group_index &&
               IsWmoBoundsVisibleInPortalClip(
                   bounds_min, bounds_max, world_view_projection,
                   visible_path.clip_rect);
      });
}

}
