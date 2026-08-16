#pragma once

#include "openwow/ui/game/runtime/render/ui_render_resources.h"

namespace openwow::ui::game::runtime::render {

class ModelRegionRenderer final {
 public:
  explicit ModelRegionRenderer(UiRenderResources& resources) noexcept;

  [[nodiscard]] std::unique_ptr<openwow::render::ModelPortrait>& surface(
      const std::string& key);
  [[nodiscard]] std::unordered_map<std::string, std::string>&
  diagnostics() noexcept;

 private:
  UiRenderResources& resources_;
};

}
