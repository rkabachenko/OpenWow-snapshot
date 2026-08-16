
#include "openwow/game/channel_display.h"

#include <algorithm>

namespace openwow::game {

void ChannelDisplay::Start(std::uint32_t spellId, const std::string& name,
                           float duration, std::uint32_t totalTicks) {
  active_         = true;
  spell_id_       = spellId;
  name_           = name;
  total_duration_ = duration;
  remaining_      = duration;
  breakable_      = true;
  targets_.clear();

  tick_info_.totalTicks = totalTicks;
  tick_info_.tickCount  = 0;
  tick_info_.tickTime   = (totalTicks > 0) ? (duration / static_cast<float>(totalTicks)) : 0.0f;
  tick_info_.nextTickAt = tick_info_.tickTime;
}

void ChannelDisplay::Cancel() {
  active_    = false;
  remaining_ = 0.0f;
}

void ChannelDisplay::Reset() {
  active_         = false;
  spell_id_       = 0;
  name_.clear();
  total_duration_ = 0.0f;
  remaining_      = 0.0f;
  breakable_      = true;
  tick_info_      = ChannelTickInfo{};
  targets_.clear();
}

void ChannelDisplay::AddTarget(const ObjectGuid& guid) {

  auto it = std::find_if(targets_.begin(), targets_.end(),
                         [&](const ObjectGuid& g) {
                           return g.GetRawValue() == guid.GetRawValue();
                         });
  if (it == targets_.end()) {
    targets_.push_back(guid);
  }
}

void ChannelDisplay::RemoveTarget(const ObjectGuid& guid) {
  targets_.erase(
      std::remove_if(targets_.begin(), targets_.end(),
                     [&](const ObjectGuid& g) {
                       return g.GetRawValue() == guid.GetRawValue();
                     }),
      targets_.end());
}

std::vector<ObjectGuid> ChannelDisplay::GetTargets() const {
  return targets_;
}

std::size_t ChannelDisplay::GetTargetCount() const {
  return targets_.size();
}

void ChannelDisplay::SetBreakable(bool breakable) {
  breakable_ = breakable;
}

bool ChannelDisplay::IsBreakable() const {
  return breakable_;
}

ChannelTickInfo ChannelDisplay::GetTickInfo() const {
  return tick_info_;
}

void ChannelDisplay::ProcessTick() {
  if (!active_) return;
  tick_info_.tickCount++;
  tick_info_.nextTickAt += tick_info_.tickTime;
}

float ChannelDisplay::GetProgress() const {
  if (!active_ || total_duration_ <= 0.0f) return 0.0f;
  return std::clamp(remaining_ / total_duration_, 0.0f, 1.0f);
}

std::uint32_t ChannelDisplay::GetSpellId() const {
  return spell_id_;
}

bool ChannelDisplay::IsChanneling() const {
  return active_;
}

float ChannelDisplay::GetRemaining() const {
  return remaining_;
}

void ChannelDisplay::Update(float dt) {
  if (!active_) return;

  remaining_ -= dt;

  if (tick_info_.totalTicks > 0 && tick_info_.tickTime > 0.0f) {
    float elapsed = total_duration_ - remaining_;
    while (tick_info_.tickCount < tick_info_.totalTicks &&
           elapsed >= tick_info_.nextTickAt) {
      tick_info_.tickCount++;
      tick_info_.nextTickAt += tick_info_.tickTime;
    }
  }

  if (remaining_ <= 0.0f) {
    remaining_ = 0.0f;
    active_    = false;
  }
}

}
