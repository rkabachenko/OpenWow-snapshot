#pragma once

#include <cstdint>
#include <functional>

namespace openwow::render::ui {
class UiRenderer;
}

namespace openwow::ui {

struct MinimapCompositeCommand {
  float x{0.0f};
  float y{0.0f};
  float width{0.0f};
  float height{0.0f};
  std::uint32_t abgr{0xffffffffu};
};

using MinimapSurfaceSubmitter = std::function<bool(
    render::ui::UiRenderer&, const MinimapCompositeCommand&)>;

}
