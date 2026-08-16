
#include "openwow/game/mirror_timer.h"

#include <algorithm>

namespace openwow::game {

void MirrorTimerDisplay::SetTimer(MirrorTimerType type, float current,
                                   float max, float scale,
                                   const std::string& label) {
    SetTimer(type, current, max, scale, label, 0);
}

void MirrorTimerDisplay::SetTimer(MirrorTimerType type, float current,
                                   float max, float scale,
                                   const std::string& label,
                                   std::uint32_t spellId) {
    auto key = static_cast<uint8_t>(type);
    bool is_new = (timers_.find(key) == timers_.end());

    auto& s  = timers_[key];
    s.type     = type;
    s.current  = current;
    s.maximum  = max;
    s.scale    = scale;
    s.isPaused = false;
    s.label    = label.empty() ? DefaultLabel(type) : label;
    s.spellId  = spellId;

    float pct = (max > 0.0f) ? (current / max) : 1.0f;
    TimerColorPhase phase = TimerColorPhase::Normal;
    if (pct <= critical_threshold_) {
        phase = TimerColorPhase::Critical;
    } else if (pct <= warning_threshold_) {
        phase = TimerColorPhase::Warning;
    }
    last_phase_[key] = phase;

    if (is_new) {
        FireEvent(TimerEvent::Started, type);
    }
}

void MirrorTimerDisplay::StopTimer(MirrorTimerType type) {
    auto key = static_cast<uint8_t>(type);
    auto it = timers_.find(key);
    if (it != timers_.end()) {
        timers_.erase(it);
        last_phase_.erase(key);
        FireEvent(TimerEvent::Stopped, type);
    }
}

void MirrorTimerDisplay::PauseTimer(MirrorTimerType type) {
    auto key = static_cast<uint8_t>(type);
    auto it = timers_.find(key);
    if (it != timers_.end() && !it->second.isPaused) {
        it->second.isPaused = true;
        FireEvent(TimerEvent::Paused, type);
    }
}

void MirrorTimerDisplay::ResumeTimer(MirrorTimerType type) {
    auto key = static_cast<uint8_t>(type);
    auto it = timers_.find(key);
    if (it != timers_.end() && it->second.isPaused) {
        it->second.isPaused = false;
        FireEvent(TimerEvent::Resumed, type);
    }
}

std::optional<MirrorTimerState> MirrorTimerDisplay::GetTimer(
    MirrorTimerType type) const {
    auto it = timers_.find(static_cast<uint8_t>(type));
    if (it == timers_.end()) return std::nullopt;
    return it->second;
}

std::vector<MirrorTimerState> MirrorTimerDisplay::GetActiveTimers() const {
    std::vector<MirrorTimerState> result;
    result.reserve(timers_.size());
    for (const auto& [_, state] : timers_) {
        result.push_back(state);
    }
    return result;
}

std::size_t MirrorTimerDisplay::GetActiveCount() const {
    return timers_.size();
}

void MirrorTimerDisplay::Update(float dt) {

    std::vector<uint8_t> expired_keys;

    for (auto& [key, s] : timers_) {
        if (s.isPaused) continue;

        float old_current = s.current;
        s.current += s.scale * dt;
        s.current = std::clamp(s.current, 0.0f, s.maximum);

        if (s.current <= 0.0f && old_current > 0.0f && s.scale < 0.0f) {
            FireEvent(TimerEvent::Expired, s.type);

        }

        float pct = (s.maximum > 0.0f) ? (s.current / s.maximum) : 1.0f;
        TimerColorPhase new_phase = TimerColorPhase::Normal;
        if (pct <= critical_threshold_) {
            new_phase = TimerColorPhase::Critical;
        } else if (pct <= warning_threshold_) {
            new_phase = TimerColorPhase::Warning;
        }

        auto& old_phase = last_phase_[key];
        if (new_phase != old_phase) {

            if (new_phase == TimerColorPhase::Warning) {
                FireEvent(TimerEvent::Warning, s.type);
            } else if (new_phase == TimerColorPhase::Critical) {
                FireEvent(TimerEvent::Critical, s.type);
            }
            old_phase = new_phase;
        }
    }
}

float MirrorTimerDisplay::GetTimerPercent(MirrorTimerType type) const {
    auto it = timers_.find(static_cast<uint8_t>(type));
    if (it == timers_.end()) return 0.0f;
    const auto& s = it->second;
    if (s.maximum <= 0.0f) return 0.0f;
    return std::clamp(s.current / s.maximum, 0.0f, 1.0f);
}

float MirrorTimerDisplay::GetTimerRemaining(MirrorTimerType type) const {
    auto it = timers_.find(static_cast<uint8_t>(type));
    if (it == timers_.end()) return 0.0f;
    return std::max(0.0f, it->second.current);
}

bool MirrorTimerDisplay::IsTimerActive(MirrorTimerType type) const {
    return timers_.count(static_cast<uint8_t>(type)) > 0;
}

bool MirrorTimerDisplay::IsTimerPaused(MirrorTimerType type) const {
    auto it = timers_.find(static_cast<uint8_t>(type));
    if (it == timers_.end()) return false;
    return it->second.isPaused;
}

std::string MirrorTimerDisplay::GetTimerLabel(MirrorTimerType type) const {
    auto it = timers_.find(static_cast<uint8_t>(type));
    if (it == timers_.end()) return {};
    return it->second.label;
}

std::uint32_t MirrorTimerDisplay::GetTimerSpellId(MirrorTimerType type) const {
    auto it = timers_.find(static_cast<uint8_t>(type));
    if (it == timers_.end()) return 0;
    return it->second.spellId;
}

void MirrorTimerDisplay::SetWarningThreshold(float fraction) {
    warning_threshold_ = std::clamp(fraction, 0.0f, 1.0f);
}

float MirrorTimerDisplay::GetWarningThreshold() const {
    return warning_threshold_;
}

void MirrorTimerDisplay::SetCriticalThreshold(float fraction) {
    critical_threshold_ = std::clamp(fraction, 0.0f, 1.0f);
}

float MirrorTimerDisplay::GetCriticalThreshold() const {
    return critical_threshold_;
}

TimerColorPhase MirrorTimerDisplay::GetColorPhase(MirrorTimerType type) const {
    float pct = GetTimerPercent(type);
    if (pct <= critical_threshold_) return TimerColorPhase::Critical;
    if (pct <= warning_threshold_) return TimerColorPhase::Warning;
    return TimerColorPhase::Normal;
}

const char* MirrorTimerDisplay::DefaultLabel(MirrorTimerType type) {
    switch (type) {
        case MirrorTimerType::Fatigue:    return "Fatigue";
        case MirrorTimerType::Breath:     return "Breath";
        case MirrorTimerType::FeignDeath: return "Feign Death";
        default:                          return "Unknown";
    }
}

void MirrorTimerDisplay::SetEventCallback(TimerEventCallback cb) {
    event_cb_ = std::move(cb);
}

void MirrorTimerDisplay::FireEvent(TimerEvent event, MirrorTimerType type) {
    if (event_cb_) event_cb_(event, type);
}

void MirrorTimerDisplay::Reset() {
    timers_.clear();
    last_phase_.clear();
    warning_threshold_ = 0.5f;
    critical_threshold_ = 0.2f;
    event_cb_ = nullptr;
}

}
