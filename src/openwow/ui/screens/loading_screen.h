#pragma once

#include <SDL2/SDL.h>

#include <string>

namespace openwow::ui::screens {

class LoadingScreen {
 public:
  void SetProgress(float progress);
  float progress() const;

  void SetStatusText(const std::string& text);
  const std::string& status_text() const;

  void SetZoneName(const std::string& zone);
  const std::string& zone_name() const;

  void SetTip(const std::string& tip);
  const std::string& tip() const;

  void Render(SDL_Renderer* renderer, int width, int height, bool failed) const;

 private:
  float progress_{0.0F};
  std::string status_text_;
  std::string zone_name_;
  std::string tip_;
};

}
