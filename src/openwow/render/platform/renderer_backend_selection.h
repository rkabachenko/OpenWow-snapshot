#pragma once

#include "openwow/render/api/render_scene.h"

#include <string_view>

namespace openwow::render {

[[nodiscard]] api::RendererBackend PlatformDefaultRendererBackend() noexcept;
[[nodiscard]] bool IsRendererBackendSupported(api::RendererBackend backend) noexcept;
[[nodiscard]] api::RendererBackend ParseRendererBackend(std::string_view value);
[[nodiscard]] api::RendererBackend ResolveRendererBackend(
    api::RendererBackend requested) noexcept;
[[nodiscard]] const char* RendererBackendConfigValue(
    api::RendererBackend backend) noexcept;

}
