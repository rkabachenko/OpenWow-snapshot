#pragma once

#include "openwow/render/m2/m2_transparent_draw_order.h"

#include <bgfx/bgfx.h>

#include <cstdint>

namespace openwow::render::m2 {

inline constexpr std::uint32_t kNoMatrixCacheIndex = UINT32_MAX;

class M2DrawEncoder {
 public:
  M2DrawEncoder() = default;
  explicit M2DrawEncoder(bgfx::Encoder* encoder,
                         M2InstanceDrawSortDepth* sort_depth = nullptr) noexcept
      : encoder_(encoder), sort_depth_(sort_depth) {}

  [[nodiscard]] bgfx::Encoder* raw() const noexcept { return encoder_; }
  [[nodiscard]] M2InstanceDrawSortDepth* sort_depth() const noexcept {
    return sort_depth_;
  }

  [[nodiscard]] std::uint32_t setTransformCached(const void* mtx,
                                                 std::uint16_t num = 1) const {
    return encoder_ != nullptr ? encoder_->setTransform(mtx, num)
                               : bgfx::setTransform(mtx, num);
  }

  void setTransform(std::uint32_t cache_index, std::uint16_t num = 1) const {
    if (encoder_ != nullptr) {
      encoder_->setTransform(cache_index, num);
    } else {
      bgfx::setTransform(cache_index, num);
    }
  }

  void setTransform(const void* mtx, std::uint16_t num = 1) const {
    if (encoder_ != nullptr) {
      encoder_->setTransform(mtx, num);
    } else {
      bgfx::setTransform(mtx, num);
    }
  }

  void setVertexBuffer(std::uint8_t stream, bgfx::VertexBufferHandle handle) const {
    if (encoder_ != nullptr) {
      encoder_->setVertexBuffer(stream, handle);
    } else {
      bgfx::setVertexBuffer(stream, handle);
    }
  }

  void setVertexBuffer(std::uint8_t stream, const bgfx::TransientVertexBuffer* tvb,
                        std::uint32_t start_vertex, std::uint32_t num_vertices) const {
    if (encoder_ != nullptr) {
      encoder_->setVertexBuffer(stream, tvb, start_vertex, num_vertices);
    } else {
      bgfx::setVertexBuffer(stream, tvb, start_vertex, num_vertices);
    }
  }

  void setIndexBuffer(bgfx::IndexBufferHandle handle, std::uint32_t first_index,
                       std::uint32_t num_indices) const {
    if (encoder_ != nullptr) {
      encoder_->setIndexBuffer(handle, first_index, num_indices);
    } else {
      bgfx::setIndexBuffer(handle, first_index, num_indices);
    }
  }

  void setInstanceDataBuffer(const bgfx::InstanceDataBuffer* idb,
                             std::uint32_t start, std::uint32_t num) const {
    if (encoder_ != nullptr) {
      encoder_->setInstanceDataBuffer(idb, start, num);
    } else {
      bgfx::setInstanceDataBuffer(idb, start, num);
    }
  }

  void setUniform(bgfx::UniformHandle handle, const void* value,
                   std::uint16_t num = 1) const {
    if (encoder_ != nullptr) {
      encoder_->setUniform(handle, value, num);
    } else {
      bgfx::setUniform(handle, value, num);
    }
  }

  void setTexture(std::uint8_t stage, bgfx::UniformHandle sampler,
                   bgfx::TextureHandle handle,
                   std::uint32_t flags = UINT32_MAX) const {
    if (encoder_ != nullptr) {
      encoder_->setTexture(stage, sampler, handle, flags);
    } else {
      bgfx::setTexture(stage, sampler, handle, flags);
    }
  }

  void setState(std::uint64_t state, std::uint32_t rgba = 0) const {
    if (encoder_ != nullptr) {
      encoder_->setState(state, rgba);
    } else {
      bgfx::setState(state, rgba);
    }
  }

  void submit(bgfx::ViewId id, bgfx::ProgramHandle program,
              std::uint8_t flags = BGFX_DISCARD_ALL) const {
    const std::uint32_t depth =
        sort_depth_ != nullptr ? sort_depth_->NextDrawDepth() : 0u;
    if (encoder_ != nullptr) {
      encoder_->submit(id, program, depth, flags);
    } else {
      bgfx::submit(id, program, depth, flags);
    }
  }

 private:
  bgfx::Encoder* encoder_ = nullptr;
  M2InstanceDrawSortDepth* sort_depth_ = nullptr;
};

}
