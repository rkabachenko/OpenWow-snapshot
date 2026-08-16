#include "openwow/render/models/characters/bowstring_renderer.h"

#include "openwow/render/ui/ui_shaders.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <bgfx/bgfx.h>

#include <cmath>

namespace openwow::render {

bool BowstringRenderer::Initialize() {
  if (initialized_) return true;

  const auto handles = ui::LoadUiProgram();
  if (!bgfx::isValid(handles.program) || !bgfx::isValid(handles.s_tex)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "BowstringRenderer: UI shader program load failed");
    return false;
  }
  program_ = handles.program;
  s_tex_ = handles.s_tex;

  const uint32_t white_pixel = 0xFFFFFFFF;
  white_tex_ = bgfx::createTexture2D(
      1, 1, false, 1, bgfx::TextureFormat::RGBA8, 0,
      bgfx::copy(&white_pixel, sizeof(white_pixel)));

  layout_.begin()
      .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
      .end();

  initialized_ = true;
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "BowstringRenderer: initialized");
  return true;
}

void BowstringRenderer::Shutdown() {
  if (!initialized_) return;

  ui::DestroyUiProgram(program_, s_tex_);
  if (bgfx::isValid(white_tex_)) {
    bgfx::destroy(white_tex_);
    white_tex_ = BGFX_INVALID_HANDLE;
  }

  initialized_ = false;
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "BowstringRenderer: shutdown");
}

void BowstringRenderer::Render(const std::uint8_t view_id,
                               const std::array<float, 3>& top_world,
                               const std::array<float, 3>& bottom_world,
                               const std::array<float, 3>& camera_right_world,
                               const std::uint32_t sort_depth) {
  if (!initialized_) return;

  float right[3] = {camera_right_world[0], camera_right_world[1],
                     camera_right_world[2]};
  const float right_len_sq =
      right[0] * right[0] + right[1] * right[1] + right[2] * right[2];
  if (right_len_sq < 1.0e-8f) return;
  const float inv_right_len = 1.0f / std::sqrt(right_len_sq);
  right[0] *= inv_right_len;
  right[1] *= inv_right_len;
  right[2] *= inv_right_len;

  constexpr std::uint32_t kVertexCount = 4u;
  constexpr std::uint32_t kIndexCount = 6u;

  if (bgfx::getAvailTransientVertexBuffer(kVertexCount, layout_) < kVertexCount ||
      bgfx::getAvailTransientIndexBuffer(kIndexCount) < kIndexCount) {
    return;
  }

  bgfx::TransientVertexBuffer tvb;
  bgfx::TransientIndexBuffer tib;
  bgfx::allocTransientVertexBuffer(&tvb, kVertexCount, layout_);
  bgfx::allocTransientIndexBuffer(&tib, kIndexCount);

  struct StringVertex {
    float x, y, z;
    float u, v;
    uint32_t abgr;
  };

  auto* verts = reinterpret_cast<StringVertex*>(tvb.data);
  const float hw = kProvenanceGapHalfWidth;

  verts[0] = {top_world[0] - right[0] * hw, top_world[1] - right[1] * hw,
              top_world[2] - right[2] * hw, 0.0f, 0.0f,
              kProvenanceGapDefaultColorAbgr};
  verts[1] = {top_world[0] + right[0] * hw, top_world[1] + right[1] * hw,
              top_world[2] + right[2] * hw, 1.0f, 0.0f,
              kProvenanceGapDefaultColorAbgr};
  verts[2] = {bottom_world[0] - right[0] * hw, bottom_world[1] - right[1] * hw,
              bottom_world[2] - right[2] * hw, 0.0f, 1.0f,
              kProvenanceGapDefaultColorAbgr};
  verts[3] = {bottom_world[0] + right[0] * hw, bottom_world[1] + right[1] * hw,
              bottom_world[2] + right[2] * hw, 1.0f, 1.0f,
              kProvenanceGapDefaultColorAbgr};

  auto* idx = reinterpret_cast<uint16_t*>(tib.data);
  idx[0] = 0; idx[1] = 1; idx[2] = 2;
  idx[3] = 2; idx[4] = 1; idx[5] = 3;

  const uint64_t state = BGFX_STATE_WRITE_RGB
                       | BGFX_STATE_WRITE_A
                       | BGFX_STATE_DEPTH_TEST_LEQUAL
                       | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                                               BGFX_STATE_BLEND_INV_SRC_ALPHA);
  bgfx::setState(state);
  bgfx::setVertexBuffer(0, &tvb);
  bgfx::setIndexBuffer(&tib);
  bgfx::setTexture(0, s_tex_, white_tex_, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
  bgfx::submit(view_id, program_, sort_depth);
}

}
