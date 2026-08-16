#pragma once

#include "openwow/render/backend/bgfx/renderer_context_services.h"

#include <cstdint>
#include <vector>

namespace openwow::render {

class OffscreenRenderTarget final {
 public:
  OffscreenRenderTarget() = default;
  ~OffscreenRenderTarget();

  OffscreenRenderTarget(const OffscreenRenderTarget&) = delete;
  OffscreenRenderTarget& operator=(const OffscreenRenderTarget&) = delete;

  OffscreenRenderTarget(OffscreenRenderTarget&& other) noexcept;
  OffscreenRenderTarget& operator=(OffscreenRenderTarget&& other) noexcept;

  [[nodiscard]] bool EnsureSize(int width, int height);
  void Reset();
  void UnbindAllViews();

  [[nodiscard]] bool IsValid() const noexcept;
  [[nodiscard]] int width() const noexcept { return width_; }
  [[nodiscard]] int height() const noexcept { return height_; }
  [[nodiscard]] std::uint64_t ImGuiTextureId() const noexcept;
  [[nodiscard]] bool UsesHomogeneousDepth() const noexcept;

  [[nodiscard]] bool ConfigureView(std::uint8_t view_id,
                                   RendererViewClearFlags clear_flags,
                                   std::uint32_t rgba,
                                   float depth,
                                   std::uint8_t stencil,
                                   const float* view,
                                   const float* projection,
                                   bool touch_view = true);

 private:
  static constexpr std::uint16_t kInvalidHandleIndex = 0xffffu;

  std::uint16_t framebuffer_index_ = kInvalidHandleIndex;
  std::uint16_t texture_index_ = kInvalidHandleIndex;
  int width_ = 0;
  int height_ = 0;
  std::vector<std::uint8_t> bound_view_ids_;
};

}
