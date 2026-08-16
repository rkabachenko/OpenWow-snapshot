#pragma once

#include "openwow/ui/game/framescript/widgets/minimap_texture_uv.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <vector>

namespace openwow::ui::game {

inline constexpr float kMinimapTileSize = 256.0f;
inline constexpr float kMinimapTileGridCenterOffset = 32.0f * kMinimapTileSize;
inline constexpr float kMinimapBaseVisibleWorldRange = 150.0f;

struct MinimapTileCoords {
  int x{0};
  int y{0};
};

struct MinimapTileWorldBounds {
  float min_x{0.0f};
  float max_x{0.0f};
  float min_y{0.0f};
  float max_y{0.0f};
};

struct MinimapTileQuadVertex {
  float world_x{0.0f};
  float world_y{0.0f};
  float u{0.0f};
  float v{0.0f};
};

struct MinimapTileQuad {
  std::array<MinimapTileQuadVertex, 4> vertices{};
};

inline float ComputeMinimapVisibleWorldRange(float zoom) noexcept {
  return kMinimapBaseVisibleWorldRange / std::clamp(zoom, 0.5f, 4.0f);
}

inline MinimapTileCoords WorldToMinimapTileCoords(float world_x,
                                                  float world_y) noexcept {
  return {
      static_cast<int>(std::floor(
          (kMinimapTileGridCenterOffset - world_y) / kMinimapTileSize)),
      static_cast<int>(std::floor(
          (kMinimapTileGridCenterOffset - world_x) / kMinimapTileSize)),
  };
}

inline MinimapTileWorldBounds MinimapTileCoordsToWorldBounds(int tile_x,
                                                             int tile_y) noexcept {
  const float max_y = kMinimapTileGridCenterOffset
                      - static_cast<float>(tile_x) * kMinimapTileSize;
  const float min_y = max_y - kMinimapTileSize;
  const float max_x = kMinimapTileGridCenterOffset
                      - static_cast<float>(tile_y) * kMinimapTileSize;
  const float min_x = max_x - kMinimapTileSize;
  return {min_x, max_x, min_y, max_y};
}

inline std::optional<MinimapTileQuad> BuildVisibleMinimapTileQuad(
    const MinimapTileWorldBounds& tile_bounds, float center_x, float center_y,
    float half_world_range) noexcept {
  const float view_min_x = center_x - half_world_range;
  const float view_max_x = center_x + half_world_range;
  const float view_min_y = center_y - half_world_range;
  const float view_max_y = center_y + half_world_range;

  const float clipped_min_x = std::max(view_min_x, tile_bounds.min_x);
  const float clipped_max_x = std::min(view_max_x, tile_bounds.max_x);
  const float clipped_min_y = std::max(view_min_y, tile_bounds.min_y);
  const float clipped_max_y = std::min(view_max_y, tile_bounds.max_y);
  if (clipped_min_x >= clipped_max_x || clipped_min_y >= clipped_max_y) {
    return std::nullopt;
  }

  MinimapTileQuad quad;
  quad.vertices = {{
      {clipped_max_x, clipped_max_y},
      {clipped_max_x, clipped_min_y},
      {clipped_min_x, clipped_min_y},
      {clipped_min_x, clipped_max_y},
  }};

  for (auto& vertex : quad.vertices) {
    vertex.u = (tile_bounds.max_y - vertex.world_y) / kMinimapTileSize;
    vertex.v = (tile_bounds.max_x - vertex.world_x) / kMinimapTileSize;
  }

  return quad;
}

inline std::optional<MinimapTileQuad> BuildVisibleMinimapTileRenderQuad(
    const MinimapTileWorldBounds& tile_bounds, float center_x, float center_y,
    float half_world_range, std::uint32_t texture_width,
    std::uint32_t texture_height) noexcept {
  auto quad =
      BuildVisibleMinimapTileQuad(tile_bounds, center_x, center_y,
                                  half_world_range);
  if (!quad.has_value()) {
    return std::nullopt;
  }

  for (auto& vertex : quad->vertices) {
    vertex.u = ContractNormalizedTextureCoordForHalfTexel(vertex.u,
                                                          texture_width);
    vertex.v = ContractNormalizedTextureCoordForHalfTexel(vertex.v,
                                                          texture_height);
  }

  return quad;
}

inline std::vector<MinimapTileCoords> EnumerateVisibleMinimapTiles(
    float player_x, float player_y, float zoom) {
  const float world_range = ComputeMinimapVisibleWorldRange(zoom);
  const float min_x = player_x - world_range;
  const float max_x = player_x + world_range;
  const float min_y = player_y - world_range;
  const float max_y = player_y + world_range;

  const int tile_x_min = static_cast<int>(std::floor(
      (kMinimapTileGridCenterOffset - max_y) / kMinimapTileSize));
  const int tile_x_max = static_cast<int>(std::floor(
      (kMinimapTileGridCenterOffset - min_y) / kMinimapTileSize));
  const int tile_y_min = static_cast<int>(std::floor(
      (kMinimapTileGridCenterOffset - max_x) / kMinimapTileSize));
  const int tile_y_max = static_cast<int>(std::floor(
      (kMinimapTileGridCenterOffset - min_x) / kMinimapTileSize));

  std::vector<MinimapTileCoords> tiles;
  tiles.reserve(static_cast<std::size_t>(tile_x_max - tile_x_min + 1)
                * static_cast<std::size_t>(tile_y_max - tile_y_min + 1));
  for (int tile_x = tile_x_min; tile_x <= tile_x_max; ++tile_x) {
    for (int tile_y = tile_y_min; tile_y <= tile_y_max; ++tile_y) {
      tiles.push_back({tile_x, tile_y});
    }
  }
  return tiles;
}

}
