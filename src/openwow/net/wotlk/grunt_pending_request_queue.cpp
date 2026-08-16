
#include "openwow/net/wotlk/grunt_pending_request_queue.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <mutex>

namespace openwow::net::wotlk {

namespace {

std::uint32_t GetTickCount32() {
  using namespace std::chrono;
  return static_cast<std::uint32_t>(
      duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
          .count());
}

}

GruntPendingRequestQueue::~GruntPendingRequestQueue() {
  Destroy();
}

GruntPendingRequestNode* GruntPendingRequestQueue::DetachRequestById(
    std::uint32_t request_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = std::find_if(
      pending_requests_.begin(), pending_requests_.end(),
      [request_id](const GruntPendingRequestNode* node) {
        return node->request_id == request_id;
      });

  if (it == pending_requests_.end()) {
    return nullptr;
  }

  GruntPendingRequestNode* found = *it;
  pending_requests_.erase(it);

  found->link.previous_link = 0;
  found->link.next_node = 0;
  return found;
}

void GruntPendingRequestQueue::InsertRequestSorted(
    GruntPendingRequestNode* node) {
  if (!node) return;

  std::lock_guard<std::mutex> lock(mutex_);

  node->deadline_tick = node->timeout_interval_ms + GetTickCount32();

  auto pos = std::lower_bound(
      pending_requests_.begin(), pending_requests_.end(), node,
      [](const GruntPendingRequestNode* existing,
         const GruntPendingRequestNode* incoming) {

        return static_cast<std::int32_t>(existing->deadline_tick -
                                         incoming->deadline_tick) < 0;
      });

  pending_requests_.insert(pos, node);
}

std::uint32_t GruntPendingRequestQueue::GetNextDeadlineMs() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (pending_requests_.empty()) {
    return 100;
  }

  GruntPendingRequestNode* head = pending_requests_.front();
  std::uint32_t now = GetTickCount32();
  auto remaining =
      static_cast<std::int32_t>(head->deadline_tick - now);

  if (remaining <= 0) {

    pending_requests_.erase(pending_requests_.begin());
    head->link.previous_link = 0;
    head->link.next_node = 0;

    if (timeout_callback_) {

      mutex_.unlock();
      timeout_callback_(head);
      mutex_.lock();
    }
    return 100;
  }

  return static_cast<std::uint32_t>(remaining);
}

void GruntPendingRequestQueue::CleanupExpiredRequests(bool force) {
  std::vector<GruntPendingRequestNode*> expired;

  {
    std::lock_guard<std::mutex> lock(mutex_);

    if (force) {
      expired = std::move(pending_requests_);
      pending_requests_.clear();
    } else {
      std::uint32_t now = GetTickCount32();
      auto it = pending_requests_.begin();
      while (it != pending_requests_.end()) {
        GruntPendingRequestNode* node = *it;
        if (node->deadline_tick != 0 &&
            static_cast<std::int32_t>(now - node->deadline_tick) >= 0) {
          expired.push_back(node);
          it = pending_requests_.erase(it);
        } else {

          break;
        }
      }
    }
  }

  for (GruntPendingRequestNode* node : expired) {
    node->link.previous_link = 0;
    node->link.next_node = 0;
    if (timeout_callback_) {
      timeout_callback_(node);
    }
  }
}

void GruntPendingRequestQueue::Destroy() {
  CleanupExpiredRequests(true);
}

bool GruntPendingRequestQueue::IsEmpty() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return pending_requests_.empty();
}

std::size_t GruntPendingRequestQueue::Size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return pending_requests_.size();
}

}
