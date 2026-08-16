
#include "openwow/game/aura_timer.h"

#include <algorithm>

namespace openwow::game {

void AuraTimerDisplay::AddTimer(const AuraTimerEntry& entry) {
    for (auto& t : timers_) {
        if (t.spellId == entry.spellId) {
            t = entry;
            return;
        }
    }
    timers_.push_back(entry);
}

void AuraTimerDisplay::RemoveTimer(uint32_t spellId) {
    timers_.erase(
        std::remove_if(timers_.begin(), timers_.end(),
                       [spellId](const AuraTimerEntry& t) {
                           return t.spellId == spellId;
                       }),
        timers_.end());
}

std::optional<AuraTimerEntry> AuraTimerDisplay::GetTimer(uint32_t spellId) const {
    for (const auto& t : timers_) {
        if (t.spellId == spellId) return t;
    }
    return std::nullopt;
}

std::vector<AuraTimerEntry> AuraTimerDisplay::GetAllTimers() const {
    return timers_;
}

float AuraTimerDisplay::GetProgress(uint32_t spellId) const {
    for (const auto& t : timers_) {
        if (t.spellId == spellId) {
            if (t.totalDuration <= 0.0f) return 0.0f;
            float elapsed = t.totalDuration - t.remainingDuration;
            float progress = elapsed / t.totalDuration;
            if (progress < 0.0f) return 0.0f;
            if (progress > 1.0f) return 1.0f;
            return progress;
        }
    }
    return 0.0f;
}

bool AuraTimerDisplay::IsExpired(uint32_t spellId) const {
    for (const auto& t : timers_) {
        if (t.spellId == spellId) return t.remainingDuration <= 0.0f;
    }
    return true;
}

std::vector<AuraTimerEntry> AuraTimerDisplay::GetExpiringSoon(float threshold) const {
    std::vector<AuraTimerEntry> result;
    for (const auto& t : timers_) {
        if (t.remainingDuration > 0.0f && t.remainingDuration < threshold) {
            result.push_back(t);
        }
    }
    return result;
}

void AuraTimerDisplay::Update(float dt) {
    for (auto& t : timers_) {
        if (t.remainingDuration > 0.0f) {
            t.remainingDuration -= dt;
            if (t.remainingDuration < 0.0f) t.remainingDuration = 0.0f;
        }
    }

    timers_.erase(
        std::remove_if(timers_.begin(), timers_.end(),
                       [](const AuraTimerEntry& t) {
                           return t.remainingDuration <= 0.0f;
                       }),
        timers_.end());
}

uint32_t AuraTimerDisplay::GetActiveCount() const {
    return static_cast<uint32_t>(timers_.size());
}

std::vector<AuraTimerEntry> AuraTimerDisplay::GetBuffTimers() const {
    std::vector<AuraTimerEntry> result;
    for (const auto& t : timers_) {
        if (!t.isDebuff) result.push_back(t);
    }
    return result;
}

std::vector<AuraTimerEntry> AuraTimerDisplay::GetDebuffTimers() const {
    std::vector<AuraTimerEntry> result;
    for (const auto& t : timers_) {
        if (t.isDebuff) result.push_back(t);
    }
    return result;
}

void AuraTimerDisplay::SetTimerVisible(bool visible) {
    timerVisible_ = visible;
}

bool AuraTimerDisplay::IsTimerVisible() const {
    return timerVisible_;
}

void AuraTimerDisplay::Clear() {
    timers_.clear();
}

}
