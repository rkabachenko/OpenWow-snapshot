#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>

namespace openwow::render {

class BgfxImGuiRenderer {
public:
  bool Initialize();
  void Shutdown();
  void Render(std::uint8_t view_id);

private:
  bgfx::VertexLayout layout_;
  bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle texture_sampler_ = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle font_texture_ = BGFX_INVALID_HANDLE;
  bool initialized_{false};
};

}
