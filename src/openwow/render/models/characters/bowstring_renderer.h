#pragma once

#include <array>
#include <cstdint>

#include <bgfx/bgfx.h>

namespace openwow::render {

class BowstringRenderer {
 public:
  BowstringRenderer() = default;
  ~BowstringRenderer() = default;

  BowstringRenderer(const BowstringRenderer&) = delete;
  BowstringRenderer& operator=(const BowstringRenderer&) = delete;

  bool Initialize();

  void Shutdown();

  void Render(std::uint8_t view_id, const std::array<float, 3>& top_world,
              const std::array<float, 3>& bottom_world,
              const std::array<float, 3>& camera_right_world,
              std::uint32_t sort_depth);

  [[nodiscard]] bool is_initialized() const noexcept { return initialized_; }

 private:
  bool initialized_{false};

  bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle s_tex_ = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle white_tex_ = BGFX_INVALID_HANDLE;
  bgfx::VertexLayout layout_;

  static constexpr float kProvenanceGapHalfWidth = 0.012f;

  static constexpr std::uint32_t kProvenanceGapDefaultColorAbgr = 0xFFE8E8E8u;
};

}
