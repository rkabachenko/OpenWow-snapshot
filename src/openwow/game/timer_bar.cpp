
#include "openwow/game/timer_bar.h"

#include <algorithm>

namespace openwow::game {

TimerBarSystem& TimerBarSystem::Get() {
    static TimerBarSystem instance;
    return instance;
}

void TimerBarSystem::StartTimer(uint32_t barId, TimerBarType type,
                                const std::string& label, float duration,
                                uint32_t color) {
    std::lock_guard lock(mutex_);
    auto& t = timers_[barId];
    t.barId = barId;
    t.barType = type;
    t.label = label;
    t.totalTime = duration;
    t.remainingTime = duration;
    t.color = color;
    t.isActive = true;
    t.isPaused = false;
}

void TimerBarSystem::StopTimer(uint32_t barId) {
    std::lock_guard lock(mutex_);
    auto it = timers_.find(barId);
    if (it != timers_.end()) {
        it->second.isActive = false;
        it->second.remainingTime = 0.0f;
    }
}

void TimerBarSystem::PauseTimer(uint32_t barId) {
    std::lock_guard lock(mutex_);
    auto it = timers_.find(barId);
    if (it != timers_.end() && it->second.isActive) {
        it->second.isPaused = true;
    }
}

void TimerBarSystem::ResumeTimer(uint32_t barId) {
    std::lock_guard lock(mutex_);
    auto it = timers_.find(barId);
    if (it != timers_.end() && it->second.isActive) {
        it->second.isPaused = false;
    }
}

std::optional<TimerBarEntry> TimerBarSystem::GetTimer(uint32_t barId) const {
    std::lock_guard lock(mutex_);
    auto it = timers_.find(barId);
    if (it == timers_.end()) return std::nullopt;
    return it->second;
}

std::vector<TimerBarEntry> TimerBarSystem::GetActiveTimers() const {
    std::lock_guard lock(mutex_);
    std::vector<TimerBarEntry> result;
    for (auto& [_, t] : timers_) {
        if (t.isActive) result.push_back(t);
    }
    return result;
}

bool TimerBarSystem::IsTimerActive(uint32_t barId) const {
    std::lock_guard lock(mutex_);
    auto it = timers_.find(barId);
    return it != timers_.end() && it->second.isActive;
}

float TimerBarSystem::GetRemainingTime(uint32_t barId) const {
    std::lock_guard lock(mutex_);
    auto it = timers_.find(barId);
    if (it == timers_.end()) return 0.0f;
    return it->second.remainingTime;
}

float TimerBarSystem::GetProgress(uint32_t barId) const {
    std::lock_guard lock(mutex_);
    auto it = timers_.find(barId);
    if (it == timers_.end() || it->second.totalTime <= 0.0f) return 0.0f;
    return it->second.remainingTime / it->second.totalTime;
}

void TimerBarSystem::Update(float dt) {
    std::lock_guard lock(mutex_);
    for (auto& [id, t] : timers_) {
        if (!t.isActive || t.isPaused) continue;
        t.remainingTime -= dt;
        if (t.remainingTime <= 0.0f) {
            t.remainingTime = 0.0f;
            t.isActive = false;
            expired_.push_back(id);
        }
    }
}

bool TimerBarSystem::HasExpired(uint32_t barId) const {
    std::lock_guard lock(mutex_);
    return std::find(expired_.begin(), expired_.end(), barId) != expired_.end();
}

std::vector<uint32_t> TimerBarSystem::GetExpiredTimers() {
    std::lock_guard lock(mutex_);
    auto result = std::move(expired_);
    expired_.clear();
    return result;
}

void TimerBarSystem::SetBreathTimer(float duration) {
    StartTimer(kBreathBarId, TimerBarType::Breath, "Breath",
               duration, GetDefaultColor(TimerBarType::Breath));
}

void TimerBarSystem::SetFatigueTimer(float duration) {
    StartTimer(kFatigueBarId, TimerBarType::Fatigue, "Fatigue",
               duration, GetDefaultColor(TimerBarType::Fatigue));
}

uint32_t TimerBarSystem::GetDefaultColor(TimerBarType type) {
    switch (type) {
        case TimerBarType::Fatigue:     return 0xFFFF0000;
        case TimerBarType::Breath:      return 0xFF0080FF;
        case TimerBarType::FeignDeath:  return 0xFFFFFF00;
        case TimerBarType::Battleground:return 0xFFFF8000;
        case TimerBarType::Custom:      return 0xFFFFFF00;
    }
    return 0xFFFFFF00;
}

void TimerBarSystem::Reset() {
    std::lock_guard lock(mutex_);
    timers_.clear();
    expired_.clear();
}

}
