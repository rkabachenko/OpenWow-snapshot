#pragma once

#include "openwow/render/backend/bgfx/renderer_context_services.h"

#include <bgfx/bgfx.h>

namespace openwow::render::api {
class RendererContext;
}

namespace openwow::render {

[[nodiscard]] bgfx::TextureHandle GetBgfxRendererContextWhiteTexture(
    const api::RendererContext* context) noexcept;

}
