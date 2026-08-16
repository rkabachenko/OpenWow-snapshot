#pragma once

#include "openwow/game/activities/dance/model/dance_studio_messages.h"

#include <functional>
#include <unordered_map>
#include <vector>

namespace openwow::game {

class DanceCacheCoordinator final {
 public:
  using QueryResultCallback =
      std::function<void(DanceId, DanceQueryStatus)>;

  void ClearCache();
  void ClearPendingQueries();

  void Cache(DanceCacheRecord dance);
  void Invalidate(DanceId dance_id);
  [[nodiscard]] const DanceCacheRecord* Find(DanceId dance_id) const;

  void Queue(DanceId dance_id, QueryResultCallback callback);
  void Complete(DanceId dance_id, DanceQueryStatus status);

 private:
  struct PendingQuery final {
    std::vector<QueryResultCallback> callbacks;
  };

  std::unordered_map<DanceId, DanceCacheRecord, DanceIdHash> cache_;
  std::unordered_map<DanceId, PendingQuery, DanceIdHash> pending_queries_;
};

}
