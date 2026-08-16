
#include "openwow/game/boss_mod_timer.h"

#include <algorithm>

namespace openwow::game {

BossTimerColor BossModTimerDisplay::DefaultColorForType(BossTimerType type) {
    switch (type) {
        case BossTimerType::Ability:
            return {1.0f, 0.6f, 0.0f};
        case BossTimerType::Phase:
            return {0.2f, 0.4f, 1.0f};
        case BossTimerType::Enrage:
            return {1.0f, 0.0f, 0.0f};
        case BossTimerType::Intermission:
            return {0.0f, 1.0f, 1.0f};
        case BossTimerType::Berserk:
            return {0.55f, 0.0f, 0.0f};
    }
    return {1.0f, 0.6f, 0.0f};
}

void BossModTimerDisplay::AddTimer(BossTimerBar bar) {

    if (bar.color.r == 1.0f && bar.color.g == 1.0f && bar.color.b == 1.0f) {
        bar.color = DefaultColorForType(bar.type);
    }

    for (auto& t : timers_) {
        if (t.timerId == bar.timerId) {
            t = bar;
            return;
        }
    }

    if (timers_.size() >= kMaxTimers) return;

    timers_.push_back(std::move(bar));
}

void BossModTimerDisplay::RemoveTimer(std::uint32_t timerId) {
    timers_.erase(
        std::remove_if(timers_.begin(), timers_.end(),
                       [timerId](const BossTimerBar& b) {
                           return b.timerId == timerId;
                       }),
        timers_.end());
}

void BossModTimerDisplay::Update(float deltaTime) {
    for (auto& t : timers_) {
        if (!t.paused) {
            t.remaining -= deltaTime;
        }
    }

    auto it = std::partition(timers_.begin(), timers_.end(),
                             [](const BossTimerBar& b) {
                                 return b.remaining > 0.0f;
                             });
    for (auto e = it; e != timers_.end(); ++e) {
        expired_.push_back(*e);
    }
    timers_.erase(it, timers_.end());
}

std::vector<BossTimerBar> BossModTimerDisplay::GetTimers() const {
    auto sorted = timers_;
    std::sort(sorted.begin(), sorted.end(),
              [](const BossTimerBar& a, const BossTimerBar& b) {
                  return a.remaining < b.remaining;
              });
    return sorted;
}

std::vector<BossTimerBar> BossModTimerDisplay::GetExpiredTimers() const {
    return expired_;
}

std::optional<BossTimerBar> BossModTimerDisplay::GetTimer(
    std::uint32_t timerId) const {
    for (const auto& t : timers_) {
        if (t.timerId == timerId) return t;
    }
    return std::nullopt;
}

void BossModTimerDisplay::PauseTimer(std::uint32_t timerId) {
    for (auto& t : timers_) {
        if (t.timerId == timerId) {
            t.paused = true;
            return;
        }
    }
}

void BossModTimerDisplay::ResumeTimer(std::uint32_t timerId) {
    for (auto& t : timers_) {
        if (t.timerId == timerId) {
            t.paused = false;
            return;
        }
    }
}

void BossModTimerDisplay::ExtendTimer(std::uint32_t timerId,
                                      float extraSeconds) {
    for (auto& t : timers_) {
        if (t.timerId == timerId) {
            t.remaining += extraSeconds;
            t.duration += extraSeconds;
            return;
        }
    }
}

void BossModTimerDisplay::SetTimerColor(std::uint32_t timerId, float r,
                                        float g, float b) {
    for (auto& t : timers_) {
        if (t.timerId == timerId) {
            t.color = {r, g, b};
            return;
        }
    }
}

std::uint32_t BossModTimerDisplay::GetActiveCount() const {
    return static_cast<std::uint32_t>(timers_.size());
}

void BossModTimerDisplay::ClearAll() {
    timers_.clear();
    expired_.clear();
}

std::optional<BossTimerBar> BossModTimerDisplay::GetShortestTimer() const {
    if (timers_.empty()) return std::nullopt;
    auto it = std::min_element(
        timers_.begin(), timers_.end(),
        [](const BossTimerBar& a, const BossTimerBar& b) {
            return a.remaining < b.remaining;
        });
    return *it;
}

}
