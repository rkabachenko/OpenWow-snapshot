#include "openwow/game/activities/dance/application/dance_cache_coordinator.h"

#include <utility>

namespace openwow::game {

void DanceCacheCoordinator::ClearCache() {
  cache_.clear();
}

void DanceCacheCoordinator::ClearPendingQueries() {
  pending_queries_.clear();
}

void DanceCacheCoordinator::Cache(DanceCacheRecord dance) {
  const DanceId dance_id = dance.id;
  if (dance.name.size() >= 0x80) {
    dance.name.resize(0x7F);
  }
  cache_[dance_id] = std::move(dance);
  Complete(dance_id, DanceQueryStatus::kFound);
}

void DanceCacheCoordinator::Invalidate(const DanceId dance_id) {
  cache_.erase(dance_id);
  Complete(dance_id, DanceQueryStatus::kMissing);
}

const DanceCacheRecord* DanceCacheCoordinator::Find(
    const DanceId dance_id) const {
  const auto dance = cache_.find(dance_id);
  return dance == cache_.end() ? nullptr : &dance->second;
}

void DanceCacheCoordinator::Queue(
    const DanceId dance_id, QueryResultCallback callback) {
  pending_queries_[dance_id].callbacks.push_back(std::move(callback));
}

void DanceCacheCoordinator::Complete(const DanceId dance_id,
                                     const DanceQueryStatus status) {
  const auto pending = pending_queries_.find(dance_id);
  if (pending == pending_queries_.end()) {
    return;
  }

  auto callbacks = std::move(pending->second.callbacks);
  pending_queries_.erase(pending);
  for (auto& callback : callbacks) {
    if (callback) {
      callback(dance_id, status);
    }
  }
}

}
