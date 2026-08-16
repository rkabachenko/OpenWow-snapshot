
#include "openwow/ui/loading_screen.h"

#include <algorithm>

namespace openwow::ui {

void LoadingScreen::Show(LoadingScreenType type, const std::string& mapName,
                         const std::string& flavorText) {
  visible_ = true;
  type_ = type;
  map_name_ = mapName;
  flavor_text_ = flavorText;
  progress_ = 0.0f;
  elapsed_time_ = 0.0f;
  status_text_.clear();

  if (!tips_.empty()) {
    current_tip_ =
        tips_[static_cast<std::size_t>(random_.Next()) % tips_.size()];
  } else {
    current_tip_.clear();
  }
}

void LoadingScreen::Hide() {
  visible_ = false;
}

bool LoadingScreen::IsVisible() const {
  return visible_;
}

void LoadingScreen::SetProgress(float progress) {
  progress_ = std::clamp(progress, 0.0f, 1.0f);
}

float LoadingScreen::GetProgress() const {
  return progress_;
}

void LoadingScreen::SetStatusText(const std::string& text) {
  status_text_ = text;
}

std::string LoadingScreen::GetStatusText() const {
  return status_text_;
}

LoadingScreenType LoadingScreen::GetType() const {
  return type_;
}

std::string LoadingScreen::GetMapName() const {
  return map_name_;
}

std::string LoadingScreen::GetFlavorText() const {
  return flavor_text_;
}

std::string LoadingScreen::GetBackgroundTexture() const {
  return background_texture_;
}

void LoadingScreen::SetBackgroundTexture(const std::string& path) {
  background_texture_ = path;
}

std::string LoadingScreen::GetTipText() const {
  return current_tip_;
}

void LoadingScreen::SetTips(const std::vector<std::string>& tips) {
  tips_ = tips;
}

std::string LoadingScreen::GetRandomTip() const {
  if (tips_.empty()) return {};
  return tips_[static_cast<std::size_t>(random_.Next()) % tips_.size()];
}

void LoadingScreen::Update(float dt) {
  if (visible_) {
    elapsed_time_ += dt;
  }
}

float LoadingScreen::GetElapsedTime() const {
  return elapsed_time_;
}

void LoadingScreen::Reset() {
  visible_ = false;
  type_ = LoadingScreenType::Initial;
  progress_ = 0.0f;
  map_name_.clear();
  flavor_text_.clear();
  status_text_.clear();
  background_texture_.clear();
  current_tip_.clear();
  tips_.clear();
  elapsed_time_ = 0.0f;
}

}
