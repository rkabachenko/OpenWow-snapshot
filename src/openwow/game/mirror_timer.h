
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::game {

enum class MirrorTimerType : uint8_t {
    Fatigue    = 0,
    Breath     = 1,
    FeignDeath = 2,
};

enum class TimerColorPhase : uint8_t {
    Normal   = 0,
    Warning  = 1,
    Critical = 2,
};

struct MirrorTimerState {
    MirrorTimerType type      = MirrorTimerType::Fatigue;
    float           current   = 0.0f;
    float           maximum   = 0.0f;
    float           scale     = -1.0f;
    bool            isPaused  = false;
    std::string     label;
    std::uint32_t   spellId   = 0;
};

enum class TimerEvent : uint8_t {
    Started    = 0,
    Stopped    = 1,
    Expired    = 2,
    Paused     = 3,
    Resumed    = 4,
    Warning    = 5,
    Critical   = 6,
};

using TimerEventCallback = std::function<void(TimerEvent, MirrorTimerType)>;

class MirrorTimerDisplay {
public:

    void SetTimer(MirrorTimerType type, float current, float max,
                  float scale, const std::string& label);

    void SetTimer(MirrorTimerType type, float current, float max,
                  float scale, const std::string& label, std::uint32_t spellId);

    void StopTimer(MirrorTimerType type);

    void PauseTimer(MirrorTimerType type);

    void ResumeTimer(MirrorTimerType type);

    [[nodiscard]] std::optional<MirrorTimerState> GetTimer(MirrorTimerType type) const;

    [[nodiscard]] std::vector<MirrorTimerState> GetActiveTimers() const;

    [[nodiscard]] std::size_t GetActiveCount() const;

    void Update(float dt);

    [[nodiscard]] float GetTimerPercent(MirrorTimerType type) const;

    [[nodiscard]] float GetTimerRemaining(MirrorTimerType type) const;

    [[nodiscard]] bool IsTimerActive(MirrorTimerType type) const;

    [[nodiscard]] bool IsTimerPaused(MirrorTimerType type) const;

    [[nodiscard]] std::string GetTimerLabel(MirrorTimerType type) const;

    [[nodiscard]] std::uint32_t GetTimerSpellId(MirrorTimerType type) const;

    void SetWarningThreshold(float fraction);
    [[nodiscard]] float GetWarningThreshold() const;

    void SetCriticalThreshold(float fraction);
    [[nodiscard]] float GetCriticalThreshold() const;

    [[nodiscard]] TimerColorPhase GetColorPhase(MirrorTimerType type) const;

    [[nodiscard]] static const char* DefaultLabel(MirrorTimerType type);

    void SetEventCallback(TimerEventCallback cb);

    void Reset();

private:
    void FireEvent(TimerEvent event, MirrorTimerType type);

    std::unordered_map<uint8_t, MirrorTimerState> timers_;

    float warning_threshold_  = 0.5f;
    float critical_threshold_ = 0.2f;

    std::unordered_map<uint8_t, TimerColorPhase> last_phase_;

    TimerEventCallback event_cb_;
};

}
