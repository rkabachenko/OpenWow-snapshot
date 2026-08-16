#include "openwow/render/backend/bgfx/bgfx_imgui_renderer.h"
#include "openwow/render/ui/ui_shaders.h"

#include <bgfx/bgfx.h>
#include <bx/bx.h>
#include <bx/math.h>
#include <imgui.h>

#include <cstdint>

namespace openwow::render {

namespace {

bgfx::TextureHandle ToHandle(const ImTextureID id) {
  return {static_cast<std::uint16_t>(static_cast<std::uint64_t>(id) & 0xffffu)};
}

}

bool BgfxImGuiRenderer::Initialize() {
  ImGuiIO& io = ImGui::GetIO();
  io.BackendRendererName = "imgui_bgfx";
  io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

  layout_
      .begin()
      .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
      .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
      .end();

  const auto handles = ui::LoadUiProgram();
  if (!bgfx::isValid(handles.program) || !bgfx::isValid(handles.s_tex)) {
    return false;
  }
  program_ = handles.program;
  texture_sampler_ = handles.s_tex;

  unsigned char* pixels = nullptr;
  int width = 0;
  int height = 0;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

  const bgfx::Memory* memory =
      bgfx::copy(pixels, static_cast<std::uint32_t>(width * height * 4));
  font_texture_ = bgfx::createTexture2D(
      static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height),
      false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE, memory);
  if (!bgfx::isValid(font_texture_)) {
    ui::DestroyUiProgram(program_, texture_sampler_);
    return false;
  }

  io.Fonts->SetTexID(static_cast<ImTextureID>(font_texture_.idx));

  initialized_ = true;
  return true;
}

void BgfxImGuiRenderer::Shutdown() {
  if (bgfx::isValid(font_texture_)) {
    bgfx::destroy(font_texture_);
    font_texture_ = BGFX_INVALID_HANDLE;
  }
  ui::DestroyUiProgram(program_, texture_sampler_);
  initialized_ = false;
}

void BgfxImGuiRenderer::Render(const std::uint8_t view_id) {
  if (!initialized_) {
    return;
  }
  ImDrawData* draw_data = ImGui::GetDrawData();
  if (!draw_data || draw_data->TotalVtxCount <= 0 ||
      draw_data->TotalIdxCount <= 0) {
    return;
  }

  bgfx::setScissor(uint16_t(0), uint16_t(0), uint16_t(0), uint16_t(0));

  const float fb_width = draw_data->DisplaySize.x * draw_data->FramebufferScale.x;
  const float fb_height = draw_data->DisplaySize.y * draw_data->FramebufferScale.y;
  if (fb_width <= 0 || fb_height <= 0) return;

  float view[16];
  float proj[16];
  bx::mtxIdentity(view);
  const float l = draw_data->DisplayPos.x;
  const float r = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
  const float t = draw_data->DisplayPos.y;
  const float b = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
  bx::mtxOrtho(proj, l, r, b, t, 0.0f, 1000.0f, 0.0f,
               bgfx::getCaps()->homogeneousDepth);
  bgfx::setViewTransform(view_id, view, proj);
  bgfx::setViewRect(view_id, 0, 0, static_cast<std::uint16_t>(fb_width),
                    static_cast<std::uint16_t>(fb_height));

  const std::uint64_t state =
      BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
      BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                            BGFX_STATE_BLEND_INV_SRC_ALPHA);

  const int total_vtx = draw_data->TotalVtxCount;
  const int total_idx = draw_data->TotalIdxCount;

  if (bgfx::getAvailTransientVertexBuffer(total_vtx, layout_) <
          static_cast<std::uint32_t>(total_vtx) ||
      bgfx::getAvailTransientIndexBuffer(static_cast<std::uint32_t>(total_idx)) <
          static_cast<std::uint32_t>(total_idx)) {
    return;
  }

  bgfx::TransientVertexBuffer tvb;
  bgfx::TransientIndexBuffer tib;
  bgfx::allocTransientVertexBuffer(&tvb, total_vtx, layout_);
  bgfx::allocTransientIndexBuffer(&tib, static_cast<std::uint32_t>(total_idx));

  struct BgfxDrawVert {
    float x, y;
    float u, v;
    std::uint32_t col;
  };
  static_assert(sizeof(BgfxDrawVert) == 20);
  static_assert(sizeof(BgfxDrawVert) == sizeof(ImDrawVert));

  auto* out_verts = reinterpret_cast<BgfxDrawVert*>(tvb.data);
  auto* out_idx = reinterpret_cast<std::uint16_t*>(tib.data);

  int vtx_offset = 0;
  int idx_offset = 0;
  for (int n = 0; n < draw_data->CmdListsCount; ++n) {
    const ImDrawList* cmd_list = draw_data->CmdLists[n];
    const ImDrawVert* src = cmd_list->VtxBuffer.Data;
    for (int i = 0; i < cmd_list->VtxBuffer.Size; ++i) {
      out_verts[vtx_offset + i].x = src[i].pos.x;
      out_verts[vtx_offset + i].y = src[i].pos.y;
      out_verts[vtx_offset + i].u = src[i].uv.x;
      out_verts[vtx_offset + i].v = src[i].uv.y;
      out_verts[vtx_offset + i].col = src[i].col;
    }
    const std::uint16_t* src_idx = cmd_list->IdxBuffer.Data;
    for (int i = 0; i < cmd_list->IdxBuffer.Size; ++i) {
      out_idx[idx_offset + i] =
          static_cast<std::uint16_t>(src_idx[i] + vtx_offset);
    }
    vtx_offset += cmd_list->VtxBuffer.Size;
    idx_offset += cmd_list->IdxBuffer.Size;
  }

  int idx_cursor = 0;
  for (int n = 0; n < draw_data->CmdListsCount; ++n) {
    const ImDrawList* cmd_list = draw_data->CmdLists[n];
    for (int ci = 0; ci < cmd_list->CmdBuffer.Size; ++ci) {
      const ImDrawCmd& cmd = cmd_list->CmdBuffer[ci];
      if (cmd.UserCallback) {
        cmd.UserCallback(cmd_list, &cmd);
        continue;
      }
      if (cmd.ElemCount == 0) continue;

      const float sx = (cmd.ClipRect.x - draw_data->DisplayPos.x) *
                       draw_data->FramebufferScale.x;
      const float sy = (cmd.ClipRect.y - draw_data->DisplayPos.y) *
                       draw_data->FramebufferScale.y;
      const float sw = (cmd.ClipRect.z - cmd.ClipRect.x) *
                       draw_data->FramebufferScale.x;
      const float sh = (cmd.ClipRect.w - cmd.ClipRect.y) *
                       draw_data->FramebufferScale.y;
      if (sw <= 0 || sh <= 0) continue;

      bgfx::setScissor(
          static_cast<std::uint16_t>(std::max(0.0f, sx)),
          static_cast<std::uint16_t>(std::max(0.0f, sy)),
          static_cast<std::uint16_t>(sw),
          static_cast<std::uint16_t>(sh));

      bgfx::TextureHandle tex = ToHandle(cmd.GetTexID());
      if (!bgfx::isValid(tex)) {
        tex = font_texture_;
      }

      bgfx::setTexture(0, texture_sampler_, tex);
      bgfx::setState(state);
      bgfx::setVertexBuffer(0, &tvb, 0, static_cast<std::uint32_t>(total_vtx));
      bgfx::setIndexBuffer(&tib, static_cast<std::uint32_t>(idx_cursor),
                           cmd.ElemCount);
      bgfx::submit(view_id, program_);
      idx_cursor += cmd.ElemCount;
    }
  }

  bgfx::setScissor(uint16_t(0), uint16_t(0), uint16_t(0), uint16_t(0));
}

}
