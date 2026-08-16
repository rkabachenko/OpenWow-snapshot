#include "openwow/game/rest_state.h"

#include <algorithm>

namespace openwow::game {

void RestStateManager::SetInRestArea(bool in_rest_area) {
  in_rest_area_ = in_rest_area;
}

bool RestStateManager::IsInRestArea() const { return in_rest_area_; }

RestState RestStateManager::GetState() const {
  if (rested_xp_ > 0) return RestState::Rested;
  return RestState::Normal;
}

void RestStateManager::SetRestedXP(std::uint32_t xp) { rested_xp_ = xp; }

std::uint32_t RestStateManager::GetRestedXP() const { return rested_xp_; }

float RestStateManager::GetRestedPercent(std::uint32_t next_level_xp) const {
  if (next_level_xp == 0) return 0.0f;
  return (static_cast<float>(rested_xp_) / static_cast<float>(next_level_xp)) * 100.0f;
}

std::uint32_t RestStateManager::GetMaxRestedXP(std::uint32_t next_level_xp) const {

  return static_cast<std::uint32_t>(next_level_xp * 1.5f);
}

bool RestStateManager::IsFullyRested(std::uint32_t next_level_xp) const {
  return rested_xp_ >= GetMaxRestedXP(next_level_xp);
}

void RestStateManager::AccumulateRest(float dt, std::uint32_t next_level_xp) {
  cached_next_level_xp_ = next_level_xp;

  if (!in_rest_area_ || next_level_xp == 0) return;

  float rate_per_second = (0.05f * static_cast<float>(next_level_xp)) / 28800.0f;
  float gain = rate_per_second * dt;

  std::uint32_t max_rest = GetMaxRestedXP(next_level_xp);
  rested_xp_ = std::min(rested_xp_ + static_cast<std::uint32_t>(gain), max_rest);
}

std::uint32_t RestStateManager::ConsumeRestXP(std::uint32_t xp_gain) {

  std::uint32_t bonus = std::min(xp_gain, rested_xp_);
  rested_xp_ -= bonus;
  return bonus;
}

float RestStateManager::GetRestXPRate() const {
  if (cached_next_level_xp_ == 0) return 0.0f;

  return (0.05f * static_cast<float>(cached_next_level_xp_)) / 8.0f;
}

float RestStateManager::GetTimeToFullRest(std::uint32_t next_level_xp) const {
  if (next_level_xp == 0) return 0.0f;
  std::uint32_t max_rest = GetMaxRestedXP(next_level_xp);
  if (rested_xp_ >= max_rest) return 0.0f;

  float remaining = static_cast<float>(max_rest - rested_xp_);

  float rate_per_second = (0.05f * static_cast<float>(next_level_xp)) / 28800.0f;
  if (rate_per_second <= 0.0f) return 0.0f;

  return remaining / rate_per_second;
}

bool RestStateManager::GetRestIcon() const {
  return in_rest_area_ || rested_xp_ > 0;
}

void RestStateManager::SetLogoutLocation(bool is_inn) {
  logged_out_in_inn_ = is_inn;
}

void RestStateManager::Reset() {
  in_rest_area_ = false;
  logged_out_in_inn_ = false;
  rested_xp_ = 0;
  cached_next_level_xp_ = 0;
}

}
