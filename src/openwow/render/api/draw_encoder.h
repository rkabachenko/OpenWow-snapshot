#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>

namespace openwow::render {

class DrawEncoder {
 public:
  DrawEncoder() = default;
  explicit DrawEncoder(bgfx::Encoder *encoder) noexcept : encoder_(encoder) {}

  [[nodiscard]] bgfx::Encoder *raw() const noexcept {
    return encoder_;
  }

  std::uint32_t setTransform(const void *mtx, std::uint16_t num = 1) const {
    if (encoder_ != nullptr) {
      return encoder_->setTransform(mtx, num);
    }
    return bgfx::setTransform(mtx, num);
  }

  void setTransform(std::uint32_t cache, std::uint16_t num) const {
    if (encoder_ != nullptr) {
      encoder_->setTransform(cache, num);
    } else {
      bgfx::setTransform(cache, num);
    }
  }

  void setState(std::uint64_t state, std::uint32_t rgba = 0) const {
    if (encoder_ != nullptr) {
      encoder_->setState(state, rgba);
    } else {
      bgfx::setState(state, rgba);
    }
  }

  void setUniform(bgfx::UniformHandle handle, const void *value,
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

  void setVertexBuffer(std::uint8_t stream, bgfx::VertexBufferHandle handle) const {
    if (encoder_ != nullptr) {
      encoder_->setVertexBuffer(stream, handle);
    } else {
      bgfx::setVertexBuffer(stream, handle);
    }
  }

  void setVertexBuffer(std::uint8_t stream, bgfx::VertexBufferHandle handle,
                       std::uint32_t start_vertex,
                       std::uint32_t num_vertices) const {
    if (encoder_ != nullptr) {
      encoder_->setVertexBuffer(stream, handle, start_vertex, num_vertices);
    } else {
      bgfx::setVertexBuffer(stream, handle, start_vertex, num_vertices);
    }
  }

  void setVertexBuffer(std::uint8_t stream,
                       bgfx::DynamicVertexBufferHandle handle) const {
    if (encoder_ != nullptr) {
      encoder_->setVertexBuffer(stream, handle);
    } else {
      bgfx::setVertexBuffer(stream, handle);
    }
  }

  void setVertexBuffer(std::uint8_t stream, bgfx::DynamicVertexBufferHandle handle,
                       std::uint32_t start_vertex,
                       std::uint32_t num_vertices) const {
    if (encoder_ != nullptr) {
      encoder_->setVertexBuffer(stream, handle, start_vertex, num_vertices);
    } else {
      bgfx::setVertexBuffer(stream, handle, start_vertex, num_vertices);
    }
  }

  void setVertexBuffer(std::uint8_t stream,
                       const bgfx::TransientVertexBuffer *tvb) const {
    if (encoder_ != nullptr) {
      encoder_->setVertexBuffer(stream, tvb);
    } else {
      bgfx::setVertexBuffer(stream, tvb);
    }
  }

  void setIndexBuffer(bgfx::IndexBufferHandle handle) const {
    if (encoder_ != nullptr) {
      encoder_->setIndexBuffer(handle);
    } else {
      bgfx::setIndexBuffer(handle);
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

  void setIndexBuffer(const bgfx::TransientIndexBuffer *tib) const {
    if (encoder_ != nullptr) {
      encoder_->setIndexBuffer(tib);
    } else {
      bgfx::setIndexBuffer(tib);
    }
  }

  void submit(bgfx::ViewId id, bgfx::ProgramHandle program,
              std::uint32_t depth = 0,
              std::uint8_t flags = BGFX_DISCARD_ALL) const {
    if (encoder_ != nullptr) {
      encoder_->submit(id, program, depth, flags);
    } else {
      bgfx::submit(id, program, depth, flags);
    }
  }

 private:
  bgfx::Encoder *encoder_ = nullptr;
};

}
