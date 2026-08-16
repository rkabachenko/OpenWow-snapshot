#pragma once

#include "openwow/ui/glue/glue_widget_runtime.h"

#include <SDL2/SDL.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace openwow::ui::glue {

struct WidgetRenderStyle {
  std::uint8_t fill_r{60};
  std::uint8_t fill_g{60};
  std::uint8_t fill_b{60};
  std::uint8_t fill_a{180};
  bool draw_border{false};
  std::uint8_t border_r{200};
  std::uint8_t border_g{200};
  std::uint8_t border_b{200};
  std::uint8_t border_a{255};
};

struct RenderWidgetOptions {
  std::unordered_set<std::string> include_only;
  std::unordered_set<std::string> exclude;
  std::unordered_map<std::string, WidgetRenderStyle> style_overrides;
};

int RenderWidgets(SDL_Renderer* renderer,
                  const GlueWidgetRuntime& runtime,
                  const RenderWidgetOptions& options);

}
