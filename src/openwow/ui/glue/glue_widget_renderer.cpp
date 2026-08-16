#include "openwow/ui/glue/glue_widget_renderer.h"

#include <algorithm>

namespace openwow::ui::glue {

namespace {

WidgetRenderStyle DefaultStyleFor(const GlueWidgetState& widget) {
  WidgetRenderStyle style;
  if (widget.kind == "Frame") {
    style.fill_r = 21;
    style.fill_g = 31;
    style.fill_b = 47;
    style.fill_a = 190;
    style.draw_border = true;
    style.border_r = 188;
    style.border_g = 146;
    style.border_b = 72;
    style.border_a = 255;
    return style;
  }
  if (widget.kind == "Button") {
    style.fill_r = 121;
    style.fill_g = 92;
    style.fill_b = 38;
    style.fill_a = 255;
    return style;
  }
  if (widget.kind == "EditBox") {
    style.fill_r = 45;
    style.fill_g = 56;
    style.fill_b = 72;
    style.fill_a = 255;
    return style;
  }
  return style;
}

}

int RenderWidgets(SDL_Renderer* renderer,
                  const GlueWidgetRuntime& runtime,
                  const RenderWidgetOptions& options) {
  int drawn = 0;
  const auto& visible_widgets = runtime.VisibleWidgetsInRenderOrder();
  for (const auto& stored_widget : visible_widgets) {
    if (!options.include_only.empty() &&
        options.include_only.find(stored_widget.name) ==
            options.include_only.end()) {
      continue;
    }
    if (options.exclude.find(stored_widget.name) != options.exclude.end()) {
      continue;
    }
    const auto presentation = runtime.ResolveScrollPresentation(stored_widget);
    if (presentation.clipped_out) {
      continue;
    }
    const auto& widget = presentation.widget;
    if (widget.width <= 0 || widget.height <= 0) {
      continue;
    }

    auto style = DefaultStyleFor(widget);
    const auto style_it = options.style_overrides.find(widget.name);
    if (style_it != options.style_overrides.end()) {
      style = style_it->second;
    }

    const auto alpha_scale = std::clamp(widget.alpha, 0.0F, 1.0F);
    const auto fill_a = static_cast<std::uint8_t>(static_cast<float>(style.fill_a) * alpha_scale);
    const auto border_a = static_cast<std::uint8_t>(static_cast<float>(style.border_a) * alpha_scale);

    SDL_Rect previous_clip{};
    const SDL_bool had_clip = SDL_RenderIsClipEnabled(renderer);
    if (had_clip == SDL_TRUE) {
      SDL_RenderGetClipRect(renderer, &previous_clip);
    }
    if (presentation.clip.has_value()) {
      SDL_Rect clip{presentation.clip->x, presentation.clip->y,
                    presentation.clip->width,
                    presentation.clip->height};
      if (had_clip == SDL_TRUE) {
        SDL_Rect intersection{};
        if (SDL_IntersectRect(&previous_clip, &clip, &intersection) !=
            SDL_TRUE) {
          continue;
        }
        clip = intersection;
      }
      SDL_RenderSetClipRect(renderer, &clip);
    }

    const SDL_Rect rect{widget.x, widget.y, widget.width, widget.height};
    SDL_SetRenderDrawColor(renderer, style.fill_r, style.fill_g, style.fill_b, fill_a);
    SDL_RenderFillRect(renderer, &rect);
    if (style.draw_border) {
      SDL_SetRenderDrawColor(renderer, style.border_r, style.border_g, style.border_b, border_a);
      SDL_RenderDrawRect(renderer, &rect);
    }
    SDL_RenderSetClipRect(renderer,
                         had_clip == SDL_TRUE ? &previous_clip : nullptr);
    ++drawn;
  }
  return drawn;
}

}
