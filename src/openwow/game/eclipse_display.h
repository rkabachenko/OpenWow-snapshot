#pragma once

#include <cstdint>

namespace openwow::game {

enum class EclipseState : std::uint8_t {
    Neutral = 0,
    Lunar   = 1,
    Solar   = 2,
};

struct EclipseBarColor {
    float r = 0.5f;
    float g = 0.5f;
    float b = 0.5f;
};

class EclipseDisplay {
 public:
    static constexpr int32_t kMinPower = -100;
    static constexpr int32_t kMaxPower =  100;

    void SetPower(std::int32_t power);
    [[nodiscard]] std::int32_t GetPower() const;
    [[nodiscard]] EclipseState GetState() const;
    [[nodiscard]] float GetNormalizedPower() const;

    [[nodiscard]] bool IsLunar() const;
    [[nodiscard]] bool IsSolar() const;
    [[nodiscard]] bool IsNeutral() const;

    [[nodiscard]] float GetLunarPercent() const;
    [[nodiscard]] float GetSolarPercent() const;

    [[nodiscard]] EclipseBarColor GetBarColor() const;

    void SetEnabled(bool enabled);
    [[nodiscard]] bool IsEnabled() const;

    [[nodiscard]] std::int32_t GetDirection() const;
    void SetDirection(std::int32_t dir);

    void Reset();

 private:
    std::int32_t power_     = 0;
    std::int32_t direction_ = 0;
    bool enabled_           = false;
};

}
