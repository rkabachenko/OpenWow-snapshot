#pragma once

#include "openwow/ui/framexml/ui_frame.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace openwow::ui::framexml {

inline constexpr int kBackdropStripTileCount = 8;

inline constexpr float kBackdropStripTileInset = 1.0f / 16.0f;

inline constexpr int kBackdropStripTileInsetDivisor = 16;

struct BackdropSliceUvRect {
  float u0{0.0f};
  float v0{0.0f};
  float u1{1.0f};
  float v1{1.0f};
};

struct BackdropSliceUvPoint {
  float u{0.0f};
  float v{0.0f};
};

struct BackdropSliceUvQuad {
  BackdropSliceUvPoint upper_left{};
  BackdropSliceUvPoint lower_left{};
  BackdropSliceUvPoint upper_right{};
  BackdropSliceUvPoint lower_right{};

  [[nodiscard]] std::array<BackdropSliceUvPoint, 4> ToUiRendererOrder() const {
    return {upper_left, upper_right, lower_right, lower_left};
  }
};

struct BackdropSlicePixelRect {
  int x{0};
  int y{0};
  int width{0};
  int height{0};
};

inline constexpr int BackdropStripTileIndex(TextureSlice slice) {
  switch (slice) {
    case TextureSlice::kBackdropLeft:
      return 0;
    case TextureSlice::kBackdropRight:
      return 1;
    case TextureSlice::kBackdropTop:
      return 2;
    case TextureSlice::kBackdropBottom:
      return 3;
    case TextureSlice::kBackdropTopLeft:
      return 4;
    case TextureSlice::kBackdropTopRight:
      return 5;
    case TextureSlice::kBackdropBottomLeft:
      return 6;
    case TextureSlice::kBackdropBottomRight:
      return 7;
    case TextureSlice::kNone:
      break;
  }
  return -1;
}

inline constexpr float BackdropStripTileAxisMin(int index) {
  return (static_cast<float>(index) + kBackdropStripTileInset) /
         static_cast<float>(kBackdropStripTileCount);
}

inline constexpr float BackdropStripTileAxisMax(int index) {
  return (static_cast<float>(index + 1) - kBackdropStripTileInset) /
         static_cast<float>(kBackdropStripTileCount);
}

inline std::optional<BackdropSliceUvRect> ComputeBackdropSliceUvRect(
    TextureSlice slice,
    int slice_edge_px,
    int tex_w,
    int tex_h) {

  if (slice == TextureSlice::kNone || slice_edge_px <= 0 || tex_w <= 0 ||
      tex_h <= 0) {
    return std::nullopt;
  }

  const int index = BackdropStripTileIndex(slice);
  if (index < 0) {
    return std::nullopt;
  }

  return BackdropSliceUvRect{
      BackdropStripTileAxisMin(index),
      kBackdropStripTileInset,
      BackdropStripTileAxisMax(index),
      1.0f - kBackdropStripTileInset,
  };
}

inline std::optional<BackdropSlicePixelRect> ComputeBackdropSlicePixelRect(
    TextureSlice slice,
    int slice_edge_px,
    int tex_w,
    int tex_h) {
  const auto uv_rect =
      ComputeBackdropSliceUvRect(slice, slice_edge_px, tex_w, tex_h);
  if (!uv_rect.has_value()) {
    return std::nullopt;
  }

  const int tile_w = std::max(1, tex_w / kBackdropStripTileCount);
  const int pad_x = tile_w / kBackdropStripTileInsetDivisor;
  const int pad_y = tex_h / kBackdropStripTileInsetDivisor;
  const int index = BackdropStripTileIndex(slice);
  return BackdropSlicePixelRect{
      index * tile_w + pad_x,
      pad_y,
      std::max(1, tile_w - 2 * pad_x),
      std::max(1, tex_h - 2 * pad_y),
  };
}

inline std::optional<BackdropSliceUvQuad> ComputeBackdropStripEdgeUvQuad(
    TextureSlice slice,
    int slice_edge_px,
    int tex_w,
    int tex_h,
    float repeat_tiles) {
  using Slice = TextureSlice;
  if (slice_edge_px <= 0 || tex_w <= 0 || tex_h <= 0 ||
      !std::isfinite(repeat_tiles)) {
    return std::nullopt;
  }

  switch (slice) {
    case Slice::kBackdropLeft:
    case Slice::kBackdropRight:
    case Slice::kBackdropTop:
    case Slice::kBackdropBottom:
      break;
    case Slice::kBackdropTopLeft:
    case Slice::kBackdropTopRight:
    case Slice::kBackdropBottomLeft:
    case Slice::kBackdropBottomRight:
    case Slice::kNone:

      return std::nullopt;
  }
  const int index = BackdropStripTileIndex(slice);

  const float tile_min = BackdropStripTileAxisMin(index);
  const float tile_max = BackdropStripTileAxisMax(index);
  const float repeat_min = kBackdropStripTileInset;
  const float repeat_max = repeat_tiles - kBackdropStripTileInset;

  if (slice == Slice::kBackdropTop || slice == Slice::kBackdropBottom) {
    return BackdropSliceUvQuad{
        .upper_left = {tile_min, repeat_max},
        .lower_left = {tile_max, repeat_max},
        .upper_right = {tile_min, repeat_min},
        .lower_right = {tile_max, repeat_min},
    };
  }

  return BackdropSliceUvQuad{
      .upper_left = {tile_min, repeat_min},
      .lower_left = {tile_min, repeat_max},
      .upper_right = {tile_max, repeat_min},
      .lower_right = {tile_max, repeat_max},
  };
}

inline float BackdropEdgeRepeatTiles(TextureSlice slice, float dst_w, float dst_h) {
  using Slice = TextureSlice;
  switch (slice) {
    case Slice::kBackdropTop:
    case Slice::kBackdropBottom:
      return dst_h > 0.0f ? dst_w / dst_h : 0.0f;
    case Slice::kBackdropLeft:
    case Slice::kBackdropRight:
      return dst_w > 0.0f ? dst_h / dst_w : 0.0f;
    case Slice::kBackdropTopLeft:
    case Slice::kBackdropTopRight:
    case Slice::kBackdropBottomLeft:
    case Slice::kBackdropBottomRight:
    case Slice::kNone:
      return 0.0f;
  }
  return 0.0f;
}

}
