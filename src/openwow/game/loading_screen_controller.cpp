
#include "openwow/game/loading_screen_controller.h"

#include <algorithm>
#include <random>

namespace openwow::game {

void LoadingScreenController::Show(const LoadingScreenInfo& info) {
  info_      = info;
  phase_     = LoadingScreenPhase::FadeIn;
  fadeAlpha_ = 0.0f;
  fadeTimer_ = 0.0f;
}

void LoadingScreenController::Hide() {
  if (phase_ == LoadingScreenPhase::Hidden) return;
  phase_     = LoadingScreenPhase::FadeOut;
  fadeTimer_ = 0.0f;
}

void LoadingScreenController::SetProgress(float value) {
  info_.progress = std::clamp(value, 0.0f, 1.0f);
}

float LoadingScreenController::GetProgress() const {
  return info_.progress;
}

void LoadingScreenController::Update(float deltaTime) {
  if (phase_ == LoadingScreenPhase::Hidden) return;

  fadeTimer_ += deltaTime;
  const float duration = (fadeDuration_ > 0.0f) ? fadeDuration_ : 0.001f;

  switch (phase_) {
    case LoadingScreenPhase::FadeIn: {
      fadeAlpha_ = std::clamp(fadeTimer_ / duration, 0.0f, 1.0f);
      if (fadeTimer_ >= duration) {
        fadeAlpha_ = 1.0f;
        phase_     = LoadingScreenPhase::Loading;
        fadeTimer_ = 0.0f;
      }
      break;
    }
    case LoadingScreenPhase::FadeOut: {
      fadeAlpha_ = std::clamp(1.0f - (fadeTimer_ / duration), 0.0f, 1.0f);
      if (fadeTimer_ >= duration) {
        fadeAlpha_ = 0.0f;
        phase_     = LoadingScreenPhase::Hidden;
        fadeTimer_ = 0.0f;
      }
      break;
    }
    case LoadingScreenPhase::Loading:
    case LoadingScreenPhase::Hidden:
      break;
  }
}

LoadingScreenPhase LoadingScreenController::GetPhase() const {
  return phase_;
}

bool LoadingScreenController::IsVisible() const {
  return phase_ != LoadingScreenPhase::Hidden;
}

bool LoadingScreenController::IsLoading() const {
  return phase_ == LoadingScreenPhase::Loading;
}

const std::string& LoadingScreenController::GetZoneName() const {
  return info_.zoneName;
}

const std::string& LoadingScreenController::GetTipText() const {
  return info_.tipText;
}

void LoadingScreenController::SetTipText(const std::string& text) {
  info_.tipText = text;
}

std::uint32_t LoadingScreenController::GetTextureId() const {
  return info_.textureId;
}

std::uint32_t LoadingScreenController::GetMapId() const {
  return info_.mapId;
}

float LoadingScreenController::GetFadeAlpha() const {
  return fadeAlpha_;
}

void LoadingScreenController::SetFadeDuration(float seconds) {
  fadeDuration_ = std::max(seconds, 0.001f);
}

void LoadingScreenController::SetRandomTip(
    const std::vector<std::string>& tips) {
  if (tips.empty()) return;
  static std::mt19937 rng{std::random_device{}()};
  std::uniform_int_distribution<std::size_t> dist(0, tips.size() - 1);
  info_.tipText = tips[dist(rng)];
}

}
