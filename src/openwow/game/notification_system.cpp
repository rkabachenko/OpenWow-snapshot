
#include "openwow/game/notification_system.h"

#include <algorithm>
#include <chrono>

namespace openwow::game {

NotificationSystem& NotificationSystem::Get() {
  static NotificationSystem instance;
  return instance;
}

static std::uint32_t GetCurrentTimestamp() {
  auto now = std::chrono::system_clock::now();
  return static_cast<std::uint32_t>(
      std::chrono::duration_cast<std::chrono::seconds>(
          now.time_since_epoch())
          .count());
}

void NotificationSystem::Notify(const Notification& notification) {
  std::lock_guard<std::mutex> lock(mutex_);

  Notification n = notification;
  if (n.timestamp == 0) {
    n.timestamp = GetCurrentTimestamp();
  }

  recent_.push_back(n);
  if (recent_.size() > kMaxRecent) {
    recent_.erase(recent_.begin());
  }

  ++pending_counts_[static_cast<int>(n.type)];

  std::vector<NotificationHandler> matching_handlers;
  for (const auto& entry : handlers_) {
    if (entry.type == n.type) {
      matching_handlers.push_back(entry.handler);
    }
  }

  mutex_.unlock();
  for (const auto& handler : matching_handlers) {
    handler(n);
  }
  mutex_.lock();
}

void NotificationSystem::Notify(NotificationType type,
                                const std::string& text) {
  Notification n;
  n.type = type;
  n.text = text;
  Notify(n);
}

std::uint32_t NotificationSystem::RegisterHandler(
    NotificationType type, NotificationHandler handler) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto id = next_handler_id_++;
  handlers_.push_back({id, type, std::move(handler)});
  return id;
}

void NotificationSystem::UnregisterHandler(std::uint32_t id) {
  std::lock_guard<std::mutex> lock(mutex_);
  handlers_.erase(
      std::remove_if(handlers_.begin(), handlers_.end(),
                     [id](const HandlerEntry& e) { return e.id == id; }),
      handlers_.end());
}

const std::vector<Notification>& NotificationSystem::GetRecent() const {
  return recent_;
}

void NotificationSystem::ClearRecent() {
  std::lock_guard<std::mutex> lock(mutex_);
  recent_.clear();
}

std::uint32_t NotificationSystem::GetPendingCount(
    NotificationType type) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = pending_counts_.find(static_cast<int>(type));
  if (it != pending_counts_.end()) return it->second;
  return 0;
}

void NotificationSystem::ClearPending(NotificationType type) {
  std::lock_guard<std::mutex> lock(mutex_);
  pending_counts_.erase(static_cast<int>(type));
}

void NotificationSystem::Reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  handlers_.clear();
  recent_.clear();
  pending_counts_.clear();
  next_handler_id_ = 1;
}

}
