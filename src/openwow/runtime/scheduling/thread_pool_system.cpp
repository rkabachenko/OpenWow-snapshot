
#include "openwow/runtime/scheduling/thread_pool_system.h"

#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace openwow::core {

double ThreadPoolSystem::Now() {
    using Clock = std::chrono::steady_clock;
    auto tp = Clock::now().time_since_epoch();
    return std::chrono::duration<double>(tp).count();
}

ThreadPoolSystem::ThreadPoolSystem() = default;

ThreadPoolSystem::~ThreadPoolSystem() {
    if (m_initialized) Shutdown();
}

void ThreadPoolSystem::Initialize(uint32_t numThreads) {
    std::lock_guard lock(m_mutex);
    if (m_initialized) return;

    m_stopping = false;
    m_initialized = true;
    m_completedCount.store(0);
    m_runningCount.store(0);
    m_nextId = 1;
    m_tasks.clear();
    m_forgetOnCompletion.clear();
    for (auto& q : m_queues) q.clear();

    if (numThreads == 0) {
        m_syncMode = true;
        return;
    }

    m_syncMode = false;
    uint32_t count = numThreads;
    if (count == 0) count = 1;

    for (uint32_t i = 0; i < count; ++i)
        m_threads.emplace_back(&ThreadPoolSystem::WorkerLoop, this);
}

void ThreadPoolSystem::Shutdown() {
    {
        std::lock_guard lock(m_mutex);
        if (!m_initialized) return;
        m_stopping = true;
        m_initialized = false;
        for (auto& queue : m_queues) {
            for (const auto& task : queue) {
                if (auto it = m_tasks.find(task.taskId); it != m_tasks.end()) {
                    it->second.status = TaskStatus::Cancelled;
                    it->second.completedTime = Now();
                }
            }
            queue.clear();
        }
    }
    m_cv.notify_all();
    m_doneCV.notify_all();

    for (auto& t : m_threads) {
        if (t.joinable()) t.join();
    }
    m_threads.clear();
    m_syncMode = false;
}

uint32_t ThreadPoolSystem::Submit(const std::string& name, TaskPriority priority, std::function<void()> callable) {
    std::unique_lock lock(m_mutex);
    if (!m_initialized)
        throw std::runtime_error("ThreadPoolSystem::Submit: not initialized");

    InternalTask task;
    task.taskId        = m_nextId++;
    task.name          = name;
    task.priority      = priority;
    task.status        = TaskStatus::Pending;
    task.submittedTime = Now();
    task.callable      = std::move(callable);

    uint32_t id = task.taskId;

    m_tasks.emplace(id, TaskInfo{
        .taskId = id,
        .name = task.name,
        .priority = task.priority,
        .status = TaskStatus::Pending,
        .submittedTime = task.submittedTime,
    });

    if (m_syncMode) {

        task.status = TaskStatus::Running;
        m_tasks[id].status = TaskStatus::Running;
        m_runningCount.fetch_add(1);
        lock.unlock();

        bool ok = true;
        try { task.callable(); }
        catch (...) {
            openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kError,
                "ThreadPool: sync task " + std::to_string(id) + " threw an exception");
            ok = false;
        }

        lock.lock();
        m_runningCount.fetch_sub(1);
        m_tasks[id].status = ok ? TaskStatus::Completed : TaskStatus::Failed;
        m_tasks[id].completedTime = Now();
        if (m_forgetOnCompletion.erase(id) != 0u) {
            m_tasks.erase(id);
        }
        m_completedCount.fetch_add(1);
        m_doneCV.notify_all();
        return id;
    }

    try {
        m_queues[static_cast<int>(priority)].push_back(std::move(task));
    } catch (...) {
        m_tasks.erase(id);
        throw;
    }
    lock.unlock();
    m_cv.notify_one();
    return id;
}

void ThreadPoolSystem::WorkerLoop() {
    while (true) {
        InternalTask task;
        {
            std::unique_lock lock(m_mutex);
            m_cv.wait(lock, [this] {
                if (m_stopping) return true;
                for (auto& q : m_queues)
                    if (!q.empty()) return true;
                return false;
            });

            if (m_stopping) {

                return;
            }

            bool found = false;
            for (int p = 3; p >= 0; --p) {
                if (!m_queues[p].empty()) {
                    task = std::move(m_queues[p].front());
                    m_queues[p].pop_front();
                    found = true;
                    break;
                }
            }
            if (!found) continue;

            task.status = TaskStatus::Running;
            m_tasks[task.taskId].status = TaskStatus::Running;
            m_runningCount.fetch_add(1);
        }

        bool ok = true;
        try { task.callable(); }
        catch (...) {
            openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kError,
                "ThreadPool: task " + std::to_string(task.taskId) + " threw an exception");
            ok = false;
        }

        {
            std::lock_guard lock(m_mutex);
            m_runningCount.fetch_sub(1);
            task.status = ok ? TaskStatus::Completed : TaskStatus::Failed;
            task.completedTime = Now();
            auto& info = m_tasks[task.taskId];
            info.status = task.status;
            info.completedTime = task.completedTime;
            if (m_forgetOnCompletion.erase(task.taskId) != 0u) {
                m_tasks.erase(task.taskId);
            }
            m_completedCount.fetch_add(1);
        }
        m_doneCV.notify_all();
    }
}

TaskStatus ThreadPoolSystem::GetTaskStatus(uint32_t taskId) const {
    std::lock_guard lock(m_mutex);
    auto it = m_tasks.find(taskId);
    if (it == m_tasks.end()) return TaskStatus::Pending;
    return it->second.status;
}

uint32_t ThreadPoolSystem::GetPendingCount() const {
    std::lock_guard lock(m_mutex);
    uint32_t n = 0;
    for (auto& q : m_queues) n += static_cast<uint32_t>(q.size());
    return n;
}

uint32_t ThreadPoolSystem::GetRunningCount() const {
    return m_runningCount.load();
}

uint32_t ThreadPoolSystem::GetCompletedCount() const {
    return m_completedCount.load();
}

uint32_t ThreadPoolSystem::GetThreadCount() const {
    return static_cast<uint32_t>(m_threads.size());
}

void ThreadPoolSystem::WaitForAll() {
    std::unique_lock lock(m_mutex);
    m_doneCV.wait(lock, [this] {
        for (auto& q : m_queues)
            if (!q.empty()) return false;
        return m_runningCount.load() == 0;
    });
}

bool ThreadPoolSystem::CancelTask(uint32_t taskId) {
    std::lock_guard lock(m_mutex);

    for (auto& q : m_queues) {
        for (auto it = q.begin(); it != q.end(); ++it) {
            if (it->taskId == taskId && it->status == TaskStatus::Pending) {
                q.erase(it);
                m_tasks[taskId].status = TaskStatus::Cancelled;
                m_doneCV.notify_all();
                return true;
            }
        }
    }
    return false;
}

bool ThreadPoolSystem::ForgetTask(const uint32_t taskId) noexcept {
    try {
        std::lock_guard lock(m_mutex);
        const auto task = m_tasks.find(taskId);
        if (task == m_tasks.end()) {
            return true;
        }
        if (task->second.status == TaskStatus::Pending ||
            task->second.status == TaskStatus::Running) {
            m_forgetOnCompletion.insert(taskId);
            return true;
        }
        m_tasks.erase(task);
        return true;
    } catch (...) {
        return false;
    }
}

uint32_t ThreadPoolSystem::GetQueueSize() const {
    std::lock_guard lock(m_mutex);
    uint32_t n = 0;
    for (auto& q : m_queues) n += static_cast<uint32_t>(q.size());
    return n + m_runningCount.load();
}

bool ThreadPoolSystem::IsInitialized() const {
    std::lock_guard lock(m_mutex);
    return m_initialized;
}

std::optional<TaskInfo> ThreadPoolSystem::GetTaskInfo(uint32_t taskId) const {
    std::lock_guard lock(m_mutex);
    auto it = m_tasks.find(taskId);
    if (it == m_tasks.end()) return std::nullopt;
    return it->second;
}

}
