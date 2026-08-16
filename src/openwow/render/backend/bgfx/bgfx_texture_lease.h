#pragma once

#include "openwow/render/resources/textures/texture_lease.h"

#include <bgfx/bgfx.h>

namespace openwow::render {

class BgfxTextureLeaseAccess {
 public:
  [[nodiscard]] static bgfx::TextureHandle Get(
      const TextureLease& lease) noexcept;
};

}
