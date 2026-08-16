#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace openwow::core {

enum class TaskPriority : uint8_t {
    Low      = 0,
    Normal   = 1,
    High     = 2,
    Critical = 3
};

enum class TaskStatus : uint8_t {
    Pending   = 0,
    Running   = 1,
    Completed = 2,
    Failed    = 3,
    Cancelled = 4
};

struct TaskInfo {
    uint32_t    taskId        = 0;
    std::string name;
    TaskPriority priority     = TaskPriority::Normal;
    TaskStatus   status       = TaskStatus::Pending;
    double       submittedTime  = 0.0;
    double       completedTime  = 0.0;
};

class ThreadPoolSystem {
public:
    ThreadPoolSystem();
    ~ThreadPoolSystem();

    ThreadPoolSystem(const ThreadPoolSystem&) = delete;
    ThreadPoolSystem& operator=(const ThreadPoolSystem&) = delete;

    void Initialize(uint32_t numThreads = 0);

    void Shutdown();

    uint32_t Submit(const std::string& name, TaskPriority priority, std::function<void()> callable);

    [[nodiscard]] TaskStatus GetTaskStatus(uint32_t taskId) const;

    [[nodiscard]] uint32_t GetPendingCount() const;

    [[nodiscard]] uint32_t GetRunningCount() const;

    [[nodiscard]] uint32_t GetCompletedCount() const;

    [[nodiscard]] uint32_t GetThreadCount() const;

    void WaitForAll();

    bool CancelTask(uint32_t taskId);

    bool ForgetTask(uint32_t taskId) noexcept;

    [[nodiscard]] uint32_t GetQueueSize() const;

    [[nodiscard]] bool IsInitialized() const;

    [[nodiscard]] std::optional<TaskInfo> GetTaskInfo(uint32_t taskId) const;

private:
    struct InternalTask {
        uint32_t                taskId   = 0;
        std::string             name;
        TaskPriority            priority = TaskPriority::Normal;
        TaskStatus              status   = TaskStatus::Pending;
        double                  submittedTime  = 0.0;
        double                  completedTime  = 0.0;
        std::function<void()>   callable;
    };

    void WorkerLoop();
    static double Now();

    mutable std::mutex              m_mutex;
    std::condition_variable         m_cv;
    std::condition_variable         m_doneCV;
    bool                            m_initialized = false;
    bool                            m_stopping    = false;
    bool                            m_syncMode    = false;
    uint32_t                        m_nextId      = 1;

    std::deque<InternalTask>        m_queues[4];

    std::unordered_map<uint32_t, TaskInfo> m_tasks;
    std::unordered_set<uint32_t> m_forgetOnCompletion;

    std::atomic<uint32_t>           m_runningCount{0};
    std::atomic<uint32_t>           m_completedCount{0};

    std::vector<std::thread>        m_threads;
};

}
