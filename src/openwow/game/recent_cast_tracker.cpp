
#include "openwow/game/recent_cast_tracker.h"

#include <algorithm>

namespace openwow::game {

RecentCastTracker& RecentCastTracker::GetPlayerTracker() {
  static RecentCastTracker instance;
  return instance;
}

RecentCastTracker& RecentCastTracker::GetPetTracker() {
  static RecentCastTracker instance;
  return instance;
}

void RecentCastTracker::RecordCast(std::uint32_t spell_id,
                                   std::uint32_t context_id,
                                   std::uint32_t tick_ms) {
  std::lock_guard lock(mutex_);

  ExpireEntriesUnlocked(tick_ms);

  entries_.push_back({spell_id, context_id, tick_ms});
}

bool RecentCastTracker::Contains(std::uint32_t spell_id,
                                 std::uint32_t context_id,
                                 std::uint32_t current_tick_ms) const {
  std::lock_guard lock(mutex_);

  return std::any_of(
      entries_.rbegin(), entries_.rend(),
      [&](const Entry& e) {

        return e.spell_id == spell_id && e.context_id == context_id &&
               (current_tick_ms - e.tick_ms) < kRecentCastWindowMs;
      });
}

void RecentCastTracker::ExpireEntries(std::uint32_t current_tick_ms) {
  std::lock_guard lock(mutex_);
  ExpireEntriesUnlocked(current_tick_ms);
}

void RecentCastTracker::ExpireEntriesUnlocked(std::uint32_t current_tick_ms) {
  while (!entries_.empty() &&
         (current_tick_ms - entries_.front().tick_ms) >= kRecentCastWindowMs) {
    entries_.pop_front();
  }
}

void RecentCastTracker::Clear() {
  std::lock_guard lock(mutex_);
  entries_.clear();
}

}
