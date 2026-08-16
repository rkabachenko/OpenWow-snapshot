#pragma once

#include <array>
#include <cstddef>

namespace openwow::ui::game {

struct LoadingScreenProgressBarElement {
  const char* texture_path = "";
  bool clip_right_edge_to_progress = false;
  float center_x = 0.0f;
  float center_y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
};

struct LoadingScreenProgressBarQuad {
  const char* texture_path = "";
  float left = 0.0f;
  float right = 0.0f;
  float bottom = 0.0f;
  float top = 0.0f;
};

struct LoadingScreenViewportRect {
  float left = 0.0f;
  float right = 1.0f;
  float bottom = 0.0f;
  float top = 1.0f;
};

inline constexpr const char* kLoadingBarFillTexturePath =
    "Interface\\Glues\\LoadingBar\\Loading-BarFill";
inline constexpr const char* kLoadingBarBorderTexturePath =
    "Interface\\Glues\\LoadingBar\\Loading-BarBorder";

inline constexpr std::array<LoadingScreenProgressBarElement, 2>
    kLoadingScreenBottomProgressBarElements = {{
        {
            .texture_path = kLoadingBarFillTexturePath,
            .clip_right_edge_to_progress = true,
            .center_x = 0.5f,
            .center_y = 0.075000003f,
            .width = 0.524999976f,
            .height = 0.025000000f,
        },
        {
            .texture_path = kLoadingBarBorderTexturePath,
            .clip_right_edge_to_progress = false,
            .center_x = 0.5f,
            .center_y = 0.075000003f,
            .width = 0.600000024f,
            .height = 0.050000001f,
        },
    }};

inline constexpr std::array<LoadingScreenProgressBarElement, 2>
    kLoadingScreenTopProgressBarElements = {{
        {
            .texture_path = kLoadingBarFillTexturePath,
            .clip_right_edge_to_progress = true,
            .center_x = 0.5f,
            .center_y = 0.975000024f,
            .width = 0.524999976f,
            .height = 0.025000000f,
        },
        {
            .texture_path = kLoadingBarBorderTexturePath,
            .clip_right_edge_to_progress = false,
            .center_x = 0.5f,
            .center_y = 0.975000024f,
            .width = 0.600000024f,
            .height = 0.050000001f,
        },
    }};

template <std::size_t N>
constexpr std::array<LoadingScreenProgressBarQuad, N>
BuildLoadingScreenProgressBarQuads(
    const std::array<LoadingScreenProgressBarElement, N>& elements,
    float progress) {
  std::array<LoadingScreenProgressBarQuad, N> quads{};

  for (std::size_t i = 0; i < N; ++i) {
    const auto& element = elements[i];
    const float left = element.center_x - element.width * 0.5f;
    const float right = element.clip_right_edge_to_progress
        ? left + element.width * progress
        : element.center_x + element.width * 0.5f;

    quads[i] = {
        .texture_path = element.texture_path,
        .left = left,
        .right = right,
        .bottom = element.center_y - element.height * 0.5f,
        .top = element.center_y + element.height * 0.5f,
    };
  }

  return quads;
}

constexpr LoadingScreenViewportRect BuildAspectFittedViewportRect(
    float screen_aspect, float target_aspect) {
  if (screen_aspect <= 0.0f || target_aspect <= 0.0f) {
    return {};
  }

  LoadingScreenViewportRect rect{};
  const float normalized_aspect = screen_aspect / target_aspect;
  if (normalized_aspect > 1.0f) {
    const float width = 1.0f / normalized_aspect;
    rect.left = (1.0f - width) * 0.5f;
    rect.right = rect.left + width;
    return rect;
  }

  if (normalized_aspect < 1.0f) {
    const float height = normalized_aspect;
    rect.bottom = (1.0f - height) * 0.5f;
    rect.top = rect.bottom + height;
  }

  return rect;
}

constexpr LoadingScreenProgressBarQuad MapLoadingScreenQuadToViewport(
    const LoadingScreenProgressBarQuad& quad,
    const LoadingScreenViewportRect& viewport) {
  const float width = viewport.right - viewport.left;
  const float height = viewport.top - viewport.bottom;
  return {
      .texture_path = quad.texture_path,
      .left = viewport.left + quad.left * width,
      .right = viewport.left + quad.right * width,
      .bottom = viewport.bottom + quad.bottom * height,
      .top = viewport.bottom + quad.top * height,
  };
}

}
