#pragma once

#include <cstdint>

#include "openwow/game/object_guid.h"

namespace openwow::game {

struct ComboPointColor {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

class ComboPointDisplay {
 public:
    static constexpr std::uint8_t kMaxComboPoints = 5;

    void SetComboPoints(std::uint8_t points);
    [[nodiscard]] std::uint8_t GetComboPoints() const;

    void SetTarget(const ObjectGuid& target);
    [[nodiscard]] ObjectGuid GetTarget() const;
    [[nodiscard]] bool HasTarget() const;

    [[nodiscard]] bool IsMaxed() const;
    [[nodiscard]] std::uint8_t GetMaxComboPoints() const;

    void SetEnabled(bool enabled);
    [[nodiscard]] bool IsEnabled() const;

    [[nodiscard]] ComboPointColor GetPointColor(std::uint8_t point) const;
    [[nodiscard]] float GetAnimationProgress(std::uint8_t point) const;

    void ClearComboPoints();
    void OnPointGain();

    void UpdateAnimation(float dt);

    [[nodiscard]] float GetFlashIntensity() const;

    [[nodiscard]] bool ShouldFlash() const;

    [[nodiscard]] float GetNormalizedProgress() const;

    void OnTargetChanged(const ObjectGuid& newTarget);

    void Reset();

 private:
    ObjectGuid target_;
    std::uint8_t points_ = 0;
    bool enabled_ = false;

    float anim_progress_[kMaxComboPoints] = {};
};

}
