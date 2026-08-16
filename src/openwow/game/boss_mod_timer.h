
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

enum class BossTimerType : std::uint8_t {
    Ability = 0,
    Phase = 1,
    Enrage = 2,
    Intermission = 3,
    Berserk = 4,
};

struct BossTimerColor {
    float r{1.0f};
    float g{1.0f};
    float b{1.0f};
};

struct BossTimerBar {
    std::uint32_t timerId{0};
    std::string name;
    float duration{0.0f};
    float remaining{0.0f};
    BossTimerType type{BossTimerType::Ability};
    std::uint32_t iconId{0};
    BossTimerColor color;
    bool isImportant{false};
    bool paused{false};
};

class BossModTimerDisplay {
 public:
    static constexpr std::uint32_t kMaxTimers = 20;

    void AddTimer(BossTimerBar bar);

    void RemoveTimer(std::uint32_t timerId);

    void Update(float deltaTime);

    [[nodiscard]] std::vector<BossTimerBar> GetTimers() const;

    [[nodiscard]] std::vector<BossTimerBar> GetExpiredTimers() const;

    [[nodiscard]] std::optional<BossTimerBar> GetTimer(std::uint32_t timerId) const;

    void PauseTimer(std::uint32_t timerId);

    void ResumeTimer(std::uint32_t timerId);

    void ExtendTimer(std::uint32_t timerId, float extraSeconds);

    void SetTimerColor(std::uint32_t timerId, float r, float g, float b);

    [[nodiscard]] std::uint32_t GetActiveCount() const;

    void ClearAll();

    [[nodiscard]] std::optional<BossTimerBar> GetShortestTimer() const;

    [[nodiscard]] static BossTimerColor DefaultColorForType(BossTimerType type);

 private:
    std::vector<BossTimerBar> timers_;
    std::vector<BossTimerBar> expired_;
};

}
