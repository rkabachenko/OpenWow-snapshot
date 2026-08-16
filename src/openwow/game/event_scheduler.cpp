
#include "openwow/game/event_scheduler.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>

namespace openwow::game {

EventScheduler& EventScheduler::Get() {
  static EventScheduler instance;
  return instance;
}

EventScheduler::EventId EventScheduler::ScheduleOnce(
    float delay_seconds, Callback callback, const std::string& debug_name) {
  std::lock_guard<std::mutex> lock(mutex_);

  EventId id = next_id_++;
  events_.push_back(ScheduledEvent{
      id,
      delay_seconds,
      0.0f,
      std::move(callback),
      debug_name,
      false,
  });
  return id;
}

EventScheduler::EventId EventScheduler::ScheduleRepeating(
    float interval_seconds, Callback callback, const std::string& debug_name) {
  std::lock_guard<std::mutex> lock(mutex_);

  EventId id = next_id_++;
  events_.push_back(ScheduledEvent{
      id,
      interval_seconds,
      interval_seconds,
      std::move(callback),
      debug_name,
      false,
  });
  return id;
}

void EventScheduler::Cancel(EventId id) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& ev : events_) {
    if (ev.id == id) {
      ev.cancelled = true;
      return;
    }
  }
}

void EventScheduler::CancelByName(const std::string& name) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& ev : events_) {
    if (ev.debug_name == name) {
      ev.cancelled = true;
    }
  }
}

void EventScheduler::CancelAll() {
  std::lock_guard<std::mutex> lock(mutex_);
  events_.clear();
}

void EventScheduler::Update(float dt) {

  std::vector<Callback> to_fire;

  {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& ev : events_) {
      if (ev.cancelled) continue;

      ev.time_remaining -= dt;
      if (ev.time_remaining <= 0.0f) {
        to_fire.push_back(ev.callback);
        if (ev.interval > 0.0f) {

          ev.time_remaining += ev.interval;
          if (ev.time_remaining <= 0.0f)
            ev.time_remaining = ev.interval;
        } else {

          ev.cancelled = true;
        }
      }
    }

    events_.erase(
        std::remove_if(events_.begin(), events_.end(),
                       [](const ScheduledEvent& e) { return e.cancelled; }),
        events_.end());
  }

  for (auto& cb : to_fire) {
    if (cb) cb();
  }
}

size_t EventScheduler::GetPendingCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  size_t count = 0;
  for (const auto& ev : events_) {
    if (!ev.cancelled) ++count;
  }
  return count;
}

bool EventScheduler::IsPending(EventId id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto& ev : events_) {
    if (ev.id == id && !ev.cancelled) return true;
  }
  return false;
}

}
