#include "openwow/game/queue_position.h"

#include <utility>

namespace openwow::game {

void QueuePositionTracker::Reset() {
  Reset({});
}

void QueuePositionTracker::Reset(std::string realm_name) {
  std::lock_guard lock(mutex_);
  sampled_positions_.fill(0);
  sample_ticks_ms_.fill(0);
  active_ = false;
  free_character_migration_ = false;
  has_account_info_ = false;
  billing_time_ = 0;
  billing_flags_ = 0;
  billing_rested_ = 0;
  expansion_level_ = 0;
  realm_name_ = std::move(realm_name);
  estimated_wait_ms_ = 0;
}

void QueuePositionTracker::RecordAccountInfo(
    const std::uint32_t billing_time, const std::uint8_t billing_flags,
    const std::uint32_t billing_rested,
    const std::uint8_t expansion_level) {
  std::lock_guard lock(mutex_);
  has_account_info_ = true;
  billing_time_ = billing_time;
  billing_flags_ = billing_flags;
  billing_rested_ = billing_rested;
  expansion_level_ = expansion_level;
}

void QueuePositionTracker::SetRealmName(std::string realm_name) {
  std::lock_guard lock(mutex_);
  realm_name_ = std::move(realm_name);
}

void QueuePositionTracker::RecordQueuePosition(
    const std::uint32_t position,
    const bool free_character_migration,
    const std::uint32_t now_tick_ms) {
  std::lock_guard lock(mutex_);
  active_ = true;
  free_character_migration_ = free_character_migration;

  if (position != sampled_positions_[0] || sample_ticks_ms_[0] == 0) {
    for (std::size_t index = kSampleCount - 1; index > 0; --index) {
      sampled_positions_[index] = sampled_positions_[index - 1];
      sample_ticks_ms_[index] = sample_ticks_ms_[index - 1];
    }
    sampled_positions_[0] = position;
  }

  sample_ticks_ms_[0] = now_tick_ms;
  if (sample_ticks_ms_[kSampleCount - 1] == 0) {
    return;
  }

  const std::int32_t queue_delta =
      static_cast<std::int32_t>(sampled_positions_[0]) -
      static_cast<std::int32_t>(sampled_positions_[kSampleCount - 1]);
  if (queue_delta < 0) {
    const std::uint32_t elapsed_ms =
        now_tick_ms - sample_ticks_ms_[kSampleCount - 1];
    const std::uint32_t positions_advanced =
        static_cast<std::uint32_t>(-queue_delta);
    estimated_wait_ms_ = static_cast<std::int32_t>(
        position * (elapsed_ms / positions_advanced));
  }
}

QueuePositionSnapshot QueuePositionTracker::Snapshot(
    const std::uint32_t now_tick_ms) const {
  std::lock_guard lock(mutex_);

  QueuePositionSnapshot snapshot;
  snapshot.active = active_;
  snapshot.position = sampled_positions_[0];
  snapshot.free_character_migration = free_character_migration_;
  snapshot.has_account_info = has_account_info_;
  snapshot.billing_time = billing_time_;
  snapshot.billing_flags = billing_flags_;
  snapshot.billing_rested = billing_rested_;
  snapshot.expansion_level = expansion_level_;
  snapshot.realm_name = realm_name_;
  snapshot.sampled_positions = sampled_positions_;
  snapshot.sample_ticks_ms = sample_ticks_ms_;
  snapshot.has_estimated_wait = estimated_wait_ms_ > 0;
  snapshot.estimated_wait_ms = estimated_wait_ms_;
  if (snapshot.has_estimated_wait) {
    snapshot.remaining_wait_ms =
        static_cast<std::int32_t>(sample_ticks_ms_[0] + estimated_wait_ms_
                                  - now_tick_ms);
  }
  return snapshot;
}

}
