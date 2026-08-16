#pragma once

#include "openwow/ui/game/runtime/render/ui_render_resources.h"

namespace openwow::ui::game::runtime::render {

class TextRegionRenderer final {
 public:
  explicit TextRegionRenderer(UiRenderResources& resources) noexcept;

  void BeginFrame(std::uint8_t view_id, float width, float height);
  [[nodiscard]] openwow::render::BgfxTextCache* cache() noexcept;
  [[nodiscard]] std::vector<openwow::render::ui::MeshVertex>&
  mesh_vertices() noexcept;
  [[nodiscard]] std::vector<std::uint16_t>& mesh_indices() noexcept;
  [[nodiscard]] stateful_widgets::ScrollingMessageFrameState&
  scrolling_message_state() noexcept;
  [[nodiscard]] std::vector<stateful_widgets::ScrollingMessageMeasurement>&
  scrolling_message_measurements() noexcept;
  [[nodiscard]] std::vector<stateful_widgets::ScrollingMessageLinePlan>&
  scrolling_message_line_plans() noexcept;

 private:
  UiRenderResources& resources_;
};

}
