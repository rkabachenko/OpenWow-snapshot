
#include "openwow/game/combat_tracker.h"

#include <algorithm>
#include <numeric>

namespace openwow::game {

CombatTracker& CombatTracker::Get() {
  static CombatTracker instance;
  return instance;
}

bool CombatTracker::IsInCombat() const {
  std::lock_guard lock(mutex_);
  return in_combat_;
}

void CombatTracker::SetInCombat(bool combat) {
  std::lock_guard lock(mutex_);
  if (combat && !in_combat_) {

    combat_start_ = events_.empty() ? 0 : events_.back().timestamp;
    stats_.combat_start_time = combat_start_;
  }
  in_combat_ = combat;
}

std::uint32_t CombatTracker::GetCombatDuration() const {
  std::lock_guard lock(mutex_);
  return stats_.combat_duration;
}

void CombatTracker::AddEvent(const TrackedCombatEvent& event) {
  std::lock_guard lock(mutex_);

  if (events_.size() >= kMaxEvents) {
    events_.erase(events_.begin());
  }
  events_.push_back(event);

  switch (event.type) {
    case TrackedCombatEvent::DamageDealt:
      stats_.damage_done += event.amount;
      break;
    case TrackedCombatEvent::DamageTaken:
      stats_.damage_taken += event.amount;
      break;
    case TrackedCombatEvent::HealingDone:
      stats_.healing_done += event.amount;
      break;
    case TrackedCombatEvent::HealingTaken:
      stats_.healing_taken += event.amount;
      break;
    case TrackedCombatEvent::KillingBlow:
      stats_.killing_blows++;
      break;
    case TrackedCombatEvent::Died:
      stats_.deaths++;
      break;
    case TrackedCombatEvent::EnterCombat:
      if (!in_combat_) {
        in_combat_ = true;
        combat_start_ = event.timestamp;
        stats_.combat_start_time = combat_start_;
      }
      break;
    case TrackedCombatEvent::LeaveCombat:
      in_combat_ = false;
      break;
    default:
      break;
  }

  if (in_combat_ && combat_start_ > 0 && event.timestamp > combat_start_) {
    stats_.combat_duration = (event.timestamp - combat_start_) / 1000;
    if (stats_.combat_duration > 0) {
      stats_.dps = static_cast<float>(stats_.damage_done) /
                   static_cast<float>(stats_.combat_duration);
      stats_.hps = static_cast<float>(stats_.healing_done) /
                   static_cast<float>(stats_.combat_duration);
    }
  }
}

std::vector<TrackedCombatEvent> CombatTracker::GetRecentEvents() const {
  std::lock_guard lock(mutex_);

  if (events_.size() <= 100) return events_;
  return {events_.end() - 100, events_.end()};
}

void CombatTracker::SetMainHandSwing(float speed) {
  std::lock_guard lock(mutex_);
  main_hand_.speed = speed;
  main_hand_.remaining = speed;
  main_hand_.is_active = speed > 0.0f;
}

void CombatTracker::SetOffHandSwing(float speed) {
  std::lock_guard lock(mutex_);
  off_hand_.speed = speed;
  off_hand_.remaining = speed;
  off_hand_.is_active = speed > 0.0f;
}

void CombatTracker::SetRangedSwing(float speed) {
  std::lock_guard lock(mutex_);
  ranged_.speed = speed;
  ranged_.remaining = speed;
  ranged_.is_active = speed > 0.0f;
}

void CombatTracker::UpdateSwingTimers(float delta_time) {
  std::lock_guard lock(mutex_);
  auto tick = [&](SwingTimer& t) {
    if (!t.is_active || t.speed <= 0.0f) return;
    t.remaining -= delta_time;
    if (t.remaining <= 0.0f) {
      t.remaining = t.speed;
    }
  };
  tick(main_hand_);
  tick(off_hand_);
  tick(ranged_);
}

SwingTimer CombatTracker::GetMainHandSwing() const {
  std::lock_guard lock(mutex_);
  return main_hand_;
}

SwingTimer CombatTracker::GetOffHandSwing() const {
  std::lock_guard lock(mutex_);
  return off_hand_;
}

SwingTimer CombatTracker::GetRangedSwing() const {
  std::lock_guard lock(mutex_);
  return ranged_;
}

void CombatTracker::ResetSwingTimer(bool mainhand, bool offhand, bool ranged) {
  std::lock_guard lock(mutex_);
  if (mainhand) main_hand_.remaining = main_hand_.speed;
  if (offhand) off_hand_.remaining = off_hand_.speed;
  if (ranged) ranged_.remaining = ranged_.speed;
}

CombatStats CombatTracker::GetStats() const {
  std::lock_guard lock(mutex_);
  return stats_;
}

float CombatTracker::GetDPS() const {
  std::lock_guard lock(mutex_);
  return stats_.dps;
}

float CombatTracker::GetHPS() const {
  std::lock_guard lock(mutex_);
  return stats_.hps;
}

void CombatTracker::SetAbsorbAmount(const ObjectGuid& unit,
                                    std::uint32_t amount) {
  std::lock_guard lock(mutex_);
  absorbs_[unit.GetRawValue()] = amount;
}

std::uint32_t CombatTracker::GetAbsorbAmount(const ObjectGuid& unit) const {
  std::lock_guard lock(mutex_);
  auto it = absorbs_.find(unit.GetRawValue());
  return it != absorbs_.end() ? it->second : 0;
}

void CombatTracker::ResetStats() {
  std::lock_guard lock(mutex_);
  stats_ = {};
  events_.clear();
}

void CombatTracker::Reset() {
  std::lock_guard lock(mutex_);
  in_combat_ = false;
  combat_start_ = 0;
  events_.clear();
  main_hand_ = {};
  off_hand_ = {};
  ranged_ = {};
  stats_ = {};
  absorbs_.clear();
}

}
