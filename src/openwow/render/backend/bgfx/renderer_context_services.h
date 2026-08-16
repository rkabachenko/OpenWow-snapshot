#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

namespace openwow::render::api {
class RendererContext;
}

namespace openwow::render {

enum class RendererViewClearFlags : std::uint8_t {
  kNone = 0,
  kColor = 1 << 0,
  kDepth = 1 << 1,
  kStencil = 1 << 2,
};

constexpr RendererViewClearFlags operator|(RendererViewClearFlags lhs,
                                           RendererViewClearFlags rhs) noexcept {
  return static_cast<RendererViewClearFlags>(
      static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs));
}

constexpr bool HasRendererViewClearFlag(RendererViewClearFlags flags,
                                        RendererViewClearFlags flag) noexcept {
  return (static_cast<std::uint8_t>(flags) & static_cast<std::uint8_t>(flag)) != 0;
}

[[nodiscard]] std::unique_ptr<api::RendererContext> CreateRendererContext();

[[nodiscard]] bool IsRendererContextActive() noexcept;

[[nodiscard]] bool IsRendererDeviceRestarting() noexcept;

[[nodiscard]] bool ClearRendererContextView(const api::RendererContext* context,
                                            std::uint8_t view_id,
                                            std::uint32_t rgba,
                                            std::uint32_t width = 0,
                                            std::uint32_t height = 0);

bool ClearRendererDebugText(const api::RendererContext* context);
bool PrintRendererDebugText(const api::RendererContext* context,
                            std::uint16_t x,
                            std::uint16_t y,
                            std::uint8_t attr,
                            std::string_view text);
bool SetRendererContextViewTransform(const api::RendererContext* context,
                                     std::uint8_t view_id,
                                     const float* view,
                                     const float* projection);
bool ConfigureRendererContextView(const api::RendererContext* context,
                                  std::uint8_t view_id,
                                  RendererViewClearFlags clear_flags,
                                  std::uint32_t rgba,
                                  float depth,
                                  std::uint8_t stencil,
                                  std::uint32_t width,
                                  std::uint32_t height,
                                  const float* view,
                                  const float* projection,
                                  bool touch_view = true);

}
