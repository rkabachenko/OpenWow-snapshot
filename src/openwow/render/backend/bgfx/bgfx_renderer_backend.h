#pragma once

#include "openwow/render/api/render_scene.h"

#include <bgfx/bgfx.h>

namespace openwow::render {

[[nodiscard]] bgfx::RendererType::Enum ToBgfxRendererType(
    api::RendererBackend backend) noexcept;
[[nodiscard]] api::RendererBackend FromBgfxRendererType(
    bgfx::RendererType::Enum type) noexcept;

}
