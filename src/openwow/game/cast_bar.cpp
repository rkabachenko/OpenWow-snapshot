
#include "openwow/game/cast_bar.h"

#include <algorithm>

namespace openwow::game {

void CastBarData::StartCast(std::uint32_t spellId, const std::string& spellName,
                            float castTime, bool isChanneled) {
  spell_id_   = spellId;
  spell_name_ = spellName;
  total_time_ = castTime;
  elapsed_    = 0.0f;
  channeled_  = isChanneled;
  state_      = isChanneled ? GameCastBarState::Channeling
                            : GameCastBarState::Casting;
  pushback_count_ = 0;
  delay_amount_   = 0.0f;
  interruptible_  = true;
}

void CastBarData::StopCast(GameCastBarState reason) {
  state_ = reason;
}

void CastBarData::Update(float dt) {
  if (state_ != GameCastBarState::Casting &&
      state_ != GameCastBarState::Channeling)
    return;

  elapsed_ += dt;
  if (elapsed_ >= total_time_) {
    elapsed_ = total_time_;
    state_ = GameCastBarState::Succeeded;
  }
}

GameCastBarState CastBarData::GetState() const { return state_; }

bool CastBarData::IsCasting() const {
  return state_ == GameCastBarState::Casting ||
         state_ == GameCastBarState::Channeling;
}

std::uint32_t CastBarData::GetSpellId() const { return spell_id_; }

const std::string& CastBarData::GetSpellName() const { return spell_name_; }

float CastBarData::GetProgress() const {
  if (total_time_ <= 0.0f) return 0.0f;
  float ratio = elapsed_ / total_time_;
  ratio = std::clamp(ratio, 0.0f, 1.0f);
  if (channeled_) return 1.0f - ratio;
  return ratio;
}

float CastBarData::GetElapsedTime() const { return elapsed_; }
float CastBarData::GetTotalTime() const   { return total_time_; }

float CastBarData::GetRemainingTime() const {
  return std::max(0.0f, total_time_ - elapsed_);
}

bool CastBarData::IsChanneled() const { return channeled_; }

float CastBarData::GetProgressDirection() const {
  return channeled_ ? -1.0f : 1.0f;
}

void CastBarData::SetDelayAmount(float seconds) {
  delay_amount_ = seconds;
}

std::uint32_t CastBarData::GetPushbackCount() const { return pushback_count_; }
std::uint32_t CastBarData::GetMaxPushbacks() const  { return kMaxPushbacks; }

void CastBarData::ApplyPushback(float amount) {
  if (pushback_count_ >= kMaxPushbacks) return;
  if (state_ != GameCastBarState::Casting &&
      state_ != GameCastBarState::Channeling)
    return;

  ++pushback_count_;

  if (channeled_) {

    float remaining = total_time_ - elapsed_;
    remaining = std::max(0.0f, remaining - amount);
    total_time_ = elapsed_ + remaining;
  } else {

    total_time_ += amount;
  }
}

void CastBarData::SetInterruptible(bool v) { interruptible_ = v; }
bool CastBarData::IsInterruptible() const   { return interruptible_; }

void CastBarData::Reset() {
  state_       = GameCastBarState::Idle;
  spell_id_    = 0;
  spell_name_.clear();
  total_time_  = 0.0f;
  elapsed_     = 0.0f;
  channeled_   = false;
  interruptible_ = true;
  pushback_count_ = 0;
  delay_amount_   = 0.0f;
  target_cast_.reset();
}

std::optional<TargetCastInfo> CastBarData::GetTargetCast() const {
  return target_cast_;
}

void CastBarData::SetTargetCast(std::uint32_t spellId, const std::string& name,
                                float total, bool isChannel) {
  TargetCastInfo tc;
  tc.spellId   = spellId;
  tc.spellName = name;
  tc.total     = total;
  tc.progress  = 0.0f;
  tc.isChannel = isChannel;
  target_cast_ = tc;
}

void CastBarData::UpdateTargetCast(float dt) {
  if (!target_cast_) return;
  target_cast_->progress += dt;
  if (target_cast_->progress >= target_cast_->total) {
    target_cast_->progress = target_cast_->total;
  }
}

void CastBarData::ClearTargetCast() {
  target_cast_.reset();
}

}
