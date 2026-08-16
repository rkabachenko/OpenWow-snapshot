#pragma once

#include "openwow/core/storm_intrusive_list.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <vector>

namespace openwow::net::wotlk {

struct GruntPendingRequestNode {
  core::StormIntrusiveLinkWords<std::uintptr_t> link{};
  std::uint32_t timeout_interval_ms = 0;
  std::uint32_t request_id = 0;
  std::uint32_t deadline_tick = 0;
  void* context_data = nullptr;
};

class GruntPendingRequestQueue {
 public:
  GruntPendingRequestQueue() = default;
  ~GruntPendingRequestQueue();

  GruntPendingRequestQueue(const GruntPendingRequestQueue&) = delete;
  GruntPendingRequestQueue& operator=(const GruntPendingRequestQueue&) = delete;

  [[nodiscard]] GruntPendingRequestNode* DetachRequestById(
      std::uint32_t request_id);

  void InsertRequestSorted(GruntPendingRequestNode* node);

  [[nodiscard]] std::uint32_t GetNextDeadlineMs();

  void CleanupExpiredRequests(bool force = false);

  void Destroy();

  [[nodiscard]] bool IsEmpty() const;
  [[nodiscard]] std::size_t Size() const;

  void SetDefaultTimeoutMs(std::uint32_t timeout_ms) {
    default_timeout_ms_ = timeout_ms;
  }
  [[nodiscard]] std::uint32_t default_timeout_ms() const {
    return default_timeout_ms_;
  }

  using TimeoutCallback = std::function<void(GruntPendingRequestNode*)>;
  void SetTimeoutCallback(TimeoutCallback cb) {
    timeout_callback_ = std::move(cb);
  }

 private:
  mutable std::mutex mutex_;
  std::vector<GruntPendingRequestNode*> pending_requests_;
  std::uint32_t default_timeout_ms_ = 18000;

  TimeoutCallback timeout_callback_;
};

}
