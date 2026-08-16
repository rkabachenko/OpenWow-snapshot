#pragma once

#include "openwow/render/api/device_generation.h"
#include "openwow/render/api/render_handles.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace openwow::render::api {

enum class RenderResourceType : std::uint8_t {
  kTexture,
  kBuffer,
  kProgram,
  kFramebuffer,
  kSampler,
};

struct LogicalRenderResourceDescriptor {
  RenderResourceType type{RenderResourceType::kBuffer};
  std::string debug_name;
  std::uint64_t content_identity{0};
  std::uint64_t byte_size{0};
  std::uint32_t width{0};
  std::uint32_t height{0};
  std::uint32_t depth{0};
  std::uint32_t mip_count{0};
  bool dynamic{false};
};

class RenderResourceRegistry {
 public:
  [[nodiscard]] TextureHandle RegisterTexture(LogicalRenderResourceDescriptor descriptor);
  [[nodiscard]] BufferHandle RegisterBuffer(LogicalRenderResourceDescriptor descriptor);
  [[nodiscard]] ProgramHandle RegisterProgram(LogicalRenderResourceDescriptor descriptor);
  [[nodiscard]] FramebufferHandle RegisterFramebuffer(
      LogicalRenderResourceDescriptor descriptor);
  [[nodiscard]] SamplerHandle RegisterSampler(LogicalRenderResourceDescriptor descriptor);

  [[nodiscard]] bool Release(TextureHandle handle);
  [[nodiscard]] bool Release(BufferHandle handle);
  [[nodiscard]] bool Release(ProgramHandle handle);
  [[nodiscard]] bool Release(FramebufferHandle handle);
  [[nodiscard]] bool Release(SamplerHandle handle);

  [[nodiscard]] const LogicalRenderResourceDescriptor* Find(TextureHandle handle) const;
  [[nodiscard]] const LogicalRenderResourceDescriptor* Find(BufferHandle handle) const;
  [[nodiscard]] const LogicalRenderResourceDescriptor* Find(ProgramHandle handle) const;
  [[nodiscard]] const LogicalRenderResourceDescriptor* Find(FramebufferHandle handle) const;
  [[nodiscard]] const LogicalRenderResourceDescriptor* Find(SamplerHandle handle) const;

  void MarkBound(TextureHandle handle);
  void MarkBound(BufferHandle handle);
  void MarkBound(ProgramHandle handle);
  void MarkBound(FramebufferHandle handle);
  void MarkBound(SamplerHandle handle);

  [[nodiscard]] bool NeedsRebind(TextureHandle handle) const;
  [[nodiscard]] bool NeedsRebind(BufferHandle handle) const;
  [[nodiscard]] bool NeedsRebind(ProgramHandle handle) const;
  [[nodiscard]] bool NeedsRebind(FramebufferHandle handle) const;
  [[nodiscard]] bool NeedsRebind(SamplerHandle handle) const;

  void BeginNewDeviceGeneration() noexcept;
  [[nodiscard]] DeviceGeneration device_generation() const noexcept {
    return device_generation_;
  }

 private:
  template <typename Handle>
  struct RecordStore {
    GenerationPool<Handle> pool;
    std::unordered_map<std::uint32_t, LogicalRenderResourceDescriptor> descriptors;
    std::unordered_map<std::uint32_t, DeviceGeneration> bindings;
  };

  template <typename Handle>
  Handle Register(RecordStore<Handle>& store, LogicalRenderResourceDescriptor descriptor);
  template <typename Handle>
  bool Release(RecordStore<Handle>& store, Handle handle);
  template <typename Handle>
  const LogicalRenderResourceDescriptor* Find(const RecordStore<Handle>& store,
                                               Handle handle) const;
  template <typename Handle>
  void MarkBound(RecordStore<Handle>& store, Handle handle);
  template <typename Handle>
  bool NeedsRebind(const RecordStore<Handle>& store, Handle handle) const;

  DeviceGeneration device_generation_{1};
  RecordStore<TextureHandle> textures_;
  RecordStore<BufferHandle> buffers_;
  RecordStore<ProgramHandle> programs_;
  RecordStore<FramebufferHandle> framebuffers_;
  RecordStore<SamplerHandle> samplers_;
};

}
