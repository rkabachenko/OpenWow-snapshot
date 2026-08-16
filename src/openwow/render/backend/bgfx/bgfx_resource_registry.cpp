#include "openwow/render/backend/bgfx/bgfx_resource_registry.h"

#include <utility>

namespace openwow::render {
namespace {

bool IsNativeValid(const bgfx::TextureHandle native) {
  return bgfx::isValid(native);
}
bool IsNativeValid(const BgfxResourceRegistry::NativeBuffer native) {
  return native.IsValid();
}
bool IsNativeValid(const bgfx::ProgramHandle native) {
  return bgfx::isValid(native);
}
bool IsNativeValid(const bgfx::FrameBufferHandle native) {
  return bgfx::isValid(native);
}
bool IsNativeValid(const bgfx::UniformHandle native) {
  return bgfx::isValid(native);
}

template <typename Store, typename Handle, typename Native>
bool BindNative(Store& store, const Handle handle, const Native native) {
  if (handle.index >= store.natives.size()) {
    store.handles.resize(handle.index + 1u);
    store.natives.resize(handle.index + 1u);
    store.live.resize(handle.index + 1u);
  }
  if (store.live[handle.index] && store.handles[handle.index] != handle) {
    return false;
  }
  store.handles[handle.index] = handle;
  store.natives[handle.index] = native;
  if (!store.live[handle.index]) {
    ++store.live_count;
  }
  store.live[handle.index] = true;
  return true;
}

template <typename Store, typename Handle>
auto FindNative(const Store& store, const Handle handle)
    -> std::optional<typename Store::Native> {
  if (handle.index >= store.live.size() || !store.live[handle.index] ||
      store.handles[handle.index] != handle) {
    return std::nullopt;
  }
  return store.natives[handle.index];
}

template <typename Store, typename Handle, typename Destroy>
bool ReleaseNative(Store& store, const Handle handle, Destroy&& destroy) {
  if (handle.index >= store.live.size() || !store.live[handle.index] ||
      store.handles[handle.index] != handle) {
    return false;
  }
  destroy(store.natives[handle.index]);
  store.live[handle.index] = false;
  --store.live_count;
  return true;
}

template <typename Store, typename Destroy>
void ReleaseAllNatives(Store& store, Destroy&& destroy) {
  for (std::size_t index = 0; index < store.live.size(); ++index) {
    if (!store.live[index]) {
      continue;
    }
    destroy(store.natives[index]);
    store.live[index] = false;
  }
  store.live_count = 0;
}

void DestroyBufferNative(const BgfxResourceRegistry::NativeBuffer native) {
  if (!native.IsValid()) {
    return;
  }
  switch (native.kind) {
    case BgfxResourceRegistry::NativeBuffer::Kind::Vertex:
      bgfx::destroy(bgfx::VertexBufferHandle{native.idx});
      break;
    case BgfxResourceRegistry::NativeBuffer::Kind::Index:
      bgfx::destroy(bgfx::IndexBufferHandle{native.idx});
      break;
    case BgfxResourceRegistry::NativeBuffer::Kind::DynamicVertex:
      bgfx::destroy(bgfx::DynamicVertexBufferHandle{native.idx});
      break;
    case BgfxResourceRegistry::NativeBuffer::Kind::DynamicIndex:
      bgfx::destroy(bgfx::DynamicIndexBufferHandle{native.idx});
      break;
  }
}

}

BgfxResourceRegistry::BgfxResourceRegistry(DestroyHooks hooks)
    : hooks_(std::move(hooks)) {}

BgfxResourceRegistry::~BgfxResourceRegistry() {
  DestroyAll();
}

api::TextureHandle BgfxResourceRegistry::AdoptTexture(
    const bgfx::TextureHandle native,
    api::LogicalRenderResourceDescriptor descriptor) {
  if (!bgfx::isValid(native)) return {};
  const auto handle = logical_.RegisterTexture(std::move(descriptor));
  return Bind(handle, native) ? handle : api::TextureHandle{};
}

api::BufferHandle BgfxResourceRegistry::AdoptVertexBuffer(
    const bgfx::VertexBufferHandle native,
    api::LogicalRenderResourceDescriptor descriptor) {
  if (!bgfx::isValid(native)) return {};
  const auto handle = logical_.RegisterBuffer(std::move(descriptor));
  return Bind(handle, NativeBuffer{NativeBuffer::Kind::Vertex, native.idx})
             ? handle
             : api::BufferHandle{};
}

api::BufferHandle BgfxResourceRegistry::AdoptIndexBuffer(
    const bgfx::IndexBufferHandle native,
    api::LogicalRenderResourceDescriptor descriptor) {
  if (!bgfx::isValid(native)) return {};
  const auto handle = logical_.RegisterBuffer(std::move(descriptor));
  return Bind(handle, NativeBuffer{NativeBuffer::Kind::Index, native.idx})
             ? handle
             : api::BufferHandle{};
}

api::BufferHandle BgfxResourceRegistry::AdoptDynamicVertexBuffer(
    const bgfx::DynamicVertexBufferHandle native,
    api::LogicalRenderResourceDescriptor descriptor) {
  if (!bgfx::isValid(native)) return {};
  descriptor.dynamic = true;
  const auto handle = logical_.RegisterBuffer(std::move(descriptor));
  return Bind(handle,
              NativeBuffer{NativeBuffer::Kind::DynamicVertex, native.idx})
             ? handle
             : api::BufferHandle{};
}

api::BufferHandle BgfxResourceRegistry::AdoptDynamicIndexBuffer(
    const bgfx::DynamicIndexBufferHandle native,
    api::LogicalRenderResourceDescriptor descriptor) {
  if (!bgfx::isValid(native)) return {};
  descriptor.dynamic = true;
  const auto handle = logical_.RegisterBuffer(std::move(descriptor));
  return Bind(handle,
              NativeBuffer{NativeBuffer::Kind::DynamicIndex, native.idx})
             ? handle
             : api::BufferHandle{};
}

api::ProgramHandle BgfxResourceRegistry::AdoptProgram(
    const bgfx::ProgramHandle native,
    api::LogicalRenderResourceDescriptor descriptor) {
  if (!bgfx::isValid(native)) return {};
  const auto handle = logical_.RegisterProgram(std::move(descriptor));
  return Bind(handle, native) ? handle : api::ProgramHandle{};
}

api::FramebufferHandle BgfxResourceRegistry::AdoptFramebuffer(
    const bgfx::FrameBufferHandle native,
    api::LogicalRenderResourceDescriptor descriptor) {
  if (!bgfx::isValid(native)) return {};
  const auto handle = logical_.RegisterFramebuffer(std::move(descriptor));
  return Bind(handle, native) ? handle : api::FramebufferHandle{};
}

api::SamplerHandle BgfxResourceRegistry::AdoptSampler(
    const bgfx::UniformHandle native,
    api::LogicalRenderResourceDescriptor descriptor) {
  if (!bgfx::isValid(native)) return {};
  const auto handle = logical_.RegisterSampler(std::move(descriptor));
  return Bind(handle, native) ? handle : api::SamplerHandle{};
}

#define OPENWOW_BIND_RESOURCE(HandleType, NativeType, store, find_call, mark_call) \
  bool BgfxResourceRegistry::Bind(const HandleType handle,                      \
                                  const NativeType native) {                    \
    if (!IsNativeValid(native) || !logical_.find_call(handle) ||                \
        !logical_.NeedsRebind(handle)) {                                        \
      return false;                                                             \
    }                                                                           \
    if (!BindNative(store, handle, native)) return false;                       \
    logical_.mark_call(handle);                                                  \
    return true;                                                                \
  }

OPENWOW_BIND_RESOURCE(api::TextureHandle, bgfx::TextureHandle, textures_, Find,
                       MarkBound)
OPENWOW_BIND_RESOURCE(api::BufferHandle, BgfxResourceRegistry::NativeBuffer,
                      buffers_, Find, MarkBound)
OPENWOW_BIND_RESOURCE(api::ProgramHandle, bgfx::ProgramHandle, programs_, Find,
                       MarkBound)
OPENWOW_BIND_RESOURCE(api::FramebufferHandle, bgfx::FrameBufferHandle,
                       framebuffers_, Find, MarkBound)
OPENWOW_BIND_RESOURCE(api::SamplerHandle, bgfx::UniformHandle, samplers_, Find,
                       MarkBound)

#undef OPENWOW_BIND_RESOURCE

#define OPENWOW_TRY_GET(HandleType, NativeType, store)                           \
  std::optional<NativeType> BgfxResourceRegistry::TryGet(                        \
      const HandleType handle) const {                                           \
    if (!logical_.Find(handle)) return std::nullopt;                              \
    return FindNative(store, handle);                                             \
  }                                                                               \
  bool BgfxResourceRegistry::IsAlive(const HandleType handle) const {             \
    return TryGet(handle).has_value();                                            \
  }

OPENWOW_TRY_GET(api::TextureHandle, bgfx::TextureHandle, textures_)
OPENWOW_TRY_GET(api::BufferHandle, BgfxResourceRegistry::NativeBuffer, buffers_)
OPENWOW_TRY_GET(api::ProgramHandle, bgfx::ProgramHandle, programs_)
OPENWOW_TRY_GET(api::FramebufferHandle, bgfx::FrameBufferHandle, framebuffers_)
OPENWOW_TRY_GET(api::SamplerHandle, bgfx::UniformHandle, samplers_)

#undef OPENWOW_TRY_GET

bool BgfxResourceRegistry::Destroy(const api::TextureHandle handle) {
  ReleaseNative(textures_, handle, [this](const bgfx::TextureHandle native) {
    if (hooks_.destroy_texture) hooks_.destroy_texture(native);
    else if (bgfx::isValid(native)) bgfx::destroy(native);
  });
  return logical_.Release(handle);
}

bool BgfxResourceRegistry::Destroy(const api::BufferHandle handle) {
  ReleaseNative(buffers_, handle, [this](const NativeBuffer native) {
    if (hooks_.destroy_buffer) hooks_.destroy_buffer(native);
    else DestroyBufferNative(native);
  });
  return logical_.Release(handle);
}

bool BgfxResourceRegistry::Destroy(const api::ProgramHandle handle) {
  ReleaseNative(programs_, handle, [this](const bgfx::ProgramHandle native) {
    if (hooks_.destroy_program) hooks_.destroy_program(native);
    else if (bgfx::isValid(native)) bgfx::destroy(native);
  });
  return logical_.Release(handle);
}

bool BgfxResourceRegistry::Destroy(const api::FramebufferHandle handle) {
  ReleaseNative(framebuffers_, handle,
                [this](const bgfx::FrameBufferHandle native) {
                  if (hooks_.destroy_framebuffer)
                    hooks_.destroy_framebuffer(native);
                  else if (bgfx::isValid(native))
                    bgfx::destroy(native);
                });
  return logical_.Release(handle);
}

bool BgfxResourceRegistry::Destroy(const api::SamplerHandle handle) {
  ReleaseNative(samplers_, handle, [this](const bgfx::UniformHandle native) {
    if (hooks_.destroy_sampler) hooks_.destroy_sampler(native);
    else if (bgfx::isValid(native)) bgfx::destroy(native);
  });
  return logical_.Release(handle);
}

void BgfxResourceRegistry::BeginDeviceRestart() {
  ReleaseAllNatives(framebuffers_, [this](const bgfx::FrameBufferHandle native) {
    if (hooks_.destroy_framebuffer) hooks_.destroy_framebuffer(native);
    else if (bgfx::isValid(native)) bgfx::destroy(native);
  });
  ReleaseAllNatives(programs_, [this](const bgfx::ProgramHandle native) {
    if (hooks_.destroy_program) hooks_.destroy_program(native);
    else if (bgfx::isValid(native)) bgfx::destroy(native);
  });
  ReleaseAllNatives(buffers_, [this](const NativeBuffer native) {
    if (hooks_.destroy_buffer) hooks_.destroy_buffer(native);
    else DestroyBufferNative(native);
  });
  ReleaseAllNatives(textures_, [this](const bgfx::TextureHandle native) {
    if (hooks_.destroy_texture) hooks_.destroy_texture(native);
    else if (bgfx::isValid(native)) bgfx::destroy(native);
  });
  ReleaseAllNatives(samplers_, [this](const bgfx::UniformHandle native) {
    if (hooks_.destroy_sampler) hooks_.destroy_sampler(native);
    else if (bgfx::isValid(native)) bgfx::destroy(native);
  });
  logical_.BeginNewDeviceGeneration();
}

void BgfxResourceRegistry::DestroyAll() {
  BeginDeviceRestart();
  for (const auto handle : framebuffers_.handles) {
    if (handle) static_cast<void>(logical_.Release(handle));
  }
  for (const auto handle : programs_.handles) {
    if (handle) static_cast<void>(logical_.Release(handle));
  }
  for (const auto handle : buffers_.handles) {
    if (handle) static_cast<void>(logical_.Release(handle));
  }
  for (const auto handle : textures_.handles) {
    if (handle) static_cast<void>(logical_.Release(handle));
  }
  for (const auto handle : samplers_.handles) {
    if (handle) static_cast<void>(logical_.Release(handle));
  }
  framebuffers_ = {};
  programs_ = {};
  buffers_ = {};
  textures_ = {};
  samplers_ = {};
}

BgfxResourceRegistry::Stats BgfxResourceRegistry::GetStats() const noexcept {
  return {textures_.live_count, buffers_.live_count, programs_.live_count,
          framebuffers_.live_count, samplers_.live_count};
}

}
