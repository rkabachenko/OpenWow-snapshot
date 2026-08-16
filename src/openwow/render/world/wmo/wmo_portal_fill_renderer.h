#pragma once

#include "openwow/world/wmo/wmo_visibility.h"

#include <bgfx/bgfx.h>

#include <cstdint>

namespace openwow::render {

class WmoPortalFillRenderer {
 public:
  bool Initialize();
  void Shutdown();
  [[nodiscard]] bool IsValid() const;

  void Render(std::uint8_t view_id,
              const world::WmoExteriorPortalFillBatch& fills,
              std::uint32_t argb, std::uint16_t viewport_width,
              std::uint16_t viewport_height);

 private:
  bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
  bgfx::VertexLayout layout_{};
};

}
