#include "openwow/ui/screens/loading_screen.h"

#include <algorithm>
#include <string>

namespace openwow::ui::screens {

void LoadingScreen::SetProgress(float progress) {
  progress_ = std::clamp(progress, 0.0F, 1.0F);
}

float LoadingScreen::progress() const {
  return progress_;
}

void LoadingScreen::SetStatusText(const std::string& text) {
  status_text_ = text;
}

const std::string& LoadingScreen::status_text() const {
  return status_text_;
}

void LoadingScreen::SetZoneName(const std::string& zone) {
  zone_name_ = zone;
}

const std::string& LoadingScreen::zone_name() const {
  return zone_name_;
}

void LoadingScreen::SetTip(const std::string& tip) {
  tip_ = tip;
}

const std::string& LoadingScreen::tip() const {
  return tip_;
}

void LoadingScreen::Render(SDL_Renderer* renderer, int width, int height, bool failed) const {
  if (failed) {
    SDL_SetRenderDrawColor(renderer, 36, 8, 8, 255);
  } else {
    SDL_SetRenderDrawColor(renderer, 6, 10, 24, 255);
  }
  SDL_RenderClear(renderer);

  const int bar_width = std::min(640, std::max(240, width - 200));
  const int bar_height = 26;
  const int bar_x = width / 2 - bar_width / 2;
  const int bar_y = height / 2 + 170;

  SDL_Rect background{bar_x, bar_y, bar_width, bar_height};
  SDL_SetRenderDrawColor(renderer, 34, 34, 40, 255);
  SDL_RenderFillRect(renderer, &background);
  SDL_SetRenderDrawColor(renderer, 122, 103, 61, 255);
  SDL_RenderDrawRect(renderer, &background);

  const int fill = static_cast<int>(static_cast<float>(bar_width - 4) * progress_);
  SDL_Rect progress_rect{bar_x + 2, bar_y + 2, std::max(0, fill), bar_height - 4};
  if (failed) {
    SDL_SetRenderDrawColor(renderer, 150, 42, 32, 255);
  } else {
    SDL_SetRenderDrawColor(renderer, 94, 136, 64, 255);
  }
  SDL_RenderFillRect(renderer, &progress_rect);
}

}
