
#include "openwow/render/resources/targets/offscreen_render_target.h"

#include "openwow/render/backend/bgfx/renderer_context_services.h"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <utility>

namespace openwow::render {
namespace {

struct CreatedFramebuffer {
  bgfx::FrameBufferHandle framebuffer = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
};

[[nodiscard]] bgfx::FrameBufferHandle ToBgfxFramebuffer(std::uint16_t index) noexcept {
  return bgfx::FrameBufferHandle{index};
}

[[nodiscard]] std::uint16_t ToIndex(bgfx::FrameBufferHandle handle) noexcept {
  return handle.idx;
}

[[nodiscard]] std::uint16_t ToIndex(bgfx::TextureHandle handle) noexcept {
  return handle.idx;
}

[[nodiscard]] std::uint16_t ToBgfxClearFlags(RendererViewClearFlags flags) noexcept {
  std::uint16_t bgfx_flags = BGFX_CLEAR_NONE;
  if (HasRendererViewClearFlag(flags, RendererViewClearFlags::kColor)) {
    bgfx_flags |= BGFX_CLEAR_COLOR;
  }
  if (HasRendererViewClearFlag(flags, RendererViewClearFlags::kDepth)) {
    bgfx_flags |= BGFX_CLEAR_DEPTH;
  }
  if (HasRendererViewClearFlag(flags, RendererViewClearFlags::kStencil)) {
    bgfx_flags |= BGFX_CLEAR_STENCIL;
  }
  return bgfx_flags;
}

[[nodiscard]] CreatedFramebuffer CreateColorDepthFramebuffer(int w, int h) {
  const std::uint16_t fw = static_cast<std::uint16_t>(w < 1 ? 1 : w);
  const std::uint16_t fh = static_cast<std::uint16_t>(h < 1 ? 1 : h);

  const std::uint64_t color_flags =
      BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
  const std::uint64_t depth_flags = BGFX_TEXTURE_RT;

  bgfx::TextureHandle color = bgfx::createTexture2D(
      fw, fh, false, 1, bgfx::TextureFormat::RGBA8, color_flags);
  if (!bgfx::isValid(color))
    return {};

  bgfx::TextureHandle depth = bgfx::createTexture2D(
      fw, fh, false, 1, bgfx::TextureFormat::D24S8, depth_flags);
  if (!bgfx::isValid(depth)) {
    bgfx::destroy(color);
    return {};
  }

  bgfx::TextureHandle attachments[2] = {color, depth};
  bgfx::FrameBufferHandle fb = bgfx::createFrameBuffer(2, attachments, true);
  if (!bgfx::isValid(fb)) {
    bgfx::destroy(color);
    bgfx::destroy(depth);
    return {};
  }

  return {.framebuffer = fb, .texture = bgfx::getTexture(fb)};
}

}

OffscreenRenderTarget::~OffscreenRenderTarget() {
  Reset();
}

OffscreenRenderTarget::OffscreenRenderTarget(
    OffscreenRenderTarget&& other) noexcept {
  *this = std::move(other);
}

OffscreenRenderTarget& OffscreenRenderTarget::operator=(
    OffscreenRenderTarget&& other) noexcept {
  if (this == &other) {
    return *this;
  }

  Reset();
  framebuffer_index_ = other.framebuffer_index_;
  texture_index_ = other.texture_index_;
  width_ = other.width_;
  height_ = other.height_;
  bound_view_ids_ = std::move(other.bound_view_ids_);

  other.framebuffer_index_ = kInvalidHandleIndex;
  other.texture_index_ = kInvalidHandleIndex;
  other.width_ = 0;
  other.height_ = 0;
  other.bound_view_ids_.clear();
  return *this;
}

bool OffscreenRenderTarget::EnsureSize(int width, int height) {
  width = std::max(1, width);
  height = std::max(1, height);
  if (width == width_ && height == height_ && IsValid()) {
    return true;
  }

  Reset();
  if (!IsRendererContextActive()) {
    return false;
  }

  const CreatedFramebuffer created = CreateColorDepthFramebuffer(width, height);
  if (!bgfx::isValid(created.framebuffer) || !bgfx::isValid(created.texture)) {
    return false;
  }

  framebuffer_index_ = ToIndex(created.framebuffer);
  texture_index_ = ToIndex(created.texture);
  width_ = width;
  height_ = height;
  return true;
}

void OffscreenRenderTarget::Reset() {
  if (framebuffer_index_ == kInvalidHandleIndex) {
    width_ = 0;
    height_ = 0;
    texture_index_ = kInvalidHandleIndex;
    bound_view_ids_.clear();
    return;
  }

  if (IsRendererContextActive()) {
    UnbindAllViews();
    const bgfx::FrameBufferHandle framebuffer = ToBgfxFramebuffer(framebuffer_index_);
    if (bgfx::isValid(framebuffer)) {
      bgfx::destroy(framebuffer);
    }
  }

  framebuffer_index_ = kInvalidHandleIndex;
  texture_index_ = kInvalidHandleIndex;
  width_ = 0;
  height_ = 0;
  bound_view_ids_.clear();
}

void OffscreenRenderTarget::UnbindAllViews() {
  if (!IsRendererContextActive()) {
    bound_view_ids_.clear();
    return;
  }

  for (const std::uint8_t view_id : bound_view_ids_) {
    bgfx::setViewFrameBuffer(view_id, BGFX_INVALID_HANDLE);
  }
  bound_view_ids_.clear();
}

bool OffscreenRenderTarget::IsValid() const noexcept {
  if (framebuffer_index_ == kInvalidHandleIndex || !IsRendererContextActive()) {
    return false;
  }
  return bgfx::isValid(ToBgfxFramebuffer(framebuffer_index_));
}

std::uint64_t OffscreenRenderTarget::ImGuiTextureId() const noexcept {
  return texture_index_ == kInvalidHandleIndex ? 0u : static_cast<std::uint64_t>(texture_index_);
}

bool OffscreenRenderTarget::UsesHomogeneousDepth() const noexcept {
  const bgfx::Caps* caps = bgfx::getCaps();
  return caps == nullptr || caps->homogeneousDepth;
}

bool OffscreenRenderTarget::ConfigureView(std::uint8_t view_id,
                                        RendererViewClearFlags clear_flags,
                                        std::uint32_t rgba,
                                        float depth,
                                        std::uint8_t stencil,
                                        const float* view,
                                        const float* projection,
                                        bool touch_view) {
  if (!IsValid() || view == nullptr || projection == nullptr || width_ <= 0 || height_ <= 0) {
    return false;
  }

  bgfx::setViewFrameBuffer(view_id, ToBgfxFramebuffer(framebuffer_index_));
  bgfx::setViewClear(view_id, ToBgfxClearFlags(clear_flags), rgba, depth, stencil);
  bgfx::setViewRect(view_id, 0, 0, static_cast<std::uint16_t>(width_),
                    static_cast<std::uint16_t>(height_));
  bgfx::setViewTransform(view_id, view, projection);
  if (touch_view) {
    bgfx::touch(view_id);
  }

  if (std::find(bound_view_ids_.begin(), bound_view_ids_.end(), view_id) ==
      bound_view_ids_.end()) {
    bound_view_ids_.push_back(view_id);
  }
  return true;
}

}
