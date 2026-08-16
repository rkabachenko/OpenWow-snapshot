
#include "openwow/game/death_display.h"

namespace openwow::game {

void DeathDisplay::Die() {
  if (state_ == DeathDisplayState::Alive) {
    state_ = DeathDisplayState::Dead;
    deathTimer_ = 0.0f;
    resOffer_.reset();
  }
}

void DeathDisplay::ReleaseSpirit() {
  if (state_ == DeathDisplayState::Dead ||
      state_ == DeathDisplayState::ReleasePending) {
    state_ = DeathDisplayState::Ghost;
  }
}

DeathDisplayState DeathDisplay::GetState() const {
  return state_;
}

void DeathDisplay::SetResurrectOffer(const ResurrectOfferInfo& offer) {
  resOffer_ = offer;
  if (state_ == DeathDisplayState::Dead || state_ == DeathDisplayState::Ghost) {
    state_ = DeathDisplayState::ResurrectPending;
  }
}

void DeathDisplay::AcceptResurrect() {
  if (resOffer_) {
    resOffer_.reset();
    state_ = DeathDisplayState::Alive;
    deathTimer_ = 0.0f;
  }
}

void DeathDisplay::DeclineResurrect() {
  if (resOffer_) {

    if (state_ == DeathDisplayState::ResurrectPending) {
      state_ = DeathDisplayState::Ghost;
    }
    resOffer_.reset();
  }
}

bool DeathDisplay::HasResurrectOffer() const {
  return resOffer_.has_value();
}

std::optional<ResurrectOfferInfo> DeathDisplay::GetResurrectOffer() const {
  return resOffer_;
}

void DeathDisplay::SetNearestGraveyard(const GraveyardDisplayInfo& info) {
  nearestGraveyard_ = info;
}

std::optional<GraveyardDisplayInfo> DeathDisplay::GetNearestGraveyard() const {
  return nearestGraveyard_;
}

float DeathDisplay::GetTimeSinceDeath() const {
  return deathTimer_;
}

void DeathDisplay::Update(float dt) {
  if (state_ != DeathDisplayState::Alive) {
    deathTimer_ += dt;
  }
}

bool DeathDisplay::CanAutoRelease() const {
  return inBattleground_ && state_ == DeathDisplayState::Dead &&
         deathTimer_ >= kBGAutoReleaseTimeSec;
}

void DeathDisplay::SetInBattleground(bool inBG) {
  inBattleground_ = inBG;
}

std::string DeathDisplay::GetResSicknessWarning() {
  return "Accepting will give you Resurrection Sickness for 10 minutes";
}

void DeathDisplay::Revive() {
  state_ = DeathDisplayState::Alive;
  deathTimer_ = 0.0f;
  resOffer_.reset();
}

std::uint32_t DeathDisplay::GetRepairCostOnRevive() const {
  return repairCost_;
}

void DeathDisplay::SetRepairCost(std::uint32_t copper) {
  repairCost_ = copper;
}

bool DeathDisplay::IsGhost() const {
  return state_ == DeathDisplayState::Ghost;
}

}
