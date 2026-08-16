
#include "openwow/game/death_state.h"

#include <algorithm>

namespace openwow::game {

void PlayerDeathState::SetPhase(DeathPhase phase) { phase_ = phase; }

DeathPhase PlayerDeathState::GetPhase() const { return phase_; }

bool PlayerDeathState::IsAlive() const { return phase_ == DeathPhase::Alive; }

bool PlayerDeathState::IsDead() const { return phase_ == DeathPhase::Dead; }

bool PlayerDeathState::IsGhost() const { return phase_ == DeathPhase::Ghost; }

bool PlayerDeathState::IsResurrectPending() const {
  return phase_ == DeathPhase::ResurrectPending;
}

void PlayerDeathState::Die(float deathX, float deathY, float deathZ,
                           uint32_t mapId) {
  phase_ = DeathPhase::Dead;
  deathPosition_ = {deathX, deathY, deathZ, mapId};
  timeSinceDeath_ = 0.0f;
  ++deathCount_;
}

void PlayerDeathState::ReleaseSpirit() {
  if (phase_ != DeathPhase::Dead) return;
  phase_ = DeathPhase::Ghost;
}

void PlayerDeathState::Resurrect() { phase_ = DeathPhase::Alive; }

DeathPosition PlayerDeathState::GetDeathPosition() const {
  return deathPosition_;
}

float PlayerDeathState::GetTimeSinceDeath() const { return timeSinceDeath_; }

void PlayerDeathState::SetTimeSinceDeath(float seconds) {
  timeSinceDeath_ = seconds;
}

float PlayerDeathState::GetCorpseRecoveryDelay() const {

  constexpr float kBaseDelay = 30.0f;
  constexpr float kIncrementPerDeath = 30.0f;
  constexpr float kMaxDelay = 120.0f;
  float delay = kBaseDelay + kIncrementPerDeath * static_cast<float>(
                                                      deathCount_ > 0 ? deathCount_ - 1 : 0);
  return std::min(delay, kMaxDelay);
}

bool PlayerDeathState::CanReleaseSpirit() const {
  return phase_ == DeathPhase::Dead;
}

void PlayerDeathState::SetGhostSpeed(float multiplier) {
  ghostSpeed_ = multiplier;
}

float PlayerDeathState::GetGhostSpeed() const { return ghostSpeed_; }

void PlayerDeathState::SetResurrectPending(bool pending) {
  if (pending) {
    phase_ = DeathPhase::ResurrectPending;
  } else if (phase_ == DeathPhase::ResurrectPending) {

    phase_ = DeathPhase::Ghost;
  }
}

uint32_t PlayerDeathState::GetDeathCount() const { return deathCount_; }

void PlayerDeathState::IncrementDeathCount() { ++deathCount_; }

void PlayerDeathState::ResetDeathCount() { deathCount_ = 0; }

void PlayerDeathState::Update(float dt) {
  if (phase_ == DeathPhase::Dead || phase_ == DeathPhase::Ghost ||
      phase_ == DeathPhase::ResurrectPending) {
    timeSinceDeath_ += dt;
  }
}

void PlayerDeathState::Reset() {
  phase_ = DeathPhase::Alive;
  deathPosition_ = {};
  timeSinceDeath_ = 0.0f;
  ghostSpeed_ = 1.0f;
  deathCount_ = 0;
}

}
