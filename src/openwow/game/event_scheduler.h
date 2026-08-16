#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace openwow::game {

class EventScheduler {
 public:
  using EventId  = uint32_t;
  using Callback = std::function<void()>;

  static EventScheduler& Get();

  EventId ScheduleOnce(float delay_seconds, Callback callback,
                       const std::string& debug_name = "");

  EventId ScheduleRepeating(float interval_seconds, Callback callback,
                            const std::string& debug_name = "");

  void Cancel(EventId id);

  void CancelByName(const std::string& name);

  void CancelAll();

  void Update(float dt);

  [[nodiscard]] size_t GetPendingCount() const;

  [[nodiscard]] bool IsPending(EventId id) const;

 private:
  EventScheduler() = default;

  struct ScheduledEvent {
    EventId id = 0;
    float time_remaining = 0.0f;
    float interval = 0.0f;
    Callback callback;
    std::string debug_name;
    bool cancelled = false;
  };

  std::vector<ScheduledEvent> events_;
  EventId next_id_ = 1;
  mutable std::mutex mutex_;
};

}
