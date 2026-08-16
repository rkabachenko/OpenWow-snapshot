#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>

namespace openwow::render {

struct TextureSurfaceCopyRect {
  std::int32_t left{0};
  std::int32_t top{0};
  std::int32_t right{0};
  std::int32_t bottom{0};

  [[nodiscard]] constexpr std::int32_t Width() const {
    return right - left;
  }

  [[nodiscard]] constexpr std::int32_t Height() const {
    return bottom - top;
  }
};

enum class TextureSurfaceCopyFilter : std::uint8_t {
  kNone = 0,
  kLinear = 2,
};

[[nodiscard]] constexpr TextureSurfaceCopyFilter SelectTextureSurfaceResolveFilter() {
  return TextureSurfaceCopyFilter::kNone;
}

[[nodiscard]] constexpr TextureSurfaceCopyFilter SelectTextureSurfaceCopyFilter(
    const TextureSurfaceCopyRect* source_rect,
    const TextureSurfaceCopyRect* dest_rect) {
  if (source_rect != nullptr && dest_rect != nullptr &&
      source_rect->Width() == dest_rect->Width() &&
      source_rect->Height() == dest_rect->Height()) {
    return TextureSurfaceCopyFilter::kNone;
  }

  return TextureSurfaceCopyFilter::kLinear;
}

[[nodiscard]] constexpr std::uint64_t TextureSurfaceCopySamplerFlags(
    TextureSurfaceCopyFilter filter) {
  constexpr std::uint64_t kBaseFlags =
      BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;

  if (filter == TextureSurfaceCopyFilter::kNone) {
    return kBaseFlags | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT |
           BGFX_SAMPLER_MIP_POINT;
  }

  return kBaseFlags;
}

}
