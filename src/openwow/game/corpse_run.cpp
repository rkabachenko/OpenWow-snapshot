
#include "openwow/game/corpse_run.h"

#include <cmath>

namespace openwow::game {

void CorpseRunDisplay::SetCorpsePosition(float x, float y, float z,
                                         uint32_t mapId) {
  corpsePos_ = {x, y, z, mapId};
  hasCorpse_ = true;
}

CorpseRunPosition CorpseRunDisplay::GetCorpsePosition() const {
  return corpsePos_;
}

bool CorpseRunDisplay::HasCorpse() const { return hasCorpse_; }

void CorpseRunDisplay::UpdatePlayerPosition(float x, float y, float z) {
  playerX_ = x;
  playerY_ = y;
  playerZ_ = z;
}

float CorpseRunDisplay::GetDistanceToCorpse() const {
  if (!hasCorpse_) return -1.0f;
  float dx = corpsePos_.x - playerX_;
  float dy = corpsePos_.y - playerY_;
  float dz = corpsePos_.z - playerZ_;
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

float CorpseRunDisplay::GetDirectionToCorpse() const {
  if (!hasCorpse_) return 0.0f;
  float dx = corpsePos_.x - playerX_;
  float dy = corpsePos_.y - playerY_;
  return std::atan2(dy, dx);
}

bool CorpseRunDisplay::IsInRangeToRevive() const {
  if (!hasCorpse_) return false;
  return GetDistanceToCorpse() <= reviveRange_;
}

float CorpseRunDisplay::GetReviveRange() const { return reviveRange_; }

void CorpseRunDisplay::SetReviveRange(float range) { reviveRange_ = range; }

bool CorpseRunDisplay::ShowCorpseArrow() const {

  return hasCorpse_ && !IsInRangeToRevive();
}

uint32_t CorpseRunDisplay::GetCorpseMapId() const { return corpsePos_.mapId; }

bool CorpseRunDisplay::IsCorpseOnSameMap(uint32_t playerMapId) const {
  return hasCorpse_ && corpsePos_.mapId == playerMapId;
}

float CorpseRunDisplay::GetRunTimeElapsed() const { return runTimeElapsed_; }

void CorpseRunDisplay::SetRunTimeElapsed(float seconds) {
  runTimeElapsed_ = seconds;
}

void CorpseRunDisplay::Update(float dt) {
  if (hasCorpse_) {
    runTimeElapsed_ += dt;
  }
}

void CorpseRunDisplay::Reset() {
  corpsePos_ = {};
  hasCorpse_ = false;
  playerX_ = playerY_ = playerZ_ = 0.0f;
  reviveRange_ = 40.0f;
  runTimeElapsed_ = 0.0f;
}

}
