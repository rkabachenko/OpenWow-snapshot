
#pragma once

#include <cstdint>

namespace openwow::game {

class ResSicknessState {
 public:
  ResSicknessState() = default;

  void Apply(float duration);

  void Remove();

  [[nodiscard]] bool IsActive() const;
  [[nodiscard]] float GetRemainingTime() const;
  [[nodiscard]] float GetTotalDuration() const;

  [[nodiscard]] float GetProgress() const;

  [[nodiscard]] float GetStatPenalty() const;

  [[nodiscard]] float GetDamagePenalty() const;

  [[nodiscard]] float GetDurabilityLoss() const;

  [[nodiscard]] static float CalculateDuration(uint32_t playerLevel);

  [[nodiscard]] static bool ShouldApply(float timeDead, bool usedSpiritHealer);

  void Update(float dt);
  void Reset();

 private:
  bool active_ = false;
  float remainingTime_ = 0.0f;
  float totalDuration_ = 0.0f;
};

}
