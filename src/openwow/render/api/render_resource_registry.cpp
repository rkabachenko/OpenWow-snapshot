#include "openwow/render/api/render_resource_registry.h"

#include <utility>

namespace openwow::render::api {

template <typename Handle>
Handle RenderResourceRegistry::Register(
    RecordStore<Handle>& store, LogicalRenderResourceDescriptor descriptor) {
  const Handle handle = store.pool.Allocate();
  store.descriptors.insert_or_assign(handle.index, std::move(descriptor));
  store.bindings.erase(handle.index);
  return handle;
}

template <typename Handle>
bool RenderResourceRegistry::Release(RecordStore<Handle>& store, const Handle handle) {
  if (!store.pool.Release(handle)) {
    return false;
  }
  store.descriptors.erase(handle.index);
  store.bindings.erase(handle.index);
  return true;
}

template <typename Handle>
const LogicalRenderResourceDescriptor* RenderResourceRegistry::Find(
    const RecordStore<Handle>& store, const Handle handle) const {
  if (!store.pool.IsAlive(handle)) {
    return nullptr;
  }
  const auto found = store.descriptors.find(handle.index);
  return found == store.descriptors.end() ? nullptr : &found->second;
}

template <typename Handle>
void RenderResourceRegistry::MarkBound(RecordStore<Handle>& store, const Handle handle) {
  if (store.pool.IsAlive(handle)) {
    store.bindings.insert_or_assign(handle.index, device_generation_);
  }
}

template <typename Handle>
bool RenderResourceRegistry::NeedsRebind(
    const RecordStore<Handle>& store, const Handle handle) const {
  if (!store.pool.IsAlive(handle)) {
    return false;
  }
  const auto found = store.bindings.find(handle.index);
  return found == store.bindings.end() || found->second != device_generation_;
}

#define OPENWOW_RENDER_RESOURCE_METHODS(Name, HandleType, member, type_value)            \
  HandleType RenderResourceRegistry::Register##Name(                                    \
      LogicalRenderResourceDescriptor descriptor) {                                     \
    descriptor.type = type_value;                                                        \
    return Register(member, std::move(descriptor));                                      \
  }                                                                                      \
  bool RenderResourceRegistry::Release(const HandleType handle) {                        \
    return Release(member, handle);                                                      \
  }                                                                                      \
  const LogicalRenderResourceDescriptor* RenderResourceRegistry::Find(                   \
      const HandleType handle) const {                                                   \
    return Find(member, handle);                                                         \
  }                                                                                      \
  void RenderResourceRegistry::MarkBound(const HandleType handle) {                      \
    MarkBound(member, handle);                                                           \
  }                                                                                      \
  bool RenderResourceRegistry::NeedsRebind(const HandleType handle) const {              \
    return NeedsRebind(member, handle);                                                  \
  }

OPENWOW_RENDER_RESOURCE_METHODS(Texture, TextureHandle, textures_,
                                RenderResourceType::kTexture)
OPENWOW_RENDER_RESOURCE_METHODS(Buffer, BufferHandle, buffers_,
                                RenderResourceType::kBuffer)
OPENWOW_RENDER_RESOURCE_METHODS(Program, ProgramHandle, programs_,
                                RenderResourceType::kProgram)
OPENWOW_RENDER_RESOURCE_METHODS(Framebuffer, FramebufferHandle, framebuffers_,
                                RenderResourceType::kFramebuffer)
OPENWOW_RENDER_RESOURCE_METHODS(Sampler, SamplerHandle, samplers_,
                                RenderResourceType::kSampler)

#undef OPENWOW_RENDER_RESOURCE_METHODS

void RenderResourceRegistry::BeginNewDeviceGeneration() noexcept {
  ++device_generation_.value;
  if (!device_generation_.IsValid()) {
    ++device_generation_.value;
  }
}

}
