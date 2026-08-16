#include "openwow/render/backend/bgfx/bgfx_renderer_backend.h"

namespace openwow::render {

bgfx::RendererType::Enum ToBgfxRendererType(
    const api::RendererBackend backend) noexcept {
  switch (backend) {
    case api::RendererBackend::Vulkan:
      return bgfx::RendererType::Vulkan;
    case api::RendererBackend::Metal:
      return bgfx::RendererType::Metal;
    case api::RendererBackend::Direct3D11:
      return bgfx::RendererType::Direct3D11;
    case api::RendererBackend::Direct3D12:
      return bgfx::RendererType::Direct3D12;
    case api::RendererBackend::OpenGL:
      return bgfx::RendererType::OpenGL;
    case api::RendererBackend::Auto:
    case api::RendererBackend::Unknown:
      return bgfx::RendererType::Count;
  }
  return bgfx::RendererType::Count;
}

api::RendererBackend FromBgfxRendererType(
    const bgfx::RendererType::Enum type) noexcept {
  switch (type) {
    case bgfx::RendererType::Vulkan:
      return api::RendererBackend::Vulkan;
    case bgfx::RendererType::Metal:
      return api::RendererBackend::Metal;
    case bgfx::RendererType::Direct3D11:
      return api::RendererBackend::Direct3D11;
    case bgfx::RendererType::Direct3D12:
      return api::RendererBackend::Direct3D12;
    case bgfx::RendererType::OpenGL:
      return api::RendererBackend::OpenGL;
    default:
      return api::RendererBackend::Unknown;
  }
}

}
