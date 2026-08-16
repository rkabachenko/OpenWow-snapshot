#pragma once

#include <array>

namespace openwow::ui::game {

struct TextureQuadUvPoint {
  float u{0.0f};
  float v{0.0f};
};

struct TextureQuadUv {
  TextureQuadUvPoint upper_left{0.0f, 0.0f};
  TextureQuadUvPoint lower_left{0.0f, 1.0f};
  TextureQuadUvPoint upper_right{1.0f, 0.0f};
  TextureQuadUvPoint lower_right{1.0f, 1.0f};

  [[nodiscard]] static constexpr TextureQuadUv FromRect(float left, float right, float top,
                                                        float bottom) noexcept {
    return {
        .upper_left = {left, top},
        .lower_left = {left, bottom},
        .upper_right = {right, top},
        .lower_right = {right, bottom},
    };
  }

  [[nodiscard]] constexpr std::array<TextureQuadUvPoint, 4> ToUiRendererOrder() const noexcept {
    return {
        upper_left,
        upper_right,
        lower_right,
        lower_left,
    };
  }
};

inline void ApplyCSimpleTextureTileExtent(TextureQuadUv& quad, const bool tile_x,
                                          const bool tile_y, const float pixel_width,
                                          const float pixel_height, const int texture_width,
                                          const int texture_height,
                                          const int tile_period_x = 0,
                                          const int tile_period_y = 0) noexcept {
  const int horizontal_period = tile_period_x > 0 ? tile_period_x : texture_width;
  const int vertical_period = tile_period_y > 0 ? tile_period_y : texture_height;

  if (tile_x && horizontal_period > 0 && pixel_width > 0.0f) {
    const float tiled_u = pixel_width / static_cast<float>(horizontal_period);
    if (quad.upper_left.u != 0.0f) {
      quad.upper_left.u = tiled_u;
    }
    if (quad.lower_left.u != 0.0f) {
      quad.lower_left.u = tiled_u;
    }
    if (quad.upper_right.u != 0.0f) {
      quad.upper_right.u = tiled_u;
    }
    if (quad.lower_right.u != 0.0f) {
      quad.lower_right.u = tiled_u;
    }
  }

  if (tile_y && vertical_period > 0 && pixel_height > 0.0f) {
    const float tiled_v = pixel_height / static_cast<float>(vertical_period);
    if (quad.upper_left.v != 0.0f) {
      quad.upper_left.v = tiled_v;
    }
    if (quad.lower_left.v != 0.0f) {
      quad.lower_left.v = tiled_v;
    }
    if (quad.upper_right.v != 0.0f) {
      quad.upper_right.v = tiled_v;
    }
    if (quad.lower_right.v != 0.0f) {
      quad.lower_right.v = tiled_v;
    }
  }
}

}
