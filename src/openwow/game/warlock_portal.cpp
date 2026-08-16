
#include "openwow/game/warlock_portal.h"

#include <algorithm>

namespace openwow::game {

void RitualOfSummoning::StartRitual(ObjectGuid caster, ObjectGuid target,
                                     const std::string& targetName) {
  state_       = RitualState::WaitingForClickers;
  caster_      = caster;
  target_      = target;
  target_name_ = targetName;
  clickers_.clear();
  elapsed_     = 0.0f;
}

RitualState RitualOfSummoning::GetState() const {
  return state_;
}

bool RitualOfSummoning::AddClicker(ObjectGuid clicker,
                                   const std::string& name) {
  if (state_ != RitualState::WaitingForClickers) return false;

  if (clicker.GetRawValue() == caster_.GetRawValue()) return false;

  if (clicker.GetRawValue() == target_.GetRawValue()) return false;

  for (const auto& [guid, _] : clickers_) {
    if (guid.GetRawValue() == clicker.GetRawValue()) return false;
  }

  clickers_.emplace_back(clicker, name);

  if (clickers_.size() >= kRequiredClickers) {
    state_ = RitualState::Complete;
  }
  return true;
}

const std::vector<std::pair<ObjectGuid, std::string>>&
RitualOfSummoning::GetClickers() const {
  return clickers_;
}

std::size_t RitualOfSummoning::GetClickerCount() const {
  return clickers_.size();
}

std::size_t RitualOfSummoning::GetRequiredClickers() const {
  return kRequiredClickers;
}

bool RitualOfSummoning::IsComplete() const {
  return state_ == RitualState::Complete;
}

const std::string& RitualOfSummoning::GetTargetName() const {
  return target_name_;
}

ObjectGuid RitualOfSummoning::GetCasterGuid() const {
  return caster_;
}

void RitualOfSummoning::Update(float dt) {
  if (state_ != RitualState::WaitingForClickers) return;

  elapsed_ += dt;
  if (elapsed_ >= kMaxDuration) {
    state_ = RitualState::Failed;

  }
}

float RitualOfSummoning::GetTimeRemaining() const {
  if (state_ != RitualState::WaitingForClickers) return 0.0f;
  const float remaining = kMaxDuration - elapsed_;
  return (remaining > 0.0f) ? remaining : 0.0f;
}

void RitualOfSummoning::Reset() {
  state_   = RitualState::Inactive;
  caster_  = ObjectGuid{};
  target_  = ObjectGuid{};
  target_name_.clear();
  clickers_.clear();
  elapsed_ = 0.0f;
}

}
