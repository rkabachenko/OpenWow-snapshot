
#include "openwow/game/res_sickness.h"

#include <algorithm>
#include <cmath>

namespace openwow::game {

namespace {

constexpr float kMaxDuration = 600.0f;

constexpr float kSecondsPerLevel = 60.0f;

constexpr float kPenaltyMultiplier = 0.25f;

constexpr float kDurabilityLossFraction = 0.25f;

constexpr float kAutoApplyDeadThresholdSec = 360.0f;

float ClampDuration(float d) {
  return std::clamp(d, 0.0f, kMaxDuration);
}

}

void ResSicknessState::Apply(float duration) {
  totalDuration_  = ClampDuration(duration);
  remainingTime_  = totalDuration_;
  active_         = (totalDuration_ > 0.0f);
}

void ResSicknessState::Remove() {
  active_        = false;
  remainingTime_ = 0.0f;
  totalDuration_ = 0.0f;
}

bool ResSicknessState::IsActive() const { return active_; }

float ResSicknessState::GetRemainingTime() const { return remainingTime_; }

float ResSicknessState::GetTotalDuration() const { return totalDuration_; }

float ResSicknessState::GetProgress() const {
  if (!active_ || totalDuration_ <= 0.0f) return 0.0f;
  const float elapsed = totalDuration_ - remainingTime_;
  return std::clamp(elapsed / totalDuration_, 0.0f, 1.0f);
}

float ResSicknessState::GetStatPenalty() const {
  return active_ ? kPenaltyMultiplier : 1.0f;
}

float ResSicknessState::GetDamagePenalty() const {
  return active_ ? kPenaltyMultiplier : 1.0f;
}

float ResSicknessState::GetDurabilityLoss() const {
  return kDurabilityLossFraction;
}

float ResSicknessState::CalculateDuration(uint32_t playerLevel) {

  const float raw = static_cast<float>(playerLevel) * kSecondsPerLevel;
  return std::min(raw, kMaxDuration);
}

bool ResSicknessState::ShouldApply(float timeDead, bool usedSpiritHealer) {

  if (usedSpiritHealer) return true;

  return timeDead > kAutoApplyDeadThresholdSec;
}

void ResSicknessState::Update(float dt) {
  if (!active_) return;

  remainingTime_ -= dt;
  if (remainingTime_ <= 0.0f) {
    remainingTime_ = 0.0f;
    active_ = false;
  }
}

void ResSicknessState::Reset() {
  active_        = false;
  remainingTime_ = 0.0f;
  totalDuration_ = 0.0f;
}

}
