#pragma once

#include "openwow/ui/framexml/ui_frame.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace openwow::ui::framexml {

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

inline std::optional<BackdropSliceUvRect> ComputeBackdropSliceUvRect(
    TextureSlice slice,
    int slice_edge_px,
    int tex_w,
    int tex_h) {
  using Slice = TextureSlice;
  if (slice == Slice::kNone || slice_edge_px <= 0 || tex_w <= 0 ||
      tex_h <= 0) {
    return std::nullopt;
  }

  const float texture_width = static_cast<float>(tex_w);
  const float texture_height = static_cast<float>(tex_h);
  const int edge = slice_edge_px;
  const bool horizontal_strip = (tex_h == edge) && (tex_w == edge * 8);
  const bool vertical_strip = (tex_w == edge) && (tex_h == edge * 8);
  if (horizontal_strip || vertical_strip) {
    int index = -1;
    switch (slice) {
      case Slice::kBackdropLeft:
        index = 0;
        break;
      case Slice::kBackdropRight:
        index = 1;
        break;
      case Slice::kBackdropTop:
        index = 2;
        break;
      case Slice::kBackdropBottom:
        index = 3;
        break;
      case Slice::kBackdropTopLeft:
        index = 4;
        break;
      case Slice::kBackdropTopRight:
        index = 5;
        break;
      case Slice::kBackdropBottomLeft:
        index = 6;
        break;
      case Slice::kBackdropBottomRight:
        index = 7;
        break;
      case Slice::kNone:
        break;
    }
    if (index < 0) {
      return std::nullopt;
    }

    const int pad = std::clamp(edge / 16, 0, std::max(0, edge / 2 - 1));
    const int inset = std::max(1, edge - 2 * pad);
    BackdropSliceUvRect rect;
    if (horizontal_strip) {
      rect.u0 = static_cast<float>(index * edge + pad) / texture_width;
      rect.v0 = static_cast<float>(pad) / texture_height;
      rect.u1 =
          static_cast<float>(index * edge + pad + inset) / texture_width;
      rect.v1 = static_cast<float>(pad + inset) / texture_height;
    } else {
      rect.u0 = static_cast<float>(pad) / texture_width;
      rect.v0 = static_cast<float>(index * edge + pad) / texture_height;
      rect.u1 = static_cast<float>(pad + inset) / texture_width;
      rect.v1 =
          static_cast<float>(index * edge + pad + inset) / texture_height;
    }
    return rect;
  }

  const int right_x = std::max(0, tex_w - edge);
  const int bottom_y = std::max(0, tex_h - edge);
  int source_x = 0;
  int source_y = 0;
  int source_w = tex_w;
  int source_h = tex_h;
  switch (slice) {
    case Slice::kBackdropTopLeft:
      source_w = std::min(edge, tex_w);
      source_h = std::min(edge, tex_h);
      break;
    case Slice::kBackdropTop:
      source_x = edge;
      source_w = std::max(0, tex_w - 2 * edge);
      source_h = std::min(edge, tex_h);
      break;
    case Slice::kBackdropTopRight:
      source_x = right_x;
      source_w = std::min(edge, tex_w);
      source_h = std::min(edge, tex_h);
      break;
    case Slice::kBackdropLeft:
      source_y = edge;
      source_w = std::min(edge, tex_w);
      source_h = std::max(0, tex_h - 2 * edge);
      break;
    case Slice::kBackdropRight:
      source_x = right_x;
      source_y = edge;
      source_w = std::min(edge, tex_w);
      source_h = std::max(0, tex_h - 2 * edge);
      break;
    case Slice::kBackdropBottomLeft:
      source_y = bottom_y;
      source_w = std::min(edge, tex_w);
      source_h = std::min(edge, tex_h);
      break;
    case Slice::kBackdropBottom:
      source_x = edge;
      source_y = bottom_y;
      source_w = std::max(0, tex_w - 2 * edge);
      source_h = std::min(edge, tex_h);
      break;
    case Slice::kBackdropBottomRight:
      source_x = right_x;
      source_y = bottom_y;
      source_w = std::min(edge, tex_w);
      source_h = std::min(edge, tex_h);
      break;
    case Slice::kNone:
      break;
  }
  if (source_w <= 0 || source_h <= 0) {
    return std::nullopt;
  }

  return BackdropSliceUvRect{
      static_cast<float>(source_x) / texture_width,
      static_cast<float>(source_y) / texture_height,
      static_cast<float>(source_x + source_w) / texture_width,
      static_cast<float>(source_y + source_h) / texture_height,
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

  const int source_x = static_cast<int>(std::lround(uv_rect->u0 * tex_w));
  const int source_y = static_cast<int>(std::lround(uv_rect->v0 * tex_h));
  const int source_w = static_cast<int>(
      std::lround((uv_rect->u1 - uv_rect->u0) * tex_w));
  const int source_h = static_cast<int>(
      std::lround((uv_rect->v1 - uv_rect->v0) * tex_h));
  if (source_w <= 0 || source_h <= 0) {
    return std::nullopt;
  }

  return BackdropSlicePixelRect{source_x, source_y, source_w, source_h};
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

  const int edge = slice_edge_px;
  if (tex_w != edge * 8 || tex_h != edge) {
    return std::nullopt;
  }

  int index = -1;
  switch (slice) {
    case Slice::kBackdropLeft:
      index = 0;
      break;
    case Slice::kBackdropRight:
      index = 1;
      break;
    case Slice::kBackdropTop:
      index = 2;
      break;
    case Slice::kBackdropBottom:
      index = 3;
      break;
    case Slice::kBackdropTopLeft:
    case Slice::kBackdropTopRight:
    case Slice::kBackdropBottomLeft:
    case Slice::kBackdropBottomRight:
    case Slice::kNone:
      return std::nullopt;
  }

  const int pad = std::clamp(edge / 16, 0, std::max(0, edge / 2 - 1));
  const int inset = std::max(1, edge - 2 * pad);
  const float u0 = static_cast<float>(index * edge + pad) / static_cast<float>(tex_w);
  const float u1 =
      static_cast<float>(index * edge + pad + inset) / static_cast<float>(tex_w);
  const float v0 = static_cast<float>(pad) / static_cast<float>(tex_h);
  const float v1 = repeat_tiles - v0;

  if (slice == Slice::kBackdropTop || slice == Slice::kBackdropBottom) {
    return BackdropSliceUvQuad{
        .upper_left = {u0, v1},
        .lower_left = {u1, v1},
        .upper_right = {u0, v0},
        .lower_right = {u1, v0},
    };
  }

  return BackdropSliceUvQuad{
      .upper_left = {u0, v0},
      .lower_left = {u0, v1},
      .upper_right = {u1, v0},
      .lower_right = {u1, v1},
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
