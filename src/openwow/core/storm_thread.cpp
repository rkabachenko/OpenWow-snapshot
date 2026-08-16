
#include "storm_thread.h"
#include "storm_memory.h"
#include "storm_tls.h"
#include "storm_utils.h"
#include "openwow/runtime/scheduling/frame_scheduler.h"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <new>
#include <thread>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <process.h>
#  include <windows.h>
#else
#  include <pthread.h>
#  include <spawn.h>
#  include <sys/syscall.h>
#  include <sys/wait.h>
#  include <unistd.h>
#endif

#ifndef _WIN32
extern char** environ;
#endif

namespace openwow::core {

namespace {

using StormProcessWaitProc = int (*)(int);

struct NativeThreadStartContext {
    std::shared_ptr<StormThreadBlock> block;
};

StormProcessWaitProc DecodeWaitProc(std::uintptr_t wait_callback) {
    return reinterpret_cast<StormProcessWaitProc>(wait_callback);
}

constexpr double kSpawnProcessPollIntervalSeconds = 0.25;
constexpr int kStormPriorityMap[] = {-15, -2, -1, 0, 1, 2, 15};
constexpr std::size_t kLegacyThreadNameCapacity = 16;
std::atomic_uint32_t g_nextStormThreadId{1};

std::uint32_t GetCurrentThreadIdValue() {
#if defined(_WIN32)
    return ::GetCurrentThreadId();
#elif defined(__APPLE__)
    std::uint64_t threadId = 0;
    (void)::pthread_threadid_np(nullptr, &threadId);
    return static_cast<std::uint32_t>(threadId);
#elif defined(SYS_gettid)
    return static_cast<std::uint32_t>(::syscall(SYS_gettid));
#else
    return static_cast<std::uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
#endif
}

template <typename OpaqueHandle>
std::uintptr_t EncodeOpaqueHandle(const OpaqueHandle& handle) {
    static_assert(sizeof(OpaqueHandle) <= sizeof(std::uintptr_t));

    std::uintptr_t encoded = 0;
    std::memcpy(&encoded, &handle, sizeof(OpaqueHandle));
    return encoded;
}

std::string CopyLegacyThreadName(std::string_view name) {
    constexpr std::size_t maxChars = kLegacyThreadNameCapacity - 1;
    if (name.size() <= maxChars) {
        return std::string(name);
    }
    return std::string(name.substr(0, maxChars));
}

#if !defined(_WIN32)
std::size_t NormalizePosixStackSize(const std::uint32_t requestedStackSize) {
    if (requestedStackSize == 0) {
        return 0;
    }

    std::size_t stackSize = requestedStackSize;
#  if defined(PTHREAD_STACK_MIN)
    if (stackSize < static_cast<std::size_t>(PTHREAD_STACK_MIN)) {
        stackSize = static_cast<std::size_t>(PTHREAD_STACK_MIN);
    }
#  endif

    const long pageSize = ::sysconf(_SC_PAGESIZE);
    if (pageSize > 0) {
        const auto alignment = static_cast<std::size_t>(pageSize);
        stackSize = ((stackSize + alignment) - 1u) & ~(alignment - 1u);
    }

    return stackSize;
}
#endif

#if defined(_WIN32)
std::uintptr_t CaptureWorkerAffinityMaskSnapshot() {
    DWORD_PTR process_mask = 0;
    DWORD_PTR system_mask = 0;
    if (::GetProcessAffinityMask(::GetCurrentProcess(), &process_mask,
                                 &system_mask) == 0) {
        return 0;
    }

    return static_cast<std::uintptr_t>(system_mask);
}

void RestoreCurrentWorkerState(const int priority_level,
                               const std::uintptr_t affinity_mask) {
    (void)StormThread::SetPriorityFromLevel(::GetCurrentThread(),
                                           priority_level);
    if (affinity_mask != 0) {
        (void)::SetThreadAffinityMask(
            ::GetCurrentThread(), static_cast<DWORD_PTR>(affinity_mask));
    }
}
#else
void RestoreCurrentWorkerState(const int, const std::uintptr_t) {
}
#endif

}

namespace storm_thread_detail {

bool TryMapStormPriorityLevelToWin32Priority(int level, int& outPriority) {
    if (level < 0 ||
        level >= static_cast<int>(sizeof(kStormPriorityMap) / sizeof(kStormPriorityMap[0]))) {
        return false;
    }

    outPriority = kStormPriorityMap[level];
    return true;
}

bool ApplyWin32PriorityToStormLevelFallthrough(int nativePriority, int& outLevel) {
    switch (nativePriority) {
        case -15:
            outLevel = 0;
            [[fallthrough]];
        case -2:
            outLevel = 1;
            [[fallthrough]];
        case -1:
            outLevel = 2;
            [[fallthrough]];
        case 0:
            outLevel = 3;
            [[fallthrough]];
        case 1:
            outLevel = 4;
            [[fallthrough]];
        case 2:
            outLevel = 5;
            [[fallthrough]];
        case 15:
            outLevel = 6;
            return true;
        default:
            return false;
    }
}

const char* FindFirstOfWithinN(const char* text,
                               const char* charSet,
                               std::size_t count) {
    if (!text || !charSet || count < 1 || *text == '\0' || *charSet == '\0') {
        return nullptr;
    }

    const char* const limit = text + count;
    for (const char* cursor = text; cursor < limit && *cursor != '\0'; ++cursor) {
        for (const char* match = charSet; *match != '\0'; ++match) {
            if (*cursor == *match) {
                return cursor;
            }
        }
    }

    return nullptr;
}

bool ConsumeFixedInterval(double& accumulatedSeconds,
                          double deltaSeconds,
                          double intervalSeconds) {
    if (intervalSeconds <= 0.0 || deltaSeconds <= 0.0) {
        return false;
    }

    if (accumulatedSeconds < 0.0) {
        accumulatedSeconds = 0.0;
    }

    accumulatedSeconds += deltaSeconds;
    if (accumulatedSeconds < intervalSeconds) {
        return false;
    }

    while (accumulatedSeconds >= intervalSeconds) {
        accumulatedSeconds -= intervalSeconds;
    }

    return true;
}

std::string BuildQuotedProcessCommandLine(const char* executable,
                                          const char* newlineSeparatedArguments) {
    if (!executable || *executable == '\0') {
        return {};
    }

    std::string command_line;
    command_line.reserve(std::strlen(executable) + 2);
    command_line.push_back('"');
    command_line.append(executable);
    command_line.push_back('"');

    const char* cursor = newlineSeparatedArguments;
    if (!cursor) {
        return command_line;
    }

    while (*cursor != '\0') {
        const std::size_t line_length = std::strcspn(cursor, "\n");
        command_line.push_back(' ');

        const bool needs_quotes =
            FindFirstOfWithinN(cursor, "\t ", line_length) != nullptr;
        if (needs_quotes) {
            command_line.push_back('"');
        }
        command_line.append(cursor, line_length);
        if (needs_quotes) {
            command_line.push_back('"');
        }

        cursor += line_length;
        cursor += std::strspn(cursor, "\n");
    }

    return command_line;
}

}

namespace {

#ifdef _WIN32
using StormProcessWaitHandle = HANDLE;
#else
using StormProcessWaitHandle = pid_t;
#endif

struct PendingStormProcessWait {
    StormProcessWaitHandle handle {};
    std::uintptr_t wait_callback = 0;
    std::intptr_t callback_arg = 0;
};

bool PollProcessWaitReady(StormProcessWaitHandle handle) {
#ifdef _WIN32
    if (!handle) {
        return false;
    }

    return ::WaitForSingleObjectEx(handle, 0, FALSE) == WAIT_OBJECT_0;
#else
    if (handle <= 0) {
        return false;
    }

    int status = 0;
    const pid_t result = ::waitpid(handle, &status, WNOHANG);
    if (result == 0) {
        return false;
    }
    if (result == handle) {
        return true;
    }

    return result == -1 && errno == ECHILD;
#endif
}

void ReleaseProcessWaitHandle(StormProcessWaitHandle handle) {
#ifdef _WIN32
    if (handle) {
        ::CloseHandle(handle);
    }
#else
    static_cast<void>(handle);
#endif
}

class StormProcessWaitRegistry {
public:
    static StormProcessWaitRegistry& Instance() {
        static StormProcessWaitRegistry registry;
        return registry;
    }

    void Register(StormProcessWaitHandle handle,
                  std::uintptr_t wait_callback,
                  std::intptr_t callback_arg) {
        if (wait_callback == 0) {
            ReleaseProcessWaitHandle(handle);
            return;
        }

        bool register_pump = false;
        {
            std::lock_guard lock(mutex_);
            waits_.push_back(PendingStormProcessWait{handle, wait_callback, callback_arg});
            register_pump = pump_handle_ == CallbackHandle::Invalid;
        }

        if (!register_pump) {
            return;
        }

        const CallbackHandle handle_id = FrameScheduler::Instance().Register(
            Phase::LateUpdate, 1000,
            [](double delta_seconds) {
                StormProcessWaitRegistry::Instance().Update(delta_seconds);
            },
            "StormProcessWaitRegistry");

        if (handle_id == CallbackHandle::Invalid) {
            return;
        }

        CallbackHandle duplicate = CallbackHandle::Invalid;
        {
            std::lock_guard lock(mutex_);
            if (pump_handle_ == CallbackHandle::Invalid) {
                pump_handle_ = handle_id;
                return;
            }
            duplicate = handle_id;
        }

        FrameScheduler::Instance().Unregister(duplicate);
    }

private:
    void Update(double delta_seconds) {
        std::vector<PendingStormProcessWait> completed;
        CallbackHandle handle_to_unregister = CallbackHandle::Invalid;

        {
            std::lock_guard lock(mutex_);
            if (waits_.empty()) {
                accumulated_seconds_ = 0.0;
                handle_to_unregister = pump_handle_;
                pump_handle_ = CallbackHandle::Invalid;
            } else if (storm_thread_detail::ConsumeFixedInterval(
                           accumulated_seconds_, delta_seconds,
                           kSpawnProcessPollIntervalSeconds)) {

                auto it = waits_.begin();
                while (it != waits_.end()) {
                    if (!PollProcessWaitReady(it->handle)) {
                        ++it;
                        continue;
                    }

                    completed.push_back(*it);
                    it = waits_.erase(it);
                }

                if (waits_.empty()) {
                    accumulated_seconds_ = 0.0;
                    handle_to_unregister = pump_handle_;
                    pump_handle_ = CallbackHandle::Invalid;
                }
            }
        }

        if (handle_to_unregister != CallbackHandle::Invalid) {
            FrameScheduler::Instance().Unregister(handle_to_unregister);
        }

        for (const PendingStormProcessWait& wait : completed) {
            ReleaseProcessWaitHandle(wait.handle);
            if (const auto callback = DecodeWaitProc(wait.wait_callback)) {
                callback(static_cast<int>(wait.callback_arg));
            }
        }
    }

    std::mutex mutex_;
    std::vector<PendingStormProcessWait> waits_;
    double accumulated_seconds_ = 0.0;
    CallbackHandle pump_handle_ = CallbackHandle::Invalid;
};

#ifndef _WIN32
std::vector<std::string> ParseSpawnCommandLine(const char* command_line) {
    std::vector<std::string> args;
    if (!command_line || !*command_line) {
        return args;
    }

    std::string current;
    bool in_quotes = false;
    unsigned int backslash_count = 0;

    for (const char* cursor = command_line;; ++cursor) {
        const char ch = *cursor;
        if (ch == '\\') {
            ++backslash_count;
            continue;
        }

        if (ch == '"') {
            current.append(backslash_count / 2, '\\');
            if ((backslash_count & 1U) != 0U) {
                current.push_back('"');
            } else {
                in_quotes = !in_quotes;
            }
            backslash_count = 0;
            continue;
        }

        if (backslash_count != 0) {
            current.append(backslash_count, '\\');
            backslash_count = 0;
        }

        if (ch == '\0') {
            if (!current.empty()) {
                args.emplace_back(std::move(current));
            }
            break;
        }

        if ((ch == ' ' || ch == '\t') && !in_quotes) {
            if (!current.empty()) {
                args.emplace_back(std::move(current));
                current.clear();
            }
            continue;
        }

        current.push_back(ch);
    }

    return args;
}

std::string ResolveSpawnExecutable(const char* application_name,
                                   const std::vector<std::string>& args) {
    const char* candidate = application_name;
    if ((!candidate || !*candidate) && !args.empty()) {
        candidate = args.front().c_str();
    }
    if (!candidate || !*candidate) {
        return {};
    }

    std::filesystem::path path(candidate);
    if (path.has_parent_path()) {
        return path.string();
    }

    std::error_code ec;
    const std::filesystem::path local_candidate =
        std::filesystem::current_path(ec) / path;
    if (!ec && std::filesystem::exists(local_candidate, ec)) {
        return local_candidate.string();
    }

    return path.string();
}
#endif

}

int SThread_SpawnProcess(const char* application_name,
                         const char* command_line,
                         std::uintptr_t wait_callback,
                         std::intptr_t callback_arg) {
    if ((!application_name || !*application_name) &&
        (!command_line || !*command_line)) {
        return 0;
    }

#ifdef _WIN32
    STARTUPINFOA startup_info {};
    startup_info.cb = sizeof(startup_info);
    PROCESS_INFORMATION process_info {};

    const bool has_application_name = application_name && *application_name;
    const bool has_command_line = command_line && *command_line;
    const std::string ansi_application_name =
        Utf8ToCurrentCodePageString(application_name);
    const std::string ansi_command_line =
        Utf8ToCurrentCodePageString(command_line);

    std::vector<char> mutable_command;
    if (has_command_line) {
        mutable_command.assign(ansi_command_line.begin(), ansi_command_line.end());
        mutable_command.push_back('\0');
    }

    const BOOL created = ::CreateProcessA(
        has_application_name ? ansi_application_name.c_str() : nullptr,
        mutable_command.empty() ? nullptr : mutable_command.data(), nullptr,
        nullptr, FALSE, 0, nullptr, nullptr, &startup_info, &process_info);
    if (!created) {
        return 0;
    }

    ::CloseHandle(process_info.hThread);
    if (wait_callback != 0) {
        if (::WaitForInputIdle(process_info.hProcess, 10000u) != 0) {
            ::CloseHandle(process_info.hProcess);
            return 0;
        }

        StormProcessWaitRegistry::Instance().Register(
            process_info.hProcess, wait_callback, callback_arg);
    } else {
        ::CloseHandle(process_info.hProcess);
    }

    return 1;
#else
    std::vector<std::string> args = ParseSpawnCommandLine(command_line);
#if defined(__APPLE__)
    std::string executable;
    const std::string_view application = application_name != nullptr
                                             ? std::string_view(application_name)
                                             : std::string_view{};
    constexpr std::string_view kMacApplicationBundleSuffix = ".app";
    const bool opens_application_bundle =
        (!command_line || !*command_line) &&
        application.size() >= kMacApplicationBundleSuffix.size() &&
        application.compare(application.size() - kMacApplicationBundleSuffix.size(),
                            kMacApplicationBundleSuffix.size(),
                            kMacApplicationBundleSuffix) == 0;
    if (opens_application_bundle) {

        constexpr std::string_view kLaunchServicesOpenTool = "/usr/bin/open";
        std::filesystem::path bundle_path(application);
        if (!bundle_path.has_parent_path()) {
            std::error_code current_path_error;
            bundle_path = std::filesystem::current_path(current_path_error) / bundle_path;
            if (current_path_error) {
                return 0;
            }
        }
        std::error_code bundle_error;
        if (!std::filesystem::exists(bundle_path, bundle_error) || bundle_error) {
            return 0;
        }
        executable = std::string(kLaunchServicesOpenTool);
        args = {executable, bundle_path.string()};
    } else {
#endif
    if (args.empty()) {
        if (!application_name || !*application_name) {
            return 0;
        }
        args.emplace_back(application_name);
    }

    const std::string resolved_executable =
        ResolveSpawnExecutable(application_name, args);
#if defined(__APPLE__)
        executable = resolved_executable;
    }
#else
    const std::string& executable = resolved_executable;
#endif
    if (executable.empty()) {
        return 0;
    }

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (std::string& arg : args) {
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);

    pid_t pid = -1;
    if (::posix_spawnp(&pid, executable.c_str(), nullptr, nullptr, argv.data(),
                       ::environ) != 0) {
        return 0;
    }

    if (wait_callback != 0) {
        StormProcessWaitRegistry::Instance().Register(pid, wait_callback,
                                                      callback_arg);
    } else {
        std::thread([pid]() {
            int status = 0;
            while (::waitpid(pid, &status, 0) == -1 && errno == EINTR) {
            }
        }).detach();
    }

    return 1;
#endif
}

StormThread& StormThread::Instance() {
    static StormThread inst;
    return inst;
}

StormWorkerSlot::StormWorkerSlot()
    : dispatchEvent_(false, false),
      readyEvent_(false, false) {}

void StormWorkerSlot::Shutdown() {
    dispatchEvent_.Set();
    readyEvent_.Wait(0xFFFFFFFFu);

    if (workerThread_) {
        dispatchEvent_.Set();
        if (workerThread_->WaitForCompletion() != 0) {
            return;
        }
        workerThread_.reset();
    }

    readyEvent_.Destroy();
    dispatchEvent_.Destroy();
}

StormWorkerPool::StormWorkerPool(const std::size_t initial_workers,
                                 const std::size_t max_workers)
    : maxWorkers_(max_workers) {
    if (initial_workers == 0) {
        return;
    }

    std::lock_guard lock(mutex_);
    for (std::size_t index = 0; index < initial_workers; ++index) {
        StormWorkerSlot* const slot = CreateWorkerLocked();
        if (!slot) {
            break;
        }

        idleWorkers_.push_back(slot);
    }
}

StormWorkerPool::~StormWorkerPool() {
    Shutdown();
}

StormWorkerSlot* StormWorkerPool::CreateWorkerLocked() {
    auto worker = std::make_unique<StormWorkerSlot>();
    worker->owningPool_ = this;

    StormWorkerSlot* const slot = worker.get();
    slot->workerThread_ = StormThread::Instance().CreateWorkerThread(
        &StormWorkerPool::WorkerProc, slot, "Blizzard Thread Pool Worker");
    if (!slot->workerThread_) {
        return nullptr;
    }

    slot->priorityLevelSnapshot_ = 0;
    slot->affinityMaskSnapshot_ = 0;
#if defined(_WIN32)
    if (slot->workerThread_->handle != 0) {
        int priority_level = 0;
        (void)StormThread::GetPriorityToLevel(
            reinterpret_cast<HANDLE>(slot->workerThread_->handle),
            priority_level);
        slot->priorityLevelSnapshot_ = priority_level;
        slot->affinityMaskSnapshot_ = CaptureWorkerAffinityMaskSnapshot();
    }
#endif
    workers_.push_back(std::move(worker));
    return slot;
}

bool StormWorkerPool::Submit(StormThreadProc proc, void* const param) {
    if (!proc) {
        return false;
    }

    StormWorkerSlot* slot = nullptr;
    {
        std::lock_guard lock(mutex_);
        if (shuttingDown_) {
            return false;
        }

        if (!idleWorkers_.empty()) {
            slot = idleWorkers_.back();
            idleWorkers_.pop_back();
        } else {
            if (maxWorkers_ != 0 && workers_.size() >= maxWorkers_) {
                return false;
            }
            slot = CreateWorkerLocked();
        }
    }

    if (!slot) {
        return false;
    }

    if (slot->readyEvent_.Wait(0xFFFFFFFFu) != 0) {
        return false;
    }

    {
        std::lock_guard lock(mutex_);
        if (shuttingDown_) {
            return false;
        }

        slot->callback_ = std::move(proc);
        slot->callbackParam_ = param;
    }

    slot->dispatchEvent_.Set();
    return true;
}

void StormWorkerPool::Shutdown() {
    std::vector<StormWorkerSlot*> workers_to_stop;
    {
        std::lock_guard lock(mutex_);
        if (workers_.empty()) {
            return;
        }

        shuttingDown_ = true;
        idleWorkers_.clear();
        workers_to_stop.reserve(workers_.size());
        for (const auto& worker : workers_) {
            workers_to_stop.push_back(worker.get());
        }
    }

    for (StormWorkerSlot* const worker : workers_to_stop) {
        worker->Shutdown();
    }

    std::lock_guard lock(mutex_);
    workers_.clear();
}

std::size_t StormWorkerPool::worker_count() const {
    std::lock_guard lock(mutex_);
    return workers_.size();
}

int StormWorkerPool::WorkerProc(void* const param) {
    auto* const slot = static_cast<StormWorkerSlot*>(param);
    if (slot == nullptr) {
        return 0;
    }

    auto* const pool = static_cast<StormWorkerPool*>(slot->owningPool_);
    if (pool == nullptr) {
        return 0;
    }

    slot->readyEvent_.Set();
    while (true) {
        slot->dispatchEvent_.Wait(0xFFFFFFFFu);

        StormThreadProc callback;
        void* callback_param = nullptr;
        bool shutting_down = false;
        {
            std::lock_guard lock(pool->mutex_);
            shutting_down = pool->shuttingDown_;
            callback = slot->callback_;
            callback_param = slot->callbackParam_;
        }

        if (!callback) {
            if (shutting_down) {
                break;
            }

            slot->readyEvent_.Set();
            continue;
        }

        (void)callback(callback_param);

        RestoreCurrentWorkerState(slot->priorityLevelSnapshot_,
                                  slot->affinityMaskSnapshot_);

        {
            std::lock_guard lock(pool->mutex_);
            slot->activeThreadBlock_.reset();
            slot->callback_ = {};
            slot->callbackParam_ = nullptr;
            if (!pool->shuttingDown_) {
                pool->idleWorkers_.push_back(slot);
            }
        }

        slot->readyEvent_.Set();
    }

    {
        std::lock_guard lock(pool->mutex_);
        slot->activeThreadBlock_.reset();
        slot->callback_ = {};
        slot->callbackParam_ = nullptr;
    }

    slot->readyEvent_.Set();
    return 0;
}

std::shared_ptr<StormThreadBlock> StormThread::CreateManagedBlock() {
    return std::shared_ptr<StormThreadBlock>(
        new StormThreadBlock(),
        [](StormThreadBlock* block) {
            if (!block) {
                return;
            }

#if defined(_WIN32)
            if (block->handle != 0) {
                ::CloseHandle(reinterpret_cast<HANDLE>(block->handle));
                block->handle = 0;
            }
#endif

            StormThread::Instance().UnregisterThreadBlock(block);
            delete block;
        });
}

void StormThread::RegisterThreadBlock(const StormThreadBlock* block) {
    if (!block) {
        return;
    }

    std::lock_guard lock(mutex_);
    liveBlocks_.insert(block);
}

void StormThread::UnregisterThreadBlock(const StormThreadBlock* block) {
    if (!block) {
        return;
    }

    std::lock_guard lock(mutex_);
    liveBlocks_.erase(block);
}

void StormThread::EnsurePrimaryThreadBlock() {
    std::shared_ptr<StormThreadBlock> primary_block;
    {
        std::lock_guard lock(mutex_);
        if (!primaryThreadBlock_) {
            primaryThreadBlock_ = CreateManagedBlock();
            liveBlocks_.insert(primaryThreadBlock_.get());
            primaryThreadBlock_->threadId = g_nextStormThreadId.fetch_add(1, std::memory_order_relaxed);
            primaryThreadBlock_->name = CopyLegacyThreadName("main");
            primaryThreadBlock_->startedFlag.store(1, std::memory_order_release);
            primaryThreadBlock_->refCount.store(1, std::memory_order_release);
        }
        primary_block = primaryThreadBlock_;
    }

    StormTls::Instance().SetThreadMemory(primary_block.get());
}

void StormThread::ShutdownPrimaryThread() {
    StormTls::Instance().CallDestructors();

    std::shared_ptr<StormThreadBlock> primary_block;
    {
        std::lock_guard lock(mutex_);
        primary_block = std::move(primaryThreadBlock_);
    }
}

bool StormThread::SubmitToSingletonWorkerPool(StormThreadProc proc,
                                              void* const param) {
    if (!proc) {
        return false;
    }

    std::shared_ptr<StormWorkerPool> pool;
    {
        std::lock_guard lock(mutex_);
        if (!primaryThreadBlock_) {
            return false;
        }
        pool = singletonWorkerPool_;
    }

    if (!pool) {
        auto candidate = std::make_shared<StormWorkerPool>(1, 0);
        std::lock_guard lock(mutex_);
        if (!primaryThreadBlock_) {
            return false;
        }
        if (!singletonWorkerPool_) {
            singletonWorkerPool_ = std::move(candidate);
        }
        pool = singletonWorkerPool_;
    }

    return pool->Submit(std::move(proc), param);
}

void StormThread::ShutdownSingletonWorkerPool() {
    std::shared_ptr<StormWorkerPool> pool;
    {
        std::lock_guard lock(mutex_);
        pool = std::move(singletonWorkerPool_);
    }

    if (pool) {
        pool->Shutdown();
    }
}

bool StormThread::HasSingletonWorkerPool() {
    std::lock_guard lock(mutex_);
    return static_cast<bool>(singletonWorkerPool_);
}

void StormThread::EntryPoint(std::shared_ptr<StormThreadBlock> block) {
    if (block->threadId == 0) {
        block->threadId = GetCurrentThreadIdValue();
    }

    StormTls::Instance().SetThreadMemory(block.get());

    while (block->startedFlag.load(std::memory_order_acquire) == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    const int result = block->proc(block->param);

    block->startedFlag.store(-1, std::memory_order_release);
    block->exitCode.store(result, std::memory_order_release);

    block->refCount.fetch_sub(1, std::memory_order_acq_rel);

    StormTls::Instance().CallDestructors();

    StormTls::Instance().SetThreadMemory(nullptr);

    auto* const rawBlock = block.get();
    const bool hasExternalOwner = block.use_count() > 1;
    if (hasExternalOwner) {
        block.reset();
        rawBlock->completionEvent.Set();
    } else {
        rawBlock->completionEvent.Set();
    }
}

#if defined(_WIN32)
unsigned __stdcall StormThread::EntryPointThunk(void* rawContext) {
    std::unique_ptr<NativeThreadStartContext> context(
        static_cast<NativeThreadStartContext*>(rawContext));
    EntryPoint(std::move(context->block));
    return 0;
}
#else
void* StormThread::EntryPointThunk(void* rawContext) {
    std::unique_ptr<NativeThreadStartContext> context(
        static_cast<NativeThreadStartContext*>(rawContext));
    EntryPoint(std::move(context->block));
    return nullptr;
}
#endif

std::shared_ptr<StormThreadBlock> StormThread::Create(
    StormThreadProc proc,
    void* param,
    const std::string& name,
    StormThreadFlags flags,
    uint32_t stackSize)
{
    StormMemory::Instance().Init();

    std::uint32_t defaultStackSize = 0;
    {
        std::lock_guard lock(mutex_);
        defaultStackSize = defaultStackSize_;
    }
    const std::uint32_t effectiveStackSize =
        storm_thread_detail::ResolveRequestedStackSize(stackSize, defaultStackSize);

    auto block = CreateManagedBlock();
    block->proc = std::move(proc);
    block->param = param;
    block->name = CopyLegacyThreadName(name);
    block->startedFlag.store(0, std::memory_order_release);
    block->refCount.store(2, std::memory_order_release);

    RegisterThreadBlock(block.get());

    auto context = std::unique_ptr<NativeThreadStartContext>(
        new (std::nothrow) NativeThreadStartContext{block});
    if (!context) {
        return nullptr;
    }

    bool created = false;
#if defined(_WIN32)
    unsigned int threadId = 0;
    const unsigned int initFlags =
        effectiveStackSize != 0 ? STACK_SIZE_PARAM_IS_A_RESERVATION : 0u;

    const uintptr_t handle =
        ::_beginthreadex(nullptr, effectiveStackSize, &StormThread::EntryPointThunk,
                         context.get(), initFlags, &threadId);
    if (handle != 0) {
        block->handle = static_cast<std::uintptr_t>(handle);
        block->threadId = threadId;
        context.release();
        created = true;
    }
#else
    pthread_attr_t attributes{};
    bool attributesInitialized = false;
    block->threadId = g_nextStormThreadId.fetch_add(1, std::memory_order_relaxed);
    int createResult = ::pthread_attr_init(&attributes);
    if (createResult == 0) {
        attributesInitialized = true;
        createResult = ::pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
    }
    if (createResult == 0 && effectiveStackSize != 0) {
        const std::size_t normalizedStackSize = NormalizePosixStackSize(effectiveStackSize);
        if (normalizedStackSize != 0) {
            createResult = ::pthread_attr_setstacksize(&attributes, normalizedStackSize);
        }
    }

    pthread_t handle{};
    if (createResult == 0) {
        createResult = ::pthread_create(
            &handle, &attributes, &StormThread::EntryPointThunk, context.get());
    }
    if (attributesInitialized) {
        ::pthread_attr_destroy(&attributes);
    }

    if (createResult == 0) {
        block->handle = EncodeOpaqueHandle(handle);
        context.release();
        created = true;
    }
#endif

    if (!created) {
        return nullptr;
    }

    const bool suspended = (static_cast<uint32_t>(flags) &
                            static_cast<uint32_t>(StormThreadFlags::CreateSuspended)) != 0;
    if (!suspended) {
        block->startedFlag.store(1, std::memory_order_release);
    }

    return block;
}

bool StormThread::CreateDetached(
    StormThreadProc proc,
    void* param,
    const std::string& name)
{
    auto block = Create(std::move(proc), param, name);
    if (!block) return false;

    block->refCount.fetch_sub(1, std::memory_order_acq_rel);
    return true;
}

std::shared_ptr<StormThreadBlock> StormThread::CreateWorkerThread(
    StormThreadProc proc,
    void* param,
    const std::string& name)
{

    auto block = Create(std::move(proc), param, name);

    return block;
}

void StormThread::SetDefaultStackSize(uint32_t size) {
    std::lock_guard lock(mutex_);
    defaultStackSize_ = size;
}

uint32_t StormThread::GetDefaultStackSize() const {
    std::lock_guard lock(mutex_);
    return defaultStackSize_;
}

bool StormThread::SetPriorityFromLevel(
    std::thread::native_handle_type handle, int level) {

#if defined(_WIN32)
    int nativePriority = 0;
    if (!storm_thread_detail::TryMapStormPriorityLevelToWin32Priority(level, nativePriority)) {
        return false;
    }
    return SetThreadPriority(handle, nativePriority) != 0;
#elif defined(__linux__)

    static constexpr int kNiceMap[7] = { 19, 10, 5, 0, -5, -10, -20 };
    if (level < 0 || level > 6) return false;
    struct sched_param param{};
    int policy = SCHED_OTHER;
    param.sched_priority = 0;
    (void)pthread_setschedparam(handle, policy, &param);

    (void)kNiceMap[level];
    return true;
#else
    (void)handle;
    (void)level;
    return level >= 0 && level <= 6;
#endif
}

bool StormThread::GetPriorityToLevel(
    std::thread::native_handle_type handle, int& outLevel) {

#if defined(_WIN32)
    const int nativePriority = GetThreadPriority(handle);
    (void)storm_thread_detail::ApplyWin32PriorityToStormLevelFallthrough(
        nativePriority, outLevel);
    return false;
#else
    (void)handle;
    (void)outLevel;
    return false;
#endif
}

}
