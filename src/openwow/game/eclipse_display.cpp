
#include "openwow/game/eclipse_display.h"

#include <algorithm>
#include <cmath>

namespace openwow::game {

void EclipseDisplay::SetPower(std::int32_t power) {
    power_ = std::clamp(power, kMinPower, kMaxPower);
}

std::int32_t EclipseDisplay::GetPower() const {
    return power_;
}

EclipseState EclipseDisplay::GetState() const {
    if (power_ < 0) return EclipseState::Lunar;
    if (power_ > 0) return EclipseState::Solar;
    return EclipseState::Neutral;
}

float EclipseDisplay::GetNormalizedPower() const {

    return static_cast<float>(power_) / static_cast<float>(kMaxPower);
}

bool EclipseDisplay::IsLunar() const {
    return power_ < 0;
}

bool EclipseDisplay::IsSolar() const {
    return power_ > 0;
}

bool EclipseDisplay::IsNeutral() const {
    return power_ == 0;
}

float EclipseDisplay::GetLunarPercent() const {
    if (power_ >= 0) return 0.0f;
    return static_cast<float>(-power_) / static_cast<float>(-kMinPower);
}

float EclipseDisplay::GetSolarPercent() const {
    if (power_ <= 0) return 0.0f;
    return static_cast<float>(power_) / static_cast<float>(kMaxPower);
}

EclipseBarColor EclipseDisplay::GetBarColor() const {
    if (power_ < 0) {

        const float t = GetLunarPercent();
        return {
            0.1f * (1.0f - t),
            0.2f + 0.3f * t,
            0.6f + 0.4f * t
        };
    }
    if (power_ > 0) {

        const float t = GetSolarPercent();
        return {
            0.8f + 0.2f * t,
            0.7f + 0.2f * t,
            0.1f * (1.0f - t)
        };
    }

    return {0.5f, 0.5f, 0.5f};
}

void EclipseDisplay::SetEnabled(bool enabled) {
    enabled_ = enabled;
    if (!enabled) {

        power_     = 0;
        direction_ = 0;
    }
}

bool EclipseDisplay::IsEnabled() const {
    return enabled_;
}

std::int32_t EclipseDisplay::GetDirection() const {
    return direction_;
}

void EclipseDisplay::SetDirection(std::int32_t dir) {
    direction_ = std::clamp(dir, -1, 1);
}

void EclipseDisplay::Reset() {
    power_     = 0;
    direction_ = 0;
    enabled_   = false;
}

}
