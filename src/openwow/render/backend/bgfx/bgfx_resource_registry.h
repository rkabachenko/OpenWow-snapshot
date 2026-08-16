#pragma once

#include "openwow/render/api/render_resource_registry.h"

#include <bgfx/bgfx.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace openwow::render {

class BgfxResourceRegistry {
 public:
  struct NativeBuffer {
    enum class Kind : std::uint8_t {
      Vertex,
      Index,
      DynamicVertex,
      DynamicIndex,
    };

    Kind kind = Kind::Vertex;
    std::uint16_t idx = bgfx::kInvalidHandle;

    [[nodiscard]] bool IsValid() const noexcept {
      return idx != bgfx::kInvalidHandle;
    }
  };

  struct Stats {
    std::uint32_t live_textures = 0;
    std::uint32_t live_buffers = 0;
    std::uint32_t live_programs = 0;
    std::uint32_t live_framebuffers = 0;
    std::uint32_t live_samplers = 0;
  };

  struct DestroyHooks {
    std::function<void(bgfx::TextureHandle)> destroy_texture;
    std::function<void(NativeBuffer)> destroy_buffer;
    std::function<void(bgfx::ProgramHandle)> destroy_program;
    std::function<void(bgfx::FrameBufferHandle)> destroy_framebuffer;
    std::function<void(bgfx::UniformHandle)> destroy_sampler;
  };

  explicit BgfxResourceRegistry(DestroyHooks hooks = {});
  ~BgfxResourceRegistry();

  BgfxResourceRegistry(const BgfxResourceRegistry&) = delete;
  BgfxResourceRegistry& operator=(const BgfxResourceRegistry&) = delete;
  BgfxResourceRegistry(BgfxResourceRegistry&&) = delete;
  BgfxResourceRegistry& operator=(BgfxResourceRegistry&&) = delete;

  [[nodiscard]] api::TextureHandle AdoptTexture(
      bgfx::TextureHandle native,
      api::LogicalRenderResourceDescriptor descriptor = {});
  [[nodiscard]] api::BufferHandle AdoptVertexBuffer(
      bgfx::VertexBufferHandle native,
      api::LogicalRenderResourceDescriptor descriptor = {});
  [[nodiscard]] api::BufferHandle AdoptIndexBuffer(
      bgfx::IndexBufferHandle native,
      api::LogicalRenderResourceDescriptor descriptor = {});
  [[nodiscard]] api::BufferHandle AdoptDynamicVertexBuffer(
      bgfx::DynamicVertexBufferHandle native,
      api::LogicalRenderResourceDescriptor descriptor = {});
  [[nodiscard]] api::BufferHandle AdoptDynamicIndexBuffer(
      bgfx::DynamicIndexBufferHandle native,
      api::LogicalRenderResourceDescriptor descriptor = {});
  [[nodiscard]] api::ProgramHandle AdoptProgram(
      bgfx::ProgramHandle native,
      api::LogicalRenderResourceDescriptor descriptor = {});
  [[nodiscard]] api::FramebufferHandle AdoptFramebuffer(
      bgfx::FrameBufferHandle native,
      api::LogicalRenderResourceDescriptor descriptor = {});
  [[nodiscard]] api::SamplerHandle AdoptSampler(
      bgfx::UniformHandle native,
      api::LogicalRenderResourceDescriptor descriptor = {});

  [[nodiscard]] bool Bind(api::TextureHandle handle,
                          bgfx::TextureHandle native);
  [[nodiscard]] bool Bind(api::BufferHandle handle, NativeBuffer native);
  [[nodiscard]] bool Bind(api::ProgramHandle handle,
                          bgfx::ProgramHandle native);
  [[nodiscard]] bool Bind(api::FramebufferHandle handle,
                          bgfx::FrameBufferHandle native);
  [[nodiscard]] bool Bind(api::SamplerHandle handle,
                          bgfx::UniformHandle native);

  [[nodiscard]] bool IsAlive(api::TextureHandle handle) const;
  [[nodiscard]] bool IsAlive(api::BufferHandle handle) const;
  [[nodiscard]] bool IsAlive(api::ProgramHandle handle) const;
  [[nodiscard]] bool IsAlive(api::FramebufferHandle handle) const;
  [[nodiscard]] bool IsAlive(api::SamplerHandle handle) const;

  [[nodiscard]] std::optional<bgfx::TextureHandle> TryGet(api::TextureHandle handle) const;
  [[nodiscard]] std::optional<NativeBuffer> TryGet(api::BufferHandle handle) const;
  [[nodiscard]] std::optional<bgfx::ProgramHandle> TryGet(api::ProgramHandle handle) const;
  [[nodiscard]] std::optional<bgfx::FrameBufferHandle> TryGet(api::FramebufferHandle handle) const;
  [[nodiscard]] std::optional<bgfx::UniformHandle> TryGet(api::SamplerHandle handle) const;

  [[nodiscard]] bool Destroy(api::TextureHandle handle);
  [[nodiscard]] bool Destroy(api::BufferHandle handle);
  [[nodiscard]] bool Destroy(api::ProgramHandle handle);
  [[nodiscard]] bool Destroy(api::FramebufferHandle handle);
  [[nodiscard]] bool Destroy(api::SamplerHandle handle);

  void DestroyAll();
  void BeginDeviceRestart();

  [[nodiscard]] api::DeviceGeneration device_generation() const noexcept {
    return logical_.device_generation();
  }

  [[nodiscard]] Stats GetStats() const noexcept;

 private:
  template <typename HandleT, typename NativeT>
  struct Store {
    using Handle = HandleT;
    using Native = NativeT;

    std::vector<HandleT> handles;
    std::vector<NativeT> natives;
    std::vector<bool> live;
    std::uint32_t live_count = 0;
  };

  DestroyHooks hooks_;
  api::RenderResourceRegistry logical_;
  Store<api::TextureHandle, bgfx::TextureHandle> textures_;
  Store<api::BufferHandle, NativeBuffer> buffers_;
  Store<api::ProgramHandle, bgfx::ProgramHandle> programs_;
  Store<api::FramebufferHandle, bgfx::FrameBufferHandle> framebuffers_;
  Store<api::SamplerHandle, bgfx::UniformHandle> samplers_;
};

}
