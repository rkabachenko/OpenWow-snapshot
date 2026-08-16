#pragma once

#include "openwow/ui/game/runtime/render/ui_render_resources.h"

namespace openwow::ui::game::runtime::render {

class TextureRegionRenderer final {
 public:
  explicit TextureRegionRenderer(UiRenderResources& resources) noexcept;

  [[nodiscard]] const UiTextureLoad& texture_loader() const noexcept;
  [[nodiscard]] stateful_widgets::SliderState& slider_state() noexcept;
  [[nodiscard]] stateful_widgets::CooldownState& cooldown_state() noexcept;
  [[nodiscard]] openwow::render::PortraitRenderer* portrait_renderer() noexcept;

 private:
  UiRenderResources& resources_;
};

}
