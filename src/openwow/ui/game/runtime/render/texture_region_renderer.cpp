#include "openwow/ui/game/runtime/render/texture_region_renderer.h"

namespace openwow::ui::game::runtime::render {

TextureRegionRenderer::TextureRegionRenderer(
    UiRenderResources& resources) noexcept
    : resources_(resources) {}

const UiTextureLoad& TextureRegionRenderer::texture_loader() const noexcept {
  return resources_.texture_loader();
}

stateful_widgets::SliderState& TextureRegionRenderer::slider_state() noexcept {
  return resources_.slider_state();
}

stateful_widgets::CooldownState&
TextureRegionRenderer::cooldown_state() noexcept {
  return resources_.cooldown_state();
}

openwow::render::PortraitRenderer*
TextureRegionRenderer::portrait_renderer() noexcept {
  return resources_.portrait_renderer();
}

}

#include "openwow/ui/game/runtime/lua_frame_projection.h"
#include "openwow/ui/game/framescript/widgets/quest_poi_frame_methods.h"
#include "openwow/ui/game/world_map_system.h"
#include "openwow/game/world_session.h"
#include "openwow/render/backend/bgfx/bgfx_texture_lease.h"
#include "openwow/render/ui/ui_renderer.h"

#include <lua.hpp>

#include <algorithm>
#include <span>
#include <vector>

namespace openwow::ui::game::lua_projection {
static std::uint32_t PackStraightWhiteAbgr(const std::uint8_t alpha) {
  return (static_cast<std::uint32_t>(alpha) << 24) | 0x00FFFFFFu;
}

static bgfx::TextureHandle TextureHandleFromIndex(const std::uint16_t index) {
  return bgfx::TextureHandle{index};
}

bgfx::TextureHandle TextureHandleFromInfo(
    const runtime::render::UiTextureInfo &texture_info) {
  return openwow::render::BgfxTextureLeaseAccess::Get(texture_info.lease);
}

template <typename VertexCollection, typename TexCoordCollection>
static std::vector<openwow::render::ui::MeshVertex>
BuildQuestPoiMeshVertices(const VertexCollection &vertices, const TexCoordCollection &tex_coords,
                          const openwow::ui::framexml::FrameRect &rect, const std::uint32_t abgr) {
  std::vector<openwow::render::ui::MeshVertex> mesh_vertices;
  if (vertices.size() != tex_coords.size()) {
    return mesh_vertices;
  }

  mesh_vertices.reserve(vertices.size());
  const float rect_x = static_cast<float>(rect.x);
  const float rect_y = static_cast<float>(rect.y);
  const float rect_w = static_cast<float>(rect.width);
  const float rect_h = static_cast<float>(rect.height);
  for (std::size_t index = 0; index < vertices.size(); ++index) {
    mesh_vertices.push_back({
        .x = rect_x + vertices[index].x * rect_w,
        .y = rect_y + vertices[index].y * rect_h,
        .u = tex_coords[index].u,
        .v = tex_coords[index].v,
        .abgr = abgr,
    });
  }

  return mesh_vertices;
}

static void SubmitQuestPoiRenderData(const openwow::game::QuestPOIRenderData &render_data,
                                     const bool border_pass,
                                     const openwow::ui::framexml::FrameRect &rect,
                                     const bgfx::TextureHandle texture,
                                     const std::uint32_t color_abgr,
                                     openwow::render::ui::UiRenderer *const ui_renderer) {
  if (ui_renderer == nullptr || !render_data.active || !bgfx::isValid(texture)) {
    return;
  }

  if (!border_pass) {
    auto mesh_vertices = BuildQuestPoiMeshVertices(render_data.fillVertices,
                                                   render_data.fillTexCoords, rect, color_abgr);
    if (mesh_vertices.empty() || render_data.fillIndices.empty()) {
      return;
    }

    ui_renderer->SubmitMesh({
        .texture = texture,
        .vertices = std::span<const openwow::render::ui::MeshVertex>(mesh_vertices.data(),
                                                                     mesh_vertices.size()),
        .indices = std::span<const std::uint16_t>(render_data.fillIndices.data(),
                                                  render_data.fillIndices.size()),
        .blend = openwow::render::ui::BlendMode::kAlpha,
        .topology = openwow::render::ui::PrimitiveTopology::kTriangleList,
    });
    return;
  }

  auto mesh_vertices = BuildQuestPoiMeshVertices(render_data.borderVertices,
                                                 render_data.borderTexCoords, rect, color_abgr);
  if (mesh_vertices.empty() || render_data.borderIndices.empty()) {
    return;
  }

  ui_renderer->SubmitMesh({
      .texture = texture,
      .vertices = std::span<const openwow::render::ui::MeshVertex>(mesh_vertices.data(),
                                                                   mesh_vertices.size()),
      .indices = std::span<const std::uint16_t>(render_data.borderIndices.data(),
                                                render_data.borderIndices.size()),
      .blend = openwow::render::ui::BlendMode::kAlpha,
      .topology = openwow::render::ui::PrimitiveTopology::kTriangleStrip,
  });
}

void RenderQuestPoiFrame(lua_State *L, const int table_index,
                                const openwow::ui::framexml::UiFrame &frame,
                                const openwow::ui::framexml::FrameRect &rect,
                                openwow::game::WorldSession *session,
                                const openwow::ui::WorldMapSystem &world_map,
                                const runtime::render::UiTextureLoad &texture_loader,
                                openwow::render::ui::UiRenderer *const ui_renderer) {
  if (L == nullptr || session == nullptr || ui_renderer == nullptr || !texture_loader) {
    return;
  }

  const auto visual_state =
      openwow::ui::game::frame_api::ReadQuestPoiFrameVisualState(L, table_index, frame);
  if (visual_state.fill_texture_path.empty() || visual_state.border_texture_path.empty()) {
    return;
  }

  const auto fill_texture = texture_loader(visual_state.fill_texture_path);
  const bgfx::TextureHandle fill_handle =
      fill_texture.has_value() ? TextureHandleFromInfo(*fill_texture)
                               : TextureHandleFromIndex(0xFFFFu);
  if (!bgfx::isValid(fill_handle)) {
    return;
  }

  const auto border_texture = texture_loader(visual_state.border_texture_path);
  const bgfx::TextureHandle border_handle =
      border_texture.has_value() ? TextureHandleFromInfo(*border_texture)
                                 : TextureHandleFromIndex(0xFFFFu);
  if (!bgfx::isValid(border_handle)) {
    return;
  }

  const auto rendered_slots = openwow::ui::game::frame_api::BuildQuestPoiFrameRenderedSlots(
      visual_state, world_map, *session);
  const auto fill_color = PackStraightWhiteAbgr(visual_state.fill_alpha);
  const auto border_color = PackStraightWhiteAbgr(visual_state.border_alpha);

  for (const auto &slot_entries : rendered_slots) {
    for (const auto &entry : slot_entries) {
      SubmitQuestPoiRenderData(entry.render_data, false, rect, fill_handle, fill_color,
                               ui_renderer);
    }
  }

  for (const auto &slot_entries : rendered_slots) {
    for (const auto &entry : slot_entries) {
      SubmitQuestPoiRenderData(entry.render_data, true, rect, border_handle, border_color,
                               ui_renderer);
    }
  }
}

}
