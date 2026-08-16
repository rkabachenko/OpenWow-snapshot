#include "openwow/runtime/scheduling/task_scheduler.h"

namespace openwow::core {

TaskId TaskScheduler::Schedule(float delaySeconds, TaskCallback callback) {
    TaskId id = nextId_++;
    float fire = currentTime_ + delaySeconds;
    heap_.push({id, fire, 0.0f, std::move(callback)});
    active_[id] = true;
    return id;
}

TaskId TaskScheduler::ScheduleRepeating(float intervalSeconds, TaskCallback callback) {
    TaskId id = nextId_++;
    float fire = currentTime_ + intervalSeconds;
    heap_.push({id, fire, intervalSeconds, std::move(callback)});
    active_[id] = true;
    return id;
}

TaskId TaskScheduler::ScheduleAt(float absoluteTime, TaskCallback callback) {
    TaskId id = nextId_++;
    heap_.push({id, absoluteTime, 0.0f, std::move(callback)});
    active_[id] = true;
    return id;
}

bool TaskScheduler::Cancel(TaskId id) {
    auto it = active_.find(id);
    if (it == active_.end() || !it->second) return false;
    it->second = false;
    return true;
}

void TaskScheduler::CancelAll() {

    active_.clear();
    heap_ = {};
}

void TaskScheduler::Update(float dt) {
    if (paused_) return;
    currentTime_ += dt;

    while (!heap_.empty() && heap_.top().fireTime <= currentTime_) {
        Task t = heap_.top();
        heap_.pop();

        auto it = active_.find(t.id);
        if (it == active_.end() || !it->second) {

            if (it != active_.end()) active_.erase(it);
            continue;
        }

        t.callback();

        if (t.interval > 0.0f) {

            float next = t.fireTime + t.interval;
            if (next <= currentTime_) next = currentTime_ + t.interval;
            heap_.push({t.id, next, t.interval, std::move(t.callback)});
        } else {
            active_.erase(t.id);
        }
    }
}

size_t TaskScheduler::GetPendingCount() const {
    size_t count = 0;
    for (auto& [id, isActive] : active_) {
        if (isActive) ++count;
    }
    return count;
}

float TaskScheduler::GetCurrentTime() const {
    return currentTime_;
}

bool TaskScheduler::IsScheduled(TaskId id) const {
    auto it = active_.find(id);
    return it != active_.end() && it->second;
}

std::optional<float> TaskScheduler::GetNextFireTime() const {

    if (heap_.empty()) return std::nullopt;
    return heap_.top().fireTime;
}

void TaskScheduler::Pause() { paused_ = true; }
void TaskScheduler::Resume() { paused_ = false; }
bool TaskScheduler::IsPaused() const { return paused_; }

void TaskScheduler::Reset() {
    active_.clear();
    heap_ = {};
    currentTime_ = 0.0f;
    paused_ = false;
    nextId_ = 1;
}

}
