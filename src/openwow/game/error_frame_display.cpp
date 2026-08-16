
#include "openwow/game/error_frame_display.h"

#include <algorithm>

namespace openwow::game {

UIErrorColor ErrorFrameDisplay::GetDefaultColor(UIErrorType type) {
  switch (type) {
    case UIErrorType::Error:  return {1.0f, 0.1f, 0.1f};
    case UIErrorType::Info:   return {1.0f, 1.0f, 0.5f};
    case UIErrorType::Money:  return {1.0f, 0.82f, 0.0f};
    case UIErrorType::System: return {1.0f, 1.0f, 1.0f};
  }
  return {1.0f, 1.0f, 1.0f};
}

void ErrorFrameDisplay::PushMessage(const std::string& message,
                                    UIErrorType type) {
  if (!enabled_) return;

  for (const auto& e : active_) {
    if (e.message == message &&
        (clock_ - e.timestamp) < kDedupWindow) {
      return;
    }
  }

  UIErrorEntry entry;
  entry.message         = message;
  entry.type            = type;
  entry.timestamp       = clock_;
  entry.displayDuration = 3.0f;
  entry.fadeProgress    = 0.0f;
  entry.color           = GetDefaultColor(type);

  if (active_.size() >= kMaxVisible) {
    active_.erase(active_.begin());
  }
  active_.push_back(entry);
  AddToHistory(entry);
}

void ErrorFrameDisplay::ShowError(const std::string& message) {
  PushMessage(message, UIErrorType::Error);
}

void ErrorFrameDisplay::ShowInfo(const std::string& message) {
  PushMessage(message, UIErrorType::Info);
}

void ErrorFrameDisplay::ShowMoney(const std::string& message) {
  PushMessage(message, UIErrorType::Money);
}

void ErrorFrameDisplay::ShowSystem(const std::string& message) {
  PushMessage(message, UIErrorType::System);
}

void ErrorFrameDisplay::ShowCustom(const std::string& message,
                                   UIErrorType type) {
  PushMessage(message, type);
}

void ErrorFrameDisplay::Update(float deltaTime) {
  clock_ += static_cast<double>(deltaTime);

  for (auto& e : active_) {
    const double age = clock_ - e.timestamp;
    const float fadeStart = e.displayDuration - kFadeTime;
    if (age >= e.displayDuration) {
      e.fadeProgress = 1.0f;
    } else if (age > fadeStart) {
      e.fadeProgress =
          static_cast<float>(age - fadeStart) / kFadeTime;
    } else {
      e.fadeProgress = 0.0f;
    }
  }

  active_.erase(
      std::remove_if(active_.begin(), active_.end(),
                     [](const UIErrorEntry& e) {
                       return e.fadeProgress >= 1.0f;
                     }),
      active_.end());
}

std::vector<UIErrorEntry> ErrorFrameDisplay::GetActiveMessages() const {
  return active_;
}

std::optional<UIErrorEntry> ErrorFrameDisplay::GetCurrentMessage() const {
  if (active_.empty()) return std::nullopt;
  return active_.back();
}

std::uint32_t ErrorFrameDisplay::GetMessageCount() const {
  return static_cast<std::uint32_t>(active_.size());
}

void ErrorFrameDisplay::ClearAll() {
  active_.clear();
}

void ErrorFrameDisplay::SetEnabled(bool enabled) {
  enabled_ = enabled;
}

void ErrorFrameDisplay::AddToHistory(const UIErrorEntry& entry) {
  if (history_.size() >= kMaxHistory) {
    history_.pop_front();
  }
  history_.push_back(entry);
}

std::vector<UIErrorEntry> ErrorFrameDisplay::GetHistory(
    std::uint32_t count) const {
  std::vector<UIErrorEntry> result;
  const auto n =
      std::min(static_cast<std::size_t>(count), history_.size());
  for (std::size_t i = history_.size() - n; i < history_.size(); ++i) {
    result.push_back(history_[i]);
  }
  return result;
}

}
