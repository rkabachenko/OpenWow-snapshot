#include "openwow/ui/game/runtime/render/model_region_renderer.h"

namespace openwow::ui::game::runtime::render {

ModelRegionRenderer::ModelRegionRenderer(UiRenderResources& resources) noexcept
    : resources_(resources) {}

std::unique_ptr<openwow::render::ModelPortrait>& ModelRegionRenderer::surface(
    const std::string& key) {
  return resources_.AcquireModelSurface(key);
}

std::unordered_map<std::string, std::string>&
ModelRegionRenderer::diagnostics() noexcept {
  return resources_.model_diagnostics();
}

}
