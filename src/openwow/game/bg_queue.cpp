
#include "openwow/game/bg_queue.h"

namespace openwow::game {

const BGQueueInfo BGQueue::kEmptyInfo{};

BGQueue& BGQueue::Get() {
  static BGQueue instance;
  return instance;
}

void BGQueue::SetQueueStatus(std::uint32_t queue_slot,
                              const BGQueueInfo& info) {
  std::lock_guard lock(mutex_);
  if (queue_slot < kMaxQueues) {
    queues_[queue_slot] = info;
  }
}

void BGQueue::ClearQueue(std::uint32_t queue_slot) {
  std::lock_guard lock(mutex_);
  if (queue_slot < kMaxQueues) {
    queues_[queue_slot] = BGQueueInfo{};
  }
}

const BGQueueInfo& BGQueue::GetQueueInfo(std::uint32_t slot) const {
  std::lock_guard lock(mutex_);
  if (slot < kMaxQueues) {
    return queues_[slot];
  }
  return kEmptyInfo;
}

bool BGQueue::IsQueued() const {
  std::lock_guard lock(mutex_);
  for (const auto& q : queues_) {
    if (q.status != BGQueueInfo::kNone) return true;
  }
  return false;
}

std::uint32_t BGQueue::GetActiveQueueCount() const {
  std::lock_guard lock(mutex_);
  std::uint32_t count = 0;
  for (const auto& q : queues_) {
    if (q.status != BGQueueInfo::kNone) ++count;
  }
  return count;
}

bool BGQueue::HasReadyQueue() const {
  std::lock_guard lock(mutex_);
  for (const auto& q : queues_) {
    if (q.status == BGQueueInfo::kWaitJoin) return true;
  }
  return false;
}

std::uint32_t BGQueue::GetReadyQueueSlot() const {
  std::lock_guard lock(mutex_);
  for (std::uint32_t i = 0; i < kMaxQueues; ++i) {
    if (queues_[i].status == BGQueueInfo::kWaitJoin) return i;
  }
  return kMaxQueues;
}

void BGQueue::AcceptBG(std::uint32_t slot) {
  std::lock_guard lock(mutex_);
  if (slot < kMaxQueues && queues_[slot].status == BGQueueInfo::kWaitJoin) {
    queues_[slot].status = BGQueueInfo::kInProgress;
  }
}

void BGQueue::DeclineBG(std::uint32_t slot) {
  std::lock_guard lock(mutex_);
  if (slot < kMaxQueues) {
    queues_[slot] = BGQueueInfo{};
  }
}

void BGQueue::SetInBattleground(bool in_bg, BattlegroundType type) {
  std::lock_guard lock(mutex_);
  in_bg_ = in_bg;
  if (in_bg) {
    current_bg_type_ = type;
  }
}

bool BGQueue::IsInBattleground() const {
  std::lock_guard lock(mutex_);
  return in_bg_;
}

BattlegroundType BGQueue::GetCurrentBGType() const {
  std::lock_guard lock(mutex_);
  return current_bg_type_;
}

void BGQueue::UpdateScores(std::int32_t ally, std::int32_t horde) {
  std::lock_guard lock(mutex_);

  for (auto& q : queues_) {
    if (q.status == BGQueueInfo::kInProgress) {
      q.ally_score = ally;
      q.horde_score = horde;
    }
  }
}

std::int32_t BGQueue::GetAllyScore() const {
  std::lock_guard lock(mutex_);
  for (const auto& q : queues_) {
    if (q.status == BGQueueInfo::kInProgress) return q.ally_score;
  }
  return 0;
}

std::int32_t BGQueue::GetHordeScore() const {
  std::lock_guard lock(mutex_);
  for (const auto& q : queues_) {
    if (q.status == BGQueueInfo::kInProgress) return q.horde_score;
  }
  return 0;
}

void BGQueue::SetBGTimer(float remaining) {
  std::lock_guard lock(mutex_);
  bg_timer_ = remaining;
}

float BGQueue::GetBGTimer() const {
  std::lock_guard lock(mutex_);
  return bg_timer_;
}

void BGQueue::LeaveBattleground() {
  std::lock_guard lock(mutex_);
  in_bg_ = false;
  bg_timer_ = 0;

  for (auto& q : queues_) {
    if (q.status == BGQueueInfo::kInProgress) {
      q = BGQueueInfo{};
    }
  }
}

void BGQueue::SetWintergraspTimer(std::uint32_t seconds) {
  std::lock_guard lock(mutex_);
  wintergrasp_timer_ = seconds;
}

std::uint32_t BGQueue::GetWintergraspTimer() const {
  std::lock_guard lock(mutex_);
  return wintergrasp_timer_;
}

bool BGQueue::IsWintergraspInProgress() const {
  std::lock_guard lock(mutex_);
  return wintergrasp_timer_ == 0;
}

void BGQueue::Reset() {
  std::lock_guard lock(mutex_);
  for (auto& q : queues_) {
    q = BGQueueInfo{};
  }
  in_bg_ = false;
  current_bg_type_ = BattlegroundType::kWarsongGulch;
  bg_timer_ = 0;
  wintergrasp_timer_ = 0;
}

}
