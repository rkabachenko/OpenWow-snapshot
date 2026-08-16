#include "openwow/ui/game/runtime/render/text_region_renderer.h"

#include "openwow/render/ui/text_renderer.h"

namespace openwow::ui::game::runtime::render {

TextRegionRenderer::TextRegionRenderer(UiRenderResources& resources) noexcept
    : resources_(resources) {}

void TextRegionRenderer::BeginFrame(const std::uint8_t view_id,
                                    const float width, const float height) {
  auto& renderer = resources_.text_renderer();
  if (renderer.is_ready()) renderer.BeginFrame(view_id, width, height);
}

openwow::render::BgfxTextCache* TextRegionRenderer::cache() noexcept {
  return resources_.text_cache();
}

std::vector<openwow::render::ui::MeshVertex>&
TextRegionRenderer::mesh_vertices() noexcept {
  return resources_.text_mesh_vertices();
}

std::vector<std::uint16_t>& TextRegionRenderer::mesh_indices() noexcept {
  return resources_.text_mesh_indices();
}

stateful_widgets::ScrollingMessageFrameState&
TextRegionRenderer::scrolling_message_state() noexcept {
  return resources_.scrolling_message_state();
}

std::vector<stateful_widgets::ScrollingMessageMeasurement>&
TextRegionRenderer::scrolling_message_measurements() noexcept {
  return resources_.scrolling_message_measurements();
}

std::vector<stateful_widgets::ScrollingMessageLinePlan>&
TextRegionRenderer::scrolling_message_line_plans() noexcept {
  return resources_.scrolling_message_line_plans();
}

}
