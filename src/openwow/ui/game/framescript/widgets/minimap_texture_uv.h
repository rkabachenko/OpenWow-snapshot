#pragma once

#include <algorithm>
#include <cstdint>

namespace openwow::ui::game {

struct MinimapAtlasUvQuad {
  float lower_left_u{0.0f};
  float lower_left_v{0.0f};
  float lower_right_u{0.0f};
  float lower_right_v{0.0f};
  float upper_left_u{0.0f};
  float upper_left_v{0.0f};
  float upper_right_u{0.0f};
  float upper_right_v{0.0f};
};

using MinimapPoiAtlasUvQuad = MinimapAtlasUvQuad;

constexpr std::uint32_t ComputeFloorIntegerSquareRoot(
    std::uint32_t value) noexcept {
  std::uint32_t result = 0;
  std::uint32_t bit = 1u << 30;

  while (bit > value) {
    bit >>= 2;
  }

  while (bit != 0) {
    if (value >= result + bit) {
      value -= result + bit;
      result = (result >> 1) + bit;
    } else {
      result >>= 1;
    }
    bit >>= 2;
  }

  return result;
}

inline constexpr std::uint32_t kMinimapPoiAtlasIconCount = 196;
inline constexpr std::uint32_t kMinimapPoiAtlasCellSpanPixels = 18;
inline constexpr std::uint32_t kMinimapPoiAtlasBorderPixels = 1;
inline constexpr std::uint32_t kMinimapPoiAtlasColumns =
    ComputeFloorIntegerSquareRoot(kMinimapPoiAtlasIconCount);
inline constexpr std::uint32_t kMinimapObjectIconAtlasIconCount = 16;
inline constexpr std::uint32_t kMinimapObjectIconAtlasColumns = 8;
inline constexpr std::uint32_t kMinimapObjectIconAtlasRows = 2;
inline constexpr std::uint32_t kMinimapPartyRaidBlipAtlasIconCount = 32;
inline constexpr std::uint32_t kMinimapPartyRaidBlipAtlasColumns = 8;
inline constexpr std::uint32_t kMinimapPartyRaidBlipAtlasRows = 4;

static_assert(kMinimapPoiAtlasColumns == 14);

inline MinimapPoiAtlasUvQuad ComputeMinimapPoiAtlasUvQuad(
    std::uint32_t icon_index,
    std::uint32_t texture_extent_pixels) noexcept {
  if (texture_extent_pixels == 0) {
    return {};
  }

  const auto column = icon_index % kMinimapPoiAtlasColumns;
  const auto row = icon_index / kMinimapPoiAtlasColumns;
  const float inverse_extent = 1.0f / static_cast<float>(texture_extent_pixels);
  const float border =
      inverse_extent * static_cast<float>(kMinimapPoiAtlasBorderPixels);
  const float cell_span =
      inverse_extent * static_cast<float>(kMinimapPoiAtlasCellSpanPixels);
  const float left = border + static_cast<float>(column) * cell_span;
  const float top = border + static_cast<float>(row) * cell_span;
  const float right = left + cell_span - border;
  const float bottom = top + cell_span - border;

  return {.lower_left_u = left,
          .lower_left_v = bottom,
          .lower_right_u = right,
          .lower_right_v = bottom,
          .upper_left_u = left,
          .upper_left_v = top,
          .upper_right_u = right,
          .upper_right_v = top};
}

inline MinimapAtlasUvQuad ComputeMinimapUniformGridAtlasUvQuad(
    std::uint32_t icon_index,
    std::uint32_t columns,
    std::uint32_t rows) noexcept {
  if (columns == 0 || rows == 0) {
    return {};
  }

  const auto column = icon_index % columns;
  const auto row = icon_index / columns;
  const float cell_width = 1.0f / static_cast<float>(columns);
  const float cell_height = 1.0f / static_cast<float>(rows);
  const float left = static_cast<float>(column) * cell_width;
  const float top = static_cast<float>(row) * cell_height;
  const float right = left + cell_width;
  const float bottom = top + cell_height;

  return {.lower_left_u = left,
          .lower_left_v = bottom,
          .lower_right_u = right,
          .lower_right_v = bottom,
          .upper_left_u = left,
          .upper_left_v = top,
          .upper_right_u = right,
          .upper_right_v = top};
}

inline MinimapAtlasUvQuad ComputeMinimapObjectIconUvQuad(
    std::uint32_t icon_index) noexcept {
  return ComputeMinimapUniformGridAtlasUvQuad(
      icon_index, kMinimapObjectIconAtlasColumns, kMinimapObjectIconAtlasRows);
}

inline MinimapAtlasUvQuad ComputeMinimapPartyRaidBlipUvQuad(
    std::uint32_t icon_index) noexcept {
  return ComputeMinimapUniformGridAtlasUvQuad(
      icon_index, kMinimapPartyRaidBlipAtlasColumns,
      kMinimapPartyRaidBlipAtlasRows);
}

inline float ContractNormalizedTextureCoordForHalfTexel(
    float coord, std::uint32_t extent) noexcept {
  if (extent == 0) {
    return coord;
  }

  const float clamped = std::clamp(coord, 0.0f, 1.0f);
  const float half_texel = 0.5f / static_cast<float>(extent);
  return half_texel + clamped * (1.0f - 2.0f * half_texel);
}

}
