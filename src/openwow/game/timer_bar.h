
#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::game {

enum class TimerBarType : uint8_t {
    Fatigue = 0,
    Breath,
    FeignDeath,
    Battleground,
    Custom,
};

struct TimerBarEntry {
    uint32_t barId = 0;
    TimerBarType barType = TimerBarType::Custom;
    std::string label;
    float totalTime = 0.0f;
    float remainingTime = 0.0f;
    uint32_t color = 0xFFFFFF00;
    bool isActive = false;
    bool isPaused = false;
};

class TimerBarSystem {
public:
    static TimerBarSystem& Get();

    void StartTimer(uint32_t barId, TimerBarType type,
                    const std::string& label, float duration,
                    uint32_t color = 0xFFFFFF00);
    void StopTimer(uint32_t barId);

    void PauseTimer(uint32_t barId);
    void ResumeTimer(uint32_t barId);

    std::optional<TimerBarEntry> GetTimer(uint32_t barId) const;
    std::vector<TimerBarEntry> GetActiveTimers() const;

    bool IsTimerActive(uint32_t barId) const;
    float GetRemainingTime(uint32_t barId) const;

    float GetProgress(uint32_t barId) const;

    void Update(float dt);

    bool HasExpired(uint32_t barId) const;

    std::vector<uint32_t> GetExpiredTimers();

    void SetBreathTimer(float duration);
    void SetFatigueTimer(float duration);

    static uint32_t GetDefaultColor(TimerBarType type);

    void Reset();

private:
    TimerBarSystem() = default;

    mutable std::mutex mutex_;
    std::unordered_map<uint32_t, TimerBarEntry> timers_;
    std::vector<uint32_t> expired_;

    static constexpr uint32_t kBreathBarId  = 1;
    static constexpr uint32_t kFatigueBarId = 2;
};

}
