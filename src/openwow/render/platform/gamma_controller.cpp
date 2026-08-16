#include "openwow/render/platform/gamma_controller.h"

#include "openwow/core/decimal_parse.h"
#include "openwow/ui/game/cvar_system.h"

#include <SDL2/SDL.h>

#include <string>
#include <string_view>

namespace openwow::render {

namespace {

bool ParseDesktopGammaEnabledValue(std::string_view value) {
  return openwow::core::ParseSignedDecimalLikeSub76F0D0(value) != 0u;
}

float ParseGammaCorrectedValue(std::string_view value) {
  const double parsed = openwow::core::ParseFloatLikeSub76FB80(value);
  return parsed < 0.001 ? 1.0f : static_cast<float>(parsed);
}

}

GammaController::~GammaController() {
  if (window_ != nullptr) {
    (void)SDL_SetWindowGammaRamp(
        window_, desktop_ramp_.red.data(), desktop_ramp_.green.data(),
        desktop_ramp_.blue.data());
  }
}

void GammaController::Register(openwow::ui::game::CVarSystem& cvars,
                               SDL_Window* window) {
  window_ = window;
  desktop_gamma_enabled_ =
      ParseDesktopGammaEnabledValue(cvars.GetCVar("DesktopGamma"));
  current_gamma_ = ParseGammaCorrectedValue(cvars.GetCVar("Gamma"));
  CaptureDesktopRamp();
  registered_ = true;
  cvars.SetValidationCallback(
      "Gamma",
      [this](const std::string&, const std::string&,
             const std::string& new_value) {
        current_gamma_ = ParseGammaCorrectedValue(new_value);
        if (!desktop_gamma_enabled_) ApplyCurrentRamp();
        return true;
      });
  cvars.SetValidationCallback(
      "DesktopGamma",
      [this](const std::string&, const std::string&,
             const std::string& new_value) {
        desktop_gamma_enabled_ = ParseDesktopGammaEnabledValue(new_value);
        ApplyCurrentRamp();
        return true;
      });
}

void GammaController::Shutdown(openwow::ui::game::CVarSystem& cvars) {
  if (!registered_) return;
  cvars.SetValidationCallback("Gamma", {});
  cvars.SetValidationCallback("DesktopGamma", {});
  ApplyRamp(desktop_ramp_);
  window_ = nullptr;
  registered_ = false;
}

void GammaController::CaptureDesktopRamp() {
  desktop_ramp_ = BuildGammaRamp(1.0f);
  if (window_ != nullptr) {
    (void)SDL_GetWindowGammaRamp(
        window_, desktop_ramp_.red.data(), desktop_ramp_.green.data(),
        desktop_ramp_.blue.data());
  }
}

void GammaController::ApplyCurrentRamp() {
  ApplyRamp(desktop_gamma_enabled_
                ? desktop_ramp_
                : BuildCalibratedGammaRamp(desktop_ramp_, current_gamma_));
}

void GammaController::ApplyRamp(const GammaRamp& ramp) {
  if (window_ == nullptr) return;
  (void)SDL_SetWindowGammaRamp(window_, ramp.red.data(), ramp.green.data(),
                               ramp.blue.data());
}

}
