#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace openwow::game {

struct AuraTimerEntry {
    uint32_t spellId          = 0;
    float    totalDuration    = 0.0f;
    float    remainingDuration = 0.0f;
    uint32_t stacks           = 0;
    bool     isDebuff         = false;
};

class AuraTimerDisplay {
public:

    void AddTimer(const AuraTimerEntry& entry);

    void RemoveTimer(uint32_t spellId);

    [[nodiscard]] std::optional<AuraTimerEntry> GetTimer(uint32_t spellId) const;

    [[nodiscard]] std::vector<AuraTimerEntry> GetAllTimers() const;

    [[nodiscard]] float GetProgress(uint32_t spellId) const;

    [[nodiscard]] bool IsExpired(uint32_t spellId) const;

    [[nodiscard]] std::vector<AuraTimerEntry> GetExpiringSoon(float threshold = 5.0f) const;

    void Update(float dt);

    [[nodiscard]] uint32_t GetActiveCount() const;

    [[nodiscard]] std::vector<AuraTimerEntry> GetBuffTimers() const;

    [[nodiscard]] std::vector<AuraTimerEntry> GetDebuffTimers() const;

    void SetTimerVisible(bool visible);
    [[nodiscard]] bool IsTimerVisible() const;

    void Clear();

private:
    std::vector<AuraTimerEntry> timers_;
    bool timerVisible_ = true;
};

}
