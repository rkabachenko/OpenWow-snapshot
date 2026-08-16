#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace openwow::ui::game {

struct LoadingScreenWorldTileQuad {
  float left = 0.0f;
  float top = 0.0f;
  float right = 0.0f;
  float bottom = 0.0f;
  float u0 = 0.0f;
  float v0 = 0.0f;
  float u1 = 0.0f;
  float v1 = 0.0f;
};

inline constexpr std::size_t kLoadingScreenWorldTileColumns = 4;
inline constexpr std::size_t kLoadingScreenWorldTileRows = 3;
inline constexpr std::size_t kLoadingScreenWorldTileCount =
    kLoadingScreenWorldTileColumns * kLoadingScreenWorldTileRows;
inline constexpr float kLoadingScreenWorldTileSize = 256.0f;
inline constexpr float kLoadingScreenWorldBackdropWidth = 1002.0f;
inline constexpr float kLoadingScreenWorldBackdropHeight = 668.0f;
inline constexpr std::string_view kLoadingScreenDynamicElementsTexturePath =
    "Interface\\Glues\\LoadingScreens\\DynamicElements";

inline constexpr std::array<LoadingScreenWorldTileQuad,
                            kLoadingScreenWorldTileCount>
BuildLoadingScreenWorldTileLayout() {
  std::array<LoadingScreenWorldTileQuad, kLoadingScreenWorldTileCount> quads{};

  for (std::size_t row = 0; row < kLoadingScreenWorldTileRows; ++row) {
    for (std::size_t column = 0; column < kLoadingScreenWorldTileColumns;
         ++column) {
      const std::size_t index = row * kLoadingScreenWorldTileColumns + column;
      const float left_px = static_cast<float>(column) * kLoadingScreenWorldTileSize;
      const float top_px = static_cast<float>(row) * kLoadingScreenWorldTileSize;
      const float unclamped_right = left_px + kLoadingScreenWorldTileSize;
      const float unclamped_bottom = top_px + kLoadingScreenWorldTileSize;
      const float right_px = unclamped_right > kLoadingScreenWorldBackdropWidth
          ? kLoadingScreenWorldBackdropWidth
          : unclamped_right;
      const float bottom_px = unclamped_bottom > kLoadingScreenWorldBackdropHeight
          ? kLoadingScreenWorldBackdropHeight
          : unclamped_bottom;

      quads[index] = {
          .left = left_px / kLoadingScreenWorldBackdropWidth,
          .top = top_px / kLoadingScreenWorldBackdropHeight,
          .right = right_px / kLoadingScreenWorldBackdropWidth,
          .bottom = bottom_px / kLoadingScreenWorldBackdropHeight,
          .u0 = 0.0f,
          .v0 = 0.0f,
          .u1 = (right_px - left_px) / kLoadingScreenWorldTileSize,
          .v1 = (bottom_px - top_px) / kLoadingScreenWorldTileSize,
      };
    }
  }

  return quads;
}

inline constexpr auto kLoadingScreenWorldTileLayout =
    BuildLoadingScreenWorldTileLayout();

inline std::string BuildLoadingScreenWorldTilePath(std::size_t tile_index) {
  return "Interface\\WorldMap\\World\\World"
      + std::to_string(tile_index + 1) + ".blp";
}

inline std::string BuildLoadingScreenTextureArchivePath(
    std::string_view texture_path) {
  if (texture_path.size() >= 4) {
    const auto suffix = texture_path.substr(texture_path.size() - 4);
    if (suffix == ".blp" || suffix == ".BLP") {
      return std::string(texture_path);
    }
  }

  std::string archive_path(texture_path);
  archive_path += ".blp";
  return archive_path;
}

template <typename Probe>
bool ProbeLoadingScreenDynamicMapChangeTextureSet(Probe&& probe) {
  if (!probe(BuildLoadingScreenTextureArchivePath(
          kLoadingScreenDynamicElementsTexturePath))) {
    return false;
  }

  for (std::size_t tile_index = 0; tile_index < kLoadingScreenWorldTileCount;
       ++tile_index) {
    if (!probe(BuildLoadingScreenWorldTilePath(tile_index))) {
      return false;
    }
  }

  return true;
}

}
