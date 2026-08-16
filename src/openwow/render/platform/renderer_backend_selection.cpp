#include "openwow/render/platform/renderer_backend_selection.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace openwow::render {

api::RendererBackend PlatformDefaultRendererBackend() noexcept {
#if defined(_WIN32)
  return api::RendererBackend::Direct3D11;
#elif defined(__APPLE__)
  return api::RendererBackend::Metal;
#elif defined(__linux__)
  return api::RendererBackend::Vulkan;
#else
  return api::RendererBackend::OpenGL;
#endif
}

bool IsRendererBackendSupported(const api::RendererBackend backend) noexcept {
  switch (backend) {
    case api::RendererBackend::Auto:
      return true;
    case api::RendererBackend::Vulkan:
#if defined(_WIN32) || defined(__linux__)
      return true;
#else
      return false;
#endif
    case api::RendererBackend::Metal:
#if defined(__APPLE__)
      return true;
#else
      return false;
#endif
    case api::RendererBackend::Direct3D11:
    case api::RendererBackend::Direct3D12:
#if defined(_WIN32)
      return true;
#else
      return false;
#endif
    case api::RendererBackend::OpenGL:
      return true;
    case api::RendererBackend::Unknown:
      return false;
  }
  return false;
}

api::RendererBackend ParseRendererBackend(const std::string_view value) {
  std::string normalized(value);
  std::ranges::transform(normalized, normalized.begin(),
                         [](const unsigned char character) {
                           return static_cast<char>(std::tolower(character));
                         });

  if (normalized == "vulkan") {
    return api::RendererBackend::Vulkan;
  }
  if (normalized == "metal") {
    return api::RendererBackend::Metal;
  }
  if (normalized == "d3d11" || normalized == "direct3d11" ||
      normalized == "dx11" || normalized == "d3d9" ||
      normalized == "direct3d9" || normalized == "dx9") {
    return api::RendererBackend::Direct3D11;
  }
  if (normalized == "d3d12" || normalized == "direct3d12" ||
      normalized == "dx12") {
    return api::RendererBackend::Direct3D12;
  }
  if (normalized == "opengl" || normalized == "gl") {
    return api::RendererBackend::OpenGL;
  }
  return api::RendererBackend::Auto;
}

api::RendererBackend ResolveRendererBackend(
    const api::RendererBackend requested) noexcept {
  if (requested != api::RendererBackend::Auto &&
      IsRendererBackendSupported(requested)) {
    return requested;
  }
  const auto platform_default = PlatformDefaultRendererBackend();
  return IsRendererBackendSupported(platform_default)
             ? platform_default
             : api::RendererBackend::OpenGL;
}

const char* RendererBackendConfigValue(
    const api::RendererBackend backend) noexcept {
  switch (backend) {
    case api::RendererBackend::Vulkan:
      return "vulkan";
    case api::RendererBackend::Metal:
      return "metal";
    case api::RendererBackend::Direct3D11:
      return "d3d11";
    case api::RendererBackend::Direct3D12:
      return "d3d12";
    case api::RendererBackend::OpenGL:
      return "opengl";
    case api::RendererBackend::Auto:
    case api::RendererBackend::Unknown:
      return "auto";
  }
  return "auto";
}

}
