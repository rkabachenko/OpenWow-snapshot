
#include "openwow/ui/game/ui_error_manager.h"

#include <algorithm>

namespace openwow::ui {

UIErrorManager& UIErrorManager::Get() {
  static UIErrorManager instance;
  return instance;
}

void UIErrorManager::AddErrorMessage(const std::string& text) {
  if (!errors_enabled_) return;
  AddMessage(UIMessageType::Error, text,
             0xFFFF0000, 3.0f,
             2.5f, 12.0f);
}

void UIErrorManager::AddInfoMessage(const std::string& text) {
  AddMessage(UIMessageType::Info, text,
             0xFFFFFF00, 3.0f,
             2.5f, 12.0f);
}

void UIErrorManager::AddRaidWarning(const std::string& text) {
  AddMessage(UIMessageType::RaidWarning, text,
             0xFFFF0000, 5.0f,
             3.5f, 20.0f);
}

void UIErrorManager::AddBossEmote(const std::string& text) {
  AddMessage(UIMessageType::BossEmote, text,
             0xFFFFFF00, 5.0f,
             3.5f, 20.0f);
}

void UIErrorManager::AddZoneText(const std::string& zone,
                                 const std::string& sub_zone) {
  AddMessage(UIMessageType::ZoneText, zone,
             0xFFFFD100, 4.0f,
             3.0f, 32.0f);

  if (!sub_zone.empty()) {
    AddMessage(UIMessageType::SubZoneText, sub_zone,
               0xFFFFD100, 4.0f,
               3.0f, 18.0f);
  }
}

void UIErrorManager::SetSuppressDuration(float seconds) {
  std::lock_guard lock(mutex_);
  suppress_duration_ = seconds;
}

float UIErrorManager::GetSuppressDuration() const {
  std::lock_guard lock(mutex_);
  return suppress_duration_;
}

void UIErrorManager::Update(float delta_time) {
  std::lock_guard lock(mutex_);
  current_time_ += delta_time;

  for (auto& msg : active_) {
    msg.lifetime += delta_time;

    if (!msg.hold && msg.lifetime >= msg.fade_start && !msg.is_fading) {
      msg.is_fading = true;
    }

    if (msg.is_fading) {
      float fade_duration = msg.max_lifetime - msg.fade_start;
      if (fade_duration > 0.0f) {
        float t = (msg.lifetime - msg.fade_start) / fade_duration;
        msg.alpha = std::max(0.0f, 1.0f - t);
      } else {
        msg.alpha = 0.0f;
      }
    }
  }

  active_.erase(
      std::remove_if(active_.begin(), active_.end(),
                     [](const UIMessage& m) {
                       return !m.hold && m.lifetime >= m.max_lifetime;
                     }),
      active_.end());

  for (auto it = suppress_map_.begin(); it != suppress_map_.end();) {
    if (current_time_ - it->second > suppress_duration_ * 2.0f) {
      it = suppress_map_.erase(it);
    } else {
      ++it;
    }
  }
}

const std::vector<UIMessage>& UIErrorManager::GetActiveMessages() const {
  return active_;
}

const UIMessage* UIErrorManager::GetCurrentError() const {
  std::lock_guard lock(mutex_);
  return FindByType(UIMessageType::Error);
}

const UIMessage* UIErrorManager::GetCurrentInfo() const {
  std::lock_guard lock(mutex_);
  return FindByType(UIMessageType::Info);
}

const UIMessage* UIErrorManager::GetCurrentRaidWarning() const {
  std::lock_guard lock(mutex_);
  return FindByType(UIMessageType::RaidWarning);
}

const UIMessage* UIErrorManager::GetCurrentZoneText() const {
  std::lock_guard lock(mutex_);
  return FindByType(UIMessageType::ZoneText);
}

const std::vector<std::string>& UIErrorManager::GetErrorHistory() const {
  return error_history_;
}

void UIErrorManager::SetErrorsEnabled(bool enabled) {
  std::lock_guard lock(mutex_);
  errors_enabled_ = enabled;
}

bool UIErrorManager::AreErrorsEnabled() const {
  std::lock_guard lock(mutex_);
  return errors_enabled_;
}

void UIErrorManager::Clear() {
  std::lock_guard lock(mutex_);
  active_.clear();
}

void UIErrorManager::Reset() {
  std::lock_guard lock(mutex_);
  active_.clear();
  error_history_.clear();
  suppress_map_.clear();
  current_time_ = 0;
  errors_enabled_ = true;
  suppress_duration_ = 1.0f;
}

void UIErrorManager::AddMessage(UIMessageType type, const std::string& text,
                                std::uint32_t color, float max_lifetime,
                                float fade_start, float font_size) {
  std::lock_guard lock(mutex_);

  if (IsSuppressed(text)) return;

  UIMessage msg;
  msg.type = type;
  msg.text = text;
  msg.color = color;
  msg.max_lifetime = max_lifetime;
  msg.fade_start = fade_start;
  msg.font_size = font_size;

  active_.push_back(msg);

  suppress_map_[text] = current_time_;

  if (type == UIMessageType::Error) {
    error_history_.push_back(text);
    if (error_history_.size() > kMaxErrorHistory) {
      error_history_.erase(error_history_.begin());
    }
  }
}

bool UIErrorManager::IsSuppressed(const std::string& text) const {
  auto it = suppress_map_.find(text);
  if (it == suppress_map_.end()) return false;
  return (current_time_ - it->second) < suppress_duration_;
}

const UIMessage* UIErrorManager::FindByType(UIMessageType type) const {

  for (auto it = active_.rbegin(); it != active_.rend(); ++it) {
    if (it->type == type) return &(*it);
  }
  return nullptr;
}

}
