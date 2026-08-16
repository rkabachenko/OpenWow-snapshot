
#include "openwow/ui/error_display.h"

#include <algorithm>

namespace openwow::ui {

void ErrorDisplay::ShowError(const std::string& message,
                             ErrorPriority priority) {
  if (!enabled_) return;

  ErrorEntry entry;
  entry.message = message;
  entry.color = GetDefaultColor();
  entry.priority = priority;
  entry.duration = default_duration_;
  entry.alpha = 1.0f;
  entry.startTime = elapsed_;

  EnqueueError(entry);
}

void ErrorDisplay::ShowCustomError(const std::string& message,
                                   std::uint32_t color, float duration) {
  if (!enabled_) return;

  ErrorEntry entry;
  entry.message = message;
  entry.color = color;
  entry.priority = ErrorPriority::Normal;
  entry.duration = duration;
  entry.alpha = 1.0f;
  entry.startTime = elapsed_;

  EnqueueError(entry);
}

std::optional<ErrorEntry> ErrorDisplay::GetCurrentError() const {
  return current_;
}

std::vector<std::string> ErrorDisplay::GetErrorHistory() const {
  return history_;
}

std::uint32_t ErrorDisplay::GetHistoryCount() const {
  return static_cast<std::uint32_t>(history_.size());
}

bool ErrorDisplay::IsShowingError() const {
  return current_.has_value();
}

float ErrorDisplay::GetCurrentAlpha() const {
  if (!current_) return 0.0f;
  return current_->alpha;
}

void ErrorDisplay::ClearCurrent() {
  current_.reset();
}

void ErrorDisplay::ClearHistory() {
  history_.clear();
}

void ErrorDisplay::Update(float dt) {
  if (!enabled_) return;

  elapsed_ += dt;

  if (current_) {
    float age = elapsed_ - current_->startTime;
    if (age >= current_->duration) {

      current_.reset();
    } else {

      float fade_start = current_->duration * (2.0f / 3.0f);
      if (age >= fade_start) {
        float fade_range = current_->duration - fade_start;
        current_->alpha = 1.0f - (age - fade_start) / fade_range;
        if (current_->alpha < 0.0f) current_->alpha = 0.0f;
      } else {
        current_->alpha = 1.0f;
      }
    }
  }

  if (!current_ && !pending_.empty()) {
    current_ = pending_.front();
    current_->startTime = elapsed_;
    current_->alpha = 1.0f;
    pending_.pop_front();
  }
}

void ErrorDisplay::Reset() {
  current_.reset();
  pending_.clear();
  history_.clear();
  elapsed_ = 0.0f;
  enabled_ = true;
  default_duration_ = 3.0f;
  max_history_ = 25;
}

void ErrorDisplay::AddCommonErrors() {
  static const char* const kErrors[] = {
      "Not enough mana",
      "Not enough rage",
      "Not enough energy",
      "Out of range",
      "Target not in line of sight",
      "Can't do that while moving",
      "Not enough space",
      "Spell is not ready yet",
      "Item is not ready yet",
      "You can't do that yet",
      "You are dead",
      "Target is friendly",
      "Invalid target",
      "You are in combat",
      "Can't use that while stunned",
      "Inventory is full",
      "Not enough money",
  };
  for (const auto* msg : kErrors) {
    PushToHistory(msg);
  }
}

void ErrorDisplay::PushToHistory(const std::string& message) {
  history_.push_back(message);
  while (history_.size() > max_history_) {
    history_.erase(history_.begin());
  }
}

void ErrorDisplay::EnqueueError(const ErrorEntry& entry) {
  PushToHistory(entry.message);

  if (entry.priority == ErrorPriority::High) {
    if (current_) {

      pending_.push_front(*current_);
    }
    current_ = entry;
    current_->startTime = elapsed_;
    current_->alpha = 1.0f;
    return;
  }

  if (!current_) {
    current_ = entry;
    current_->startTime = elapsed_;
    current_->alpha = 1.0f;
  } else {
    pending_.push_back(entry);
  }
}

}
