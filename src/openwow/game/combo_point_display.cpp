
#include "openwow/game/combo_point_display.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace openwow::game {

void ComboPointDisplay::SetComboPoints(std::uint8_t points) {
    points_ = std::min(points, kMaxComboPoints);
}

std::uint8_t ComboPointDisplay::GetComboPoints() const {
    return points_;
}

void ComboPointDisplay::SetTarget(const ObjectGuid& target) {
    target_ = target;
}

ObjectGuid ComboPointDisplay::GetTarget() const {
    return target_;
}

bool ComboPointDisplay::HasTarget() const {
    return !target_.IsEmpty();
}

bool ComboPointDisplay::IsMaxed() const {
    return points_ >= kMaxComboPoints;
}

std::uint8_t ComboPointDisplay::GetMaxComboPoints() const {
    return kMaxComboPoints;
}

void ComboPointDisplay::SetEnabled(bool enabled) {
    enabled_ = enabled;
}

bool ComboPointDisplay::IsEnabled() const {
    return enabled_;
}

ComboPointColor ComboPointDisplay::GetPointColor(std::uint8_t point) const {
    if (point >= kMaxComboPoints || point >= points_) {
        return {0.2f, 0.2f, 0.2f};
    }

    const float intensity = static_cast<float>(point + 1) /
                            static_cast<float>(kMaxComboPoints);
    return {0.8f + 0.2f * intensity, 0.1f * (1.0f - intensity), 0.0f};
}

float ComboPointDisplay::GetAnimationProgress(std::uint8_t point) const {
    if (point >= kMaxComboPoints) return 0.0f;
    return anim_progress_[point];
}

void ComboPointDisplay::ClearComboPoints() {
    points_ = 0;
    target_ = ObjectGuid{};
    std::memset(anim_progress_, 0, sizeof(anim_progress_));
}

void ComboPointDisplay::OnPointGain() {
    if (points_ == 0) return;

    const auto idx = static_cast<std::uint8_t>(points_ - 1);
    if (idx < kMaxComboPoints) {
        anim_progress_[idx] = 1.0f;
    }
}

void ComboPointDisplay::Reset() {
    target_ = ObjectGuid{};
    points_ = 0;
    enabled_ = false;
    std::memset(anim_progress_, 0, sizeof(anim_progress_));
}

void ComboPointDisplay::UpdateAnimation(float dt) {
    if (!enabled_) return;

    constexpr float kDecayRate = 2.0f;
    for (std::uint8_t i = 0; i < kMaxComboPoints; ++i) {
        if (anim_progress_[i] > 0.0f) {
            anim_progress_[i] -= dt * kDecayRate;
            if (anim_progress_[i] < 0.0f) {
                anim_progress_[i] = 0.0f;
            }
        }
    }
}

float ComboPointDisplay::GetFlashIntensity() const {
    if (points_ == 0 || !enabled_) return 0.0f;

    auto idx = static_cast<std::uint8_t>(points_ - 1);
    if (idx >= kMaxComboPoints) return 0.0f;

    float progress = anim_progress_[idx];
    if (progress <= 0.0f) return 0.0f;

    return std::sin(progress * 3.14159265f) * 0.8f;
}

bool ComboPointDisplay::ShouldFlash() const {
    if (points_ == 0 || !enabled_) return false;
    auto idx = static_cast<std::uint8_t>(points_ - 1);
    if (idx >= kMaxComboPoints) return false;
    return anim_progress_[idx] > 0.0f;
}

float ComboPointDisplay::GetNormalizedProgress() const {
    return static_cast<float>(points_) /
           static_cast<float>(kMaxComboPoints);
}

void ComboPointDisplay::OnTargetChanged(const ObjectGuid& newTarget) {
    if (newTarget.IsEmpty()) {

        ClearComboPoints();
        return;
    }
    if (!(target_ == newTarget)) {

        points_ = 0;
        std::memset(anim_progress_, 0, sizeof(anim_progress_));
        target_ = newTarget;
    }
}

}
