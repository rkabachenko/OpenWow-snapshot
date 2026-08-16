
#pragma once

#include "storm_sync.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace openwow::core {

using StormThreadProc = std::function<int(void* param)>;

namespace storm_thread_detail {

bool TryMapStormPriorityLevelToWin32Priority(int level, int& outPriority);
bool ApplyWin32PriorityToStormLevelFallthrough(int nativePriority, int& outLevel);
inline std::uint32_t ResolveRequestedStackSize(std::uint32_t requestedStackSize,
                                               std::uint32_t defaultStackSize) {
    return requestedStackSize == 0 ? defaultStackSize : requestedStackSize;
}
const char* FindFirstOfWithinN(const char* text, const char* charSet, std::size_t count);
bool ConsumeFixedInterval(double& accumulatedSeconds,
                          double deltaSeconds,
                          double intervalSeconds);
std::string BuildQuotedProcessCommandLine(const char* executable,
                                          const char* newlineSeparatedArguments);

}

enum class StormThreadFlags : uint32_t {
    None           = 0,
    CreateSuspended = 1 << 0,
};

struct StormThreadBlock {
    std::uintptr_t          handle           = 0;
    uint32_t                threadId         = 0;
    void*                   param            = nullptr;
    StormThreadProc         proc;
    std::atomic<int32_t>    startedFlag{0};
    std::atomic<int32_t>    refCount{2};
    std::atomic<int32_t>    exitCode{0};
    SEvent                  completionEvent{true, false};
    std::string             name;

    [[nodiscard]] uint32_t WaitForCompletion(
        const uint32_t milliseconds = 0xFFFFFFFFu) {
        return completionEvent.Wait(milliseconds);
    }
};

class StormWorkerSlot {
public:
    StormWorkerSlot();

    StormWorkerSlot(const StormWorkerSlot&) = delete;
    StormWorkerSlot& operator=(const StormWorkerSlot&) = delete;

    [[nodiscard]] std::shared_ptr<StormThreadBlock> worker_thread() const {
        return workerThread_;
    }

    [[nodiscard]] void* owning_pool() const { return owningPool_; }

    [[nodiscard]] SEvent& dispatch_event() { return dispatchEvent_; }
    [[nodiscard]] const SEvent& dispatch_event() const { return dispatchEvent_; }

    [[nodiscard]] SEvent& ready_event() { return readyEvent_; }
    [[nodiscard]] const SEvent& ready_event() const { return readyEvent_; }

    [[nodiscard]] std::shared_ptr<StormThreadBlock> active_thread_block() const {
        return activeThreadBlock_;
    }

    [[nodiscard]] bool has_callback() const {
        return static_cast<bool>(callback_);
    }

    [[nodiscard]] void* callback_param() const { return callbackParam_; }
    [[nodiscard]] int priority_level_snapshot() const {
        return priorityLevelSnapshot_;
    }
    [[nodiscard]] std::uintptr_t affinity_mask_snapshot() const {
        return affinityMaskSnapshot_;
    }

private:
    friend class StormWorkerPool;

    void Shutdown();

    std::shared_ptr<StormThreadBlock> workerThread_;
    void*                             owningPool_ = nullptr;
    SEvent                            dispatchEvent_;
    SEvent                            readyEvent_;
    std::shared_ptr<StormThreadBlock> activeThreadBlock_;
    StormThreadProc                   callback_;
    void*                             callbackParam_ = nullptr;
    int                               priorityLevelSnapshot_ = 0;
    std::uintptr_t                    affinityMaskSnapshot_ = 0;
};

class StormWorkerPool {
public:
    explicit StormWorkerPool(std::size_t initial_workers = 0,
                             std::size_t max_workers = 0);
    ~StormWorkerPool();

    StormWorkerPool(const StormWorkerPool&) = delete;
    StormWorkerPool& operator=(const StormWorkerPool&) = delete;

    [[nodiscard]] bool Submit(StormThreadProc proc, void* param = nullptr);
    void Shutdown();

    [[nodiscard]] std::size_t worker_count() const;

private:
    static int WorkerProc(void* param);
    StormWorkerSlot* CreateWorkerLocked();

    const std::size_t maxWorkers_;
    mutable std::mutex mutex_;
    std::vector<std::unique_ptr<StormWorkerSlot>> workers_;
    std::vector<StormWorkerSlot*> idleWorkers_;
    bool shuttingDown_ = false;
};

int SThread_SpawnProcess(const char* applicationName,
                         const char* commandLine,
                         std::uintptr_t waitCallback,
                         std::intptr_t callbackArg);

class StormThread {
public:
    static StormThread& Instance();

    std::shared_ptr<StormThreadBlock> Create(
        StormThreadProc proc,
        void* param,
        const std::string& name,
        StormThreadFlags flags = StormThreadFlags::None,
        uint32_t stackSize = 0);

    bool CreateDetached(
        StormThreadProc proc,
        void* param,
        const std::string& name);

    std::shared_ptr<StormThreadBlock> CreateWorkerThread(
        StormThreadProc proc,
        void* param,
        const std::string& name);

    void EnsurePrimaryThreadBlock();

    void ShutdownPrimaryThread();

    [[nodiscard]] bool SubmitToSingletonWorkerPool(
        StormThreadProc proc,
        void* param = nullptr);

    void ShutdownSingletonWorkerPool();

    [[nodiscard]] bool HasSingletonWorkerPool();

    void SetDefaultStackSize(uint32_t size);
    uint32_t GetDefaultStackSize() const;

    static bool SetPriorityFromLevel(std::thread::native_handle_type handle, int level);

    static bool GetPriorityToLevel(std::thread::native_handle_type handle, int& outLevel);

private:
    StormThread() = default;

    static void EntryPoint(std::shared_ptr<StormThreadBlock> block);
#if defined(_WIN32)
    static unsigned __stdcall EntryPointThunk(void* rawContext);
#else
    static void* EntryPointThunk(void* rawContext);
#endif
    std::shared_ptr<StormThreadBlock> CreateManagedBlock();
    void RegisterThreadBlock(const StormThreadBlock* block);
    void UnregisterThreadBlock(const StormThreadBlock* block);

    mutable std::mutex mutex_;
    uint32_t    defaultStackSize_ = 0;
    std::unordered_set<const StormThreadBlock*> liveBlocks_;
    std::shared_ptr<StormThreadBlock> primaryThreadBlock_;
    std::shared_ptr<StormWorkerPool> singletonWorkerPool_;
};

}
