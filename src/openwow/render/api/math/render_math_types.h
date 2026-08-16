#pragma once

#include <array>
#include <span>

namespace openwow::render {

using RenderMatrix4x4 = std::array<float, 16>;
using RenderMatrix4x4View = std::span<const float, 16>;
using RenderMatrix3x3 = std::array<float, 9>;
using RenderVec3 = std::array<float, 3>;
using RenderVec3View = std::span<const float, 3>;
using RenderVec4 = std::array<float, 4>;
using RenderVec4View = std::span<const float, 4>;
using RenderAabb = std::array<float, 6>;
using RenderAabbView = std::span<const float, 6>;
using RenderFrustumCorners = std::array<RenderVec3, 8>;
static_assert(sizeof(RenderFrustumCorners) == 24 * sizeof(float));

struct RenderViewportRayEndpoints {
  RenderVec3 near_point{};
  RenderVec3 far_point{};
};

struct RenderFogState {
  RenderVec4 params{300.0f, 800.0f, 0.0f, 0.0f};
  RenderVec4 color{0.6f, 0.7f, 0.85f, 1.0f};
};

struct RenderScreenProjection {
  RenderVec3 position{};
  int clip_flags = 0;
  bool on_screen = false;
};

constexpr RenderMatrix4x4 kRenderIdentityMatrix4x4{
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f,
};

}
