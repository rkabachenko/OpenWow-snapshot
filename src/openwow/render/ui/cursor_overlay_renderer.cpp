#include "openwow/render/ui/cursor_overlay_renderer.h"
#include "openwow/render/backend/bgfx/bgfx_texture_lease.h"

#include "openwow/render/resources/textures/texture_manager.h"
#include "openwow/render/ui/ui_renderer.h"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <string>

namespace openwow::render {
namespace {

constexpr std::uint16_t kCursorPixelsWide = 32u;
constexpr std::uint16_t kCursorPixelsHigh = 32u;
constexpr float kSoftwareCursorSize = 32.0f;
constexpr std::size_t kCursorPixelBytes =
    static_cast<std::size_t>(kCursorPixelsWide) *
    static_cast<std::size_t>(kCursorPixelsHigh) * 4u;
constexpr std::uint64_t kCursorSamplerFlags =
    BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
    BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT;

[[nodiscard]] bool HasVisiblePixels(std::span<const std::uint8_t> pixels) {
  return std::any_of(pixels.begin(), pixels.end(),
                     [](std::uint8_t byte) { return byte != 0; });
}

}

struct CursorOverlayRenderer::Impl {
  ui::UiRenderer renderer;
  bgfx::TextureHandle custom_texture = BGFX_INVALID_HANDLE;
  bool renderer_ready = false;

  [[nodiscard]] bool EnsureRenderer() {
    if (renderer_ready) {
      return true;
    }
    renderer_ready = renderer.Init();
    return renderer_ready;
  }

  void DestroyCustomTexture() {
    if (bgfx::isValid(custom_texture)) {
      bgfx::destroy(custom_texture);
      custom_texture = BGFX_INVALID_HANDLE;
    }
  }

  [[nodiscard]] bool Submit(std::uint8_t view_id,
                            float screen_w,
                            float screen_h,
                            int mouse_x,
                            int mouse_y,
                            int hotspot_x,
                            int hotspot_y,
                            bgfx::TextureHandle texture) {
    if (!EnsureRenderer() || !bgfx::isValid(texture) || screen_w <= 0.0f ||
        screen_h <= 0.0f) {
      return false;
    }

    ui::Quad quad{};
    quad.texture = texture;
    quad.x = static_cast<float>(mouse_x - hotspot_x);
    quad.y = static_cast<float>(mouse_y - hotspot_y);
    quad.w = kSoftwareCursorSize;
    quad.h = kSoftwareCursorSize;
    quad.blend = ui::BlendMode::kAlpha;
    quad.sampler_flags = kCursorSamplerFlags;

    renderer.Begin(static_cast<int>(view_id), static_cast<int>(screen_w),
                   static_cast<int>(screen_h));
    const bool submitted = renderer.Submit(quad);
    renderer.End();
    return submitted;
  }
};

CursorOverlayRenderer::CursorOverlayRenderer(TextureManager& texture_manager)
    : texture_manager_(texture_manager),
      impl_(std::make_unique<Impl>()) {}

CursorOverlayRenderer::~CursorOverlayRenderer() {
  Shutdown();
}

bool CursorOverlayRenderer::RenderTexturePath(std::uint8_t view_id,
                                              float screen_w,
                                              float screen_h,
                                              int mouse_x,
                                              int mouse_y,
                                              int hotspot_x,
                                              int hotspot_y,
                                              std::string_view texture_path) {
  if (impl_ == nullptr) {
    return false;
  }

  const TextureLease texture =
      texture_manager_.AcquireCachedTexture(std::string(texture_path));
  return impl_->Submit(view_id, screen_w, screen_h, mouse_x, mouse_y,
                       hotspot_x, hotspot_y,
                       BgfxTextureLeaseAccess::Get(texture));
}

bool CursorOverlayRenderer::RenderCustomPixels(std::uint8_t view_id,
                                               float screen_w,
                                               float screen_h,
                                               int mouse_x,
                                               int mouse_y,
                                               int hotspot_x,
                                               int hotspot_y,
                                               std::span<const std::uint8_t> rgba32x32,
                                               bool pixels_dirty) {
  if (impl_ == nullptr) {
    return false;
  }

  if (pixels_dirty) {
    impl_->DestroyCustomTexture();
    if (rgba32x32.size() == kCursorPixelBytes && HasVisiblePixels(rgba32x32)) {
      impl_->custom_texture = bgfx::createTexture2D(
          kCursorPixelsWide,
          kCursorPixelsHigh,
          false,
          1,
          bgfx::TextureFormat::RGBA8,
          kCursorSamplerFlags,
          bgfx::copy(rgba32x32.data(),
                     static_cast<std::uint32_t>(rgba32x32.size())));
    }
  }

  return impl_->Submit(view_id, screen_w, screen_h, mouse_x, mouse_y,
                       hotspot_x, hotspot_y, impl_->custom_texture);
}

void CursorOverlayRenderer::ResetCustomTexture() {
  if (impl_ != nullptr) {
    impl_->DestroyCustomTexture();
  }
}

void CursorOverlayRenderer::Shutdown() {
  if (impl_ == nullptr) {
    return;
  }
  impl_->DestroyCustomTexture();
  impl_->renderer.Shutdown();
  impl_->renderer_ready = false;
}

}
