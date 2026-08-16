#pragma once

#include "openwow/render/platform/gamma_ramp.h"

struct SDL_Window;

namespace openwow::ui::game {
class CVarSystem;
}

namespace openwow::render {

class GammaController {
 public:
  GammaController() = default;
  ~GammaController();
  GammaController(const GammaController&) = delete;
  GammaController& operator=(const GammaController&) = delete;

  void Register(openwow::ui::game::CVarSystem& cvars, SDL_Window* window);
  void Shutdown(openwow::ui::game::CVarSystem& cvars);

 private:
  void ApplyCurrentRamp();
  void ApplyRamp(const GammaRamp& ramp);
  void CaptureDesktopRamp();

  SDL_Window* window_{};
  bool registered_{};
  bool desktop_gamma_enabled_{};
  float current_gamma_{1.0f};
  GammaRamp desktop_ramp_{BuildGammaRamp(1.0f)};
};

}
