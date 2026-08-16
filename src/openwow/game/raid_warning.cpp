
#include "openwow/game/raid_warning.h"

#include <algorithm>
#include <cmath>

namespace openwow::game {

void RaidWarningDisplay::ShowWarning(const std::string& message,
                                     const std::string& sender,
                                     std::uint32_t color) {

    if (message.empty()) return;

    RaidWarningEntry entry;
    entry.message    = message;
    entry.senderName = sender;
    entry.timestamp  = elapsedTime_;
    entry.duration   = defaultDuration_;
    entry.remaining  = defaultDuration_;
    entry.color      = color;

    current_ = entry;

    history_.push_back(std::move(entry));
    while (history_.size() > maxHistory_) {
        history_.erase(history_.begin());
    }
}

std::optional<RaidWarningEntry> RaidWarningDisplay::GetCurrentWarning() const {
    return current_;
}

bool RaidWarningDisplay::HasWarning() const {
    return current_.has_value();
}

std::vector<RaidWarningEntry> RaidWarningDisplay::GetWarningHistory() const {
    return history_;
}

std::uint32_t RaidWarningDisplay::GetHistoryCount() const {
    return static_cast<std::uint32_t>(history_.size());
}

void RaidWarningDisplay::SetMaxHistory(std::uint32_t max) {
    maxHistory_ = (max == 0) ? 1 : max;
    while (history_.size() > maxHistory_) {
        history_.erase(history_.begin());
    }
}

void RaidWarningDisplay::SetDefaultDuration(float seconds) {

    defaultDuration_ = std::max(seconds, 0.1f);
}

float RaidWarningDisplay::GetDefaultDuration() const {
    return defaultDuration_;
}

float RaidWarningDisplay::GetFadeProgress() const {
    if (!current_) return 1.0f;
    if (current_->duration <= 0.0f) return 1.0f;

    const float elapsed = current_->duration - current_->remaining;
    return std::clamp(elapsed / current_->duration, 0.0f, 1.0f);
}

void RaidWarningDisplay::Update(float dt) {
    elapsedTime_ += dt;

    if (current_) {
        current_->remaining -= dt;
        if (current_->remaining <= 0.0f) {
            current_.reset();
        }
    }
}

void RaidWarningDisplay::Clear() {
    current_.reset();
}

void RaidWarningDisplay::ClearHistory() {
    history_.clear();
}

}
