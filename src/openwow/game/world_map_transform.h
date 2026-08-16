#pragma once

#include <cstdint>

namespace openwow::game {

struct AppliedWorldMapTransform {
  std::uint32_t map_id = 0;
  float x = 0.0f;
  float y = 0.0f;
  std::int32_t dungeon_map_id = 0;
  bool transformed = false;
};

struct WorldMapTransformRule {
  std::uint32_t map_id = 0;
  float region_min_x = 0.0f;
  float region_min_y = 0.0f;
  float region_max_x = 0.0f;
  float region_max_y = 0.0f;
  std::uint32_t new_map_id = 0;
  float region_offset_x = 0.0f;
  float region_offset_y = 0.0f;
  std::int32_t new_dungeon_map_id = 0;
};

template <typename TransformRange>
[[nodiscard]] AppliedWorldMapTransform ApplyWorldMapTransform(
    const TransformRange& transforms, std::uint32_t map_id, float x, float y) {
  AppliedWorldMapTransform result;
  result.map_id = map_id;
  result.x = x;
  result.y = y;

  for (const auto& transform : transforms) {
    if (transform.map_id != map_id) {
      continue;
    }
    if (transform.region_min_x > x || transform.region_max_x < x) {
      continue;
    }
    if (y < transform.region_min_y || y > transform.region_max_y) {
      continue;
    }

    result.map_id = transform.new_map_id;
    result.x += transform.region_offset_x;
    result.y += transform.region_offset_y;
    result.dungeon_map_id = transform.new_dungeon_map_id;
    result.transformed = true;
    return result;
  }

  return result;
}

}
