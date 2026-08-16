#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <queue>
#include <unordered_map>
#include <vector>

namespace openwow::core {

using TaskId = uint32_t;
using TaskCallback = std::function<void()>;

class TaskScheduler {
public:
    TaskScheduler() = default;
    ~TaskScheduler() = default;

    TaskScheduler(const TaskScheduler&) = delete;
    TaskScheduler& operator=(const TaskScheduler&) = delete;

    TaskId Schedule(float delaySeconds, TaskCallback callback);

    TaskId ScheduleRepeating(float intervalSeconds, TaskCallback callback);

    TaskId ScheduleAt(float absoluteTime, TaskCallback callback);

    bool Cancel(TaskId id);

    void CancelAll();

    void Update(float dt);

    [[nodiscard]] size_t GetPendingCount() const;

    [[nodiscard]] float GetCurrentTime() const;

    [[nodiscard]] bool IsScheduled(TaskId id) const;

    [[nodiscard]] std::optional<float> GetNextFireTime() const;

    void Pause();

    void Resume();

    [[nodiscard]] bool IsPaused() const;

    void Reset();

private:
    struct Task {
        TaskId id;
        float fireTime;
        float interval;
        TaskCallback callback;

        bool operator>(const Task& other) const { return fireTime > other.fireTime; }
    };

    TaskId nextId_{1};
    float currentTime_{0.0f};
    bool paused_{false};

    std::priority_queue<Task, std::vector<Task>, std::greater<Task>> heap_;

    std::unordered_map<TaskId, bool> active_;
};

}
