
#include "storm_sync.h"
#include "storm_thread.h"

#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <new>
#include <string>
#include <thread>
#include <utility>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <sys/file.h>
#  include <unistd.h>
#endif

namespace openwow::core {

namespace {

constexpr std::uint32_t kInitialEventTokenCount = 1024;
constexpr std::uint32_t kEventTokenRingSize = 2048;
constexpr std::uint32_t kEventTokenMask = 0x7FF;
constexpr std::uint32_t kEventTokenShift = 21;
constexpr std::uint32_t kEventTokenStride = 1u << kEventTokenShift;
constexpr std::uint32_t kWaitObject0 = 0;
constexpr std::uint32_t kWaitTimeout = 0x102;
constexpr std::uint32_t kWaitFailed = 0xFFFFFFFF;
constexpr std::uint32_t kInfiniteWait = 0xFFFFFFFF;

struct EventTokenPool {
    std::atomic<std::int32_t> available{
        static_cast<std::int32_t>(kInitialEventTokenCount)};
    std::atomic<std::uint32_t> free_index{kInitialEventTokenCount - 1};
    std::atomic<std::uint32_t> alloc_index{kEventTokenRingSize - 1};
    std::array<std::atomic<std::uint32_t>, kEventTokenRingSize> slots = {};
};

std::array<EventTokenPool, 2> g_event_token_pools = {};
std::atomic<std::uint32_t> g_event_token_pool_refcount{0};
std::atomic<std::int32_t> g_event_token_spin_count{100};

const char* EventTokenPoolName(int type) {
    return type == 0 ? "SUAREVTYPE" : "SUMREVTYPE";
}

void ResetEventTokenPoolState(EventTokenPool& pool) {
    pool.available.store(static_cast<std::int32_t>(kInitialEventTokenCount),
                         std::memory_order_relaxed);
    pool.free_index.store(kInitialEventTokenCount - 1, std::memory_order_relaxed);
    pool.alloc_index.store(kEventTokenRingSize - 1, std::memory_order_relaxed);
    for (auto& slot : pool.slots) {
        slot.store(0, std::memory_order_relaxed);
    }
    for (std::uint32_t index = 0; index < kInitialEventTokenCount; ++index) {
        pool.slots[index].store((index + 1) * kEventTokenStride,
                                std::memory_order_relaxed);
    }
}

void InitializeEventTokenPools() {
    for (auto& pool : g_event_token_pools) {
        ResetEventTokenPoolState(pool);
    }
    g_event_token_spin_count.store(
        std::thread::hardware_concurrency() > 1 ? 4000 : 1,
        std::memory_order_relaxed);
}

void AcquireEventTokenPools() {
    if (g_event_token_pool_refcount.fetch_add(1, std::memory_order_acq_rel) == 0) {
        InitializeEventTokenPools();
    }
}

void ReleaseEventTokenPools() {
    const auto previous =
        g_event_token_pool_refcount.fetch_sub(1, std::memory_order_acq_rel);
    if (previous == 0) {
        g_event_token_pool_refcount.store(0, std::memory_order_relaxed);
        return;
    }
    if (previous == 1) {
        InitializeEventTokenPools();
    }
}

std::uint32_t AcquireWaitTokenLocked(std::uint32_t& token, int type) {
    if (token == 0) {
        token = static_cast<std::uint32_t>(AllocEventFromPool(type));
    }
    return token;
}

#if !defined(_WIN32)
std::string EncodeMutexNameAsHex(const char* name) {
    static constexpr char kHexDigits[] = "0123456789ABCDEF";

    std::string encoded;
    if (!name) {
        return encoded;
    }

    while (*name) {
        const auto value = static_cast<unsigned char>(*name++);
        encoded.push_back(kHexDigits[value >> 4]);
        encoded.push_back(kHexDigits[value & 0x0F]);
    }
    return encoded;
}

std::filesystem::path BuildNamedMutexPath(const char* name) {
    std::error_code error;
    auto base = std::filesystem::temp_directory_path(error);
    if (error) {
        base = ".";
    }
    return base / ("openwow_named_mutex_" + EncodeMutexNameAsHex(name) + ".lock");
}

bool TryAcquireNamedProcessLock(const int fd) {
    return fd >= 0 && ::flock(fd, LOCK_EX | LOCK_NB) == 0;
}

void ReleaseNamedProcessLock(const int fd) {
    if (fd >= 0) {
        (void)::flock(fd, LOCK_UN);
    }
}
#endif

}

void SCritSect::Enter() {
    mutex_.lock();
}

void SCritSect::Leave() {
    mutex_.unlock();
}

SEvent::SEvent(bool manual_reset, bool initial_state)
    : signaled_(initial_state), manual_reset_(manual_reset), valid_(true) {}

void SEvent::Set() {
    {
        std::lock_guard lock(mutex_);
        signaled_ = true;
    }
    if (manual_reset_) {
        cv_.notify_all();
    } else {
        cv_.notify_one();
    }
}

void SEvent::Reset() {
    std::lock_guard lock(mutex_);
    signaled_ = false;
}

uint32_t SEvent::Wait(uint32_t milliseconds) {
    std::unique_lock lock(mutex_);
    if (milliseconds == 0xFFFFFFFF) {
        cv_.wait(lock, [this] { return signaled_; });
    } else {
        if (!cv_.wait_for(lock, std::chrono::milliseconds(milliseconds),
                          [this] { return signaled_; })) {
            return 0x102;
        }
    }
    if (!manual_reset_) {
        signaled_ = false;
    }
    return 0;
}

bool SEvent::IsSignaled() const {
    std::lock_guard lock(mutex_);
    return signaled_;
}

void SEvent::Destroy() {
    valid_ = false;
}

SMutex::SMutex() {
    Create(false, nullptr);
}

SMutex::~SMutex() {
    Destroy();
}

void SMutex::Create(const bool initial_owner, const char* const name) {
    Destroy();

#if defined(_WIN32)
    native_handle_ = ::CreateMutexA(nullptr, initial_owner ? TRUE : FALSE, name);
    valid_.store(native_handle_ != nullptr, std::memory_order_release);
#else
    uses_named_process_lock_ = name != nullptr && *name != '\0';
    if (uses_named_process_lock_) {
        const auto lock_path = BuildNamedMutexPath(name);
        named_lock_fd_ = ::open(lock_path.c_str(), O_CREAT | O_RDWR, 0600);
        if (named_lock_fd_ < 0) {
            uses_named_process_lock_ = false;
            valid_.store(false, std::memory_order_release);
            return;
        }
    }

    {
        std::lock_guard lock(state_mutex_);
        owner_thread_ = {};
        recursion_count_ = 0;
    }

    valid_.store(true, std::memory_order_release);

    if (!initial_owner) {
        return;
    }

    if (uses_named_process_lock_) {
        if (TryAcquireNamedProcessLock(named_lock_fd_)) {
            std::lock_guard lock(state_mutex_);
            owner_thread_ = std::this_thread::get_id();
            recursion_count_ = 1;
        }
        return;
    }

    std::lock_guard lock(state_mutex_);
    owner_thread_ = std::this_thread::get_id();
    recursion_count_ = 1;
#endif
}

std::uint32_t SMutex::Wait(const std::uint32_t milliseconds) {
    if (!IsValid()) {
        return kWaitFailed;
    }

#if defined(_WIN32)
    return static_cast<std::uint32_t>(
        ::WaitForSingleObject(static_cast<HANDLE>(native_handle_), milliseconds));
#else
    const auto current_thread = std::this_thread::get_id();
    const auto deadline = milliseconds == kInfiniteWait
                              ? std::chrono::steady_clock::time_point::max()
                              : std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    std::unique_lock lock(state_mutex_);

    while (true) {
        if (!valid_.load(std::memory_order_acquire)) {
            return kWaitFailed;
        }

        if (owner_thread_ == current_thread) {
            ++recursion_count_;
            return kWaitObject0;
        }

        if (owner_thread_ == std::thread::id{}) {
            if (!uses_named_process_lock_) {
                owner_thread_ = current_thread;
                recursion_count_ = 1;
                return kWaitObject0;
            }

            lock.unlock();
            const bool acquired = TryAcquireNamedProcessLock(named_lock_fd_);
            lock.lock();

            if (!valid_.load(std::memory_order_acquire)) {
                return kWaitFailed;
            }

            if (owner_thread_ != std::thread::id{}) {
                continue;
            }

            if (acquired) {
                owner_thread_ = current_thread;
                recursion_count_ = 1;
                return kWaitObject0;
            }
        }

        if (milliseconds == 0) {
            return kWaitTimeout;
        }

        if (owner_thread_ != std::thread::id{}) {
            if (milliseconds == kInfiniteWait) {
                state_cv_.wait(lock, [this] {
                    return owner_thread_ == std::thread::id{} ||
                           !valid_.load(std::memory_order_acquire);
                });
                continue;
            }

            if (!state_cv_.wait_until(lock, deadline, [this] {
                    return owner_thread_ == std::thread::id{} ||
                           !valid_.load(std::memory_order_acquire);
                })) {
                return kWaitTimeout;
            }
            continue;
        }

        const auto now = std::chrono::steady_clock::now();
        if (milliseconds != kInfiniteWait && now >= deadline) {
            return kWaitTimeout;
        }

        const auto pause = milliseconds == kInfiniteWait
                               ? std::chrono::milliseconds(1)
                               : std::min(std::chrono::milliseconds(1),
                                          std::chrono::duration_cast<std::chrono::milliseconds>(
                                              deadline - now));
        lock.unlock();
        std::this_thread::sleep_for(pause);
        lock.lock();
    }
#endif
}

void SMutex::Destroy() {
#if defined(_WIN32)
    if (native_handle_ != nullptr) {
        ::CloseHandle(static_cast<HANDLE>(native_handle_));
        native_handle_ = nullptr;
    }
    valid_.store(false, std::memory_order_release);
#else
    {
        std::lock_guard lock(state_mutex_);
        owner_thread_ = {};
        recursion_count_ = 0;
    }

    if (named_lock_fd_ >= 0) {
        ::close(named_lock_fd_);
        named_lock_fd_ = -1;
    }

    uses_named_process_lock_ = false;
    valid_.store(false, std::memory_order_release);
    state_cv_.notify_all();
#endif
}

bool SMutex::Release() {
#if defined(_WIN32)
    if (!IsValid()) {
        return false;
    }
    return ::ReleaseMutex(static_cast<HANDLE>(native_handle_)) != FALSE;
#else
    std::unique_lock lock(state_mutex_);
    if (!valid_.load(std::memory_order_acquire) || owner_thread_ != std::this_thread::get_id() ||
        recursion_count_ == 0) {
        return false;
    }

    --recursion_count_;
    if (recursion_count_ == 0) {
        owner_thread_ = {};
        if (uses_named_process_lock_) {
            ReleaseNamedProcessLock(named_lock_fd_);
        }
        lock.unlock();
        state_cv_.notify_one();
    }

    return true;
#endif
}

void SMutex::Lock() {
    (void)Wait(kInfiniteWait);
}

void SMutex::Unlock() {
    (void)Release();
}

SEventRWLock::SEventRWLock() {
    Init();
}

SEventRWLock::~SEventRWLock() {
    Destroy();
}

void SEventRWLock::Init() {
    {
        std::lock_guard lock(mutex_);
        if (initialized_) {
            return;
        }
    }

    AcquireEventTokenPools();

    std::lock_guard lock(mutex_);
    if (initialized_) {
        ReleaseEventTokenPools();
        return;
    }
    gate_locked_ = false;
    exclusive_owner_ = false;
    shared_owner_count_ = 0;
    exclusive_wait_token_ = 0;
    shared_wait_token_ = 0;
    initialized_ = true;
}

void SEventRWLock::Destroy() {
    std::uint32_t exclusive_wait_token = 0;
    std::uint32_t shared_wait_token = 0;

    {
        std::lock_guard lock(mutex_);
        if (!initialized_) {
            return;
        }

        gate_locked_ = false;
        exclusive_owner_ = false;
        shared_owner_count_ = 0;
        exclusive_wait_token = std::exchange(exclusive_wait_token_, 0u);
        shared_wait_token = std::exchange(shared_wait_token_, 0u);
        initialized_ = false;
    }

    cv_.notify_all();

    if (exclusive_wait_token != 0) {
        FreeEventToPool(exclusive_wait_token, 0, true);
    }
    if (shared_wait_token != 0) {
        FreeEventToPool(shared_wait_token, 1, true);
    }

    ReleaseEventTokenPools();
}

void SEventRWLock::Lock(bool exclusive) {
    std::unique_lock lock(mutex_);
    if (!initialized_) {
        lock.unlock();
        Init();
        lock.lock();
    }

    if (exclusive) {
        while (gate_locked_) {
            (void)AcquireWaitTokenLocked(exclusive_wait_token_, 0);
            cv_.wait(lock);
            if (!initialized_) {
                return;
            }
        }

        gate_locked_ = true;
        exclusive_owner_ = true;
        return;
    }

    while (exclusive_owner_) {
        (void)AcquireWaitTokenLocked(shared_wait_token_, 1);
        cv_.wait(lock);
        if (!initialized_) {
            return;
        }
    }

    if (shared_owner_count_ == 0) {
        while (gate_locked_) {
            (void)AcquireWaitTokenLocked(shared_wait_token_, 1);
            cv_.wait(lock);
            if (!initialized_) {
                return;
            }
        }
        gate_locked_ = true;
    }

    ++shared_owner_count_;
}

void SEventRWLock::Unlock(bool exclusive) {
    bool notify_waiters = false;

    {
        std::lock_guard lock(mutex_);
        if (!initialized_) {
            return;
        }

        if (exclusive) {
            if (!exclusive_owner_) {
                return;
            }
            exclusive_owner_ = false;
            gate_locked_ = false;
            notify_waiters = true;
        } else {
            if (shared_owner_count_ == 0) {
                return;
            }
            --shared_owner_count_;
            if (shared_owner_count_ == 0) {
                gate_locked_ = false;
                notify_waiters = true;
            }
        }
    }

    if (notify_waiters) {
        cv_.notify_all();
    }
}

uint32_t SThread_WaitForMultiple(uint32_t count, SEvent** events,
                                 bool wait_all, uint32_t milliseconds) {
    if (count == 0 || count > 64) return static_cast<uint32_t>(-1);

    uint32_t valid_count = 0;
    SEvent* valid_events[64] = {};
    for (uint32_t i = 0; i < count; ++i) {
        if (events[i] && events[i]->IsValid()) {
            valid_events[valid_count++] = events[i];
        }
    }

    if (valid_count == 0) return static_cast<uint32_t>(-1);

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(milliseconds);

    while (true) {
        uint32_t signaled_count = 0;
        for (uint32_t i = 0; i < valid_count; ++i) {
            if (valid_events[i]->Wait(0) == 0) {
                if (!wait_all) return i;
                ++signaled_count;
            }
        }
        if (wait_all && signaled_count == valid_count) return 0;

        if (milliseconds != 0xFFFFFFFF &&
            std::chrono::steady_clock::now() >= deadline) {
            return 0x102;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

StormThreadHandle::StormThreadHandle(std::shared_ptr<StormThreadBlock> block)
    : block_(std::move(block)) {}

void StormThreadHandle::Bind(std::shared_ptr<StormThreadBlock> block) {
    block_ = std::move(block);
}

uint32_t StormThreadHandle::Wait(const uint32_t milliseconds) const {
    if (!block_) {
        return kWaitFailed;
    }

    return block_->WaitForCompletion(milliseconds);
}

bool StormThreadHandle::SetPriority(const int level) const {
    if (!block_ || block_->handle == 0) {
        return false;
    }

    std::thread::native_handle_type native_handle{};
    static_assert(sizeof(native_handle) <= sizeof(block_->handle));
    std::memcpy(&native_handle, &block_->handle, sizeof(native_handle));
    return StormThread::SetPriorityFromLevel(native_handle, level);
}

std::uintptr_t StormThreadHandle::native_handle_value() const {
    return block_ ? block_->handle : 0;
}

std::string_view StormThreadHandle::name() const {
    return block_ ? std::string_view(block_->name) : std::string_view{};
}

void* SThread_CreateSimple(int (*proc)(void*), void* param,
                           uint32_t* out_thread_id, int ,
                           const char* thread_name, uint32_t stack_size) {
    if (!proc) {
        return nullptr;
    }

    auto* const handle = new (std::nothrow) StormThreadHandle();
    if (handle == nullptr) {
        return nullptr;
    }

    auto block = StormThread::Instance().Create(
        [proc](void* raw_param) -> int {
            return proc(raw_param);
        },
        param,
        thread_name != nullptr ? thread_name : "",
        StormThreadFlags::None,
        stack_size);
    if (!block || block->handle == 0) {
        delete handle;
        return nullptr;
    }

    if (out_thread_id) {
        *out_thread_id = block->threadId;
    }

    handle->Bind(std::move(block));
    return handle;
}

bool SThread_CreateAndStart(int (*proc)(void*), void* param,
                            void** out_handle, const char* thread_name,
                            const uint32_t stack_size) {
    if (!proc || !out_handle) {
        return false;
    }

    *out_handle = nullptr;

    auto* const handle = new (std::nothrow) StormThreadHandle();
    if (handle == nullptr) {
        return false;
    }

    auto block = StormThread::Instance().Create(
        [proc](void* raw_param) -> int {
            return proc(raw_param);
        },
        param,
        thread_name != nullptr ? thread_name : "",
        StormThreadFlags::None,
        stack_size);
    if (!block || block->handle == 0) {
        delete handle;
        return false;
    }

    handle->Bind(std::move(block));
    *out_handle = handle;
    return true;
}

bool SThread_SetPriority(void** handle_storage, int level) {
    if (level < 0 || level >= 6 || handle_storage == nullptr || *handle_storage == nullptr) {
        return false;
    }

    return static_cast<StormThreadHandle*>(*handle_storage)->SetPriority(level);
}

namespace {
std::once_flag g_crit_sect_init_flag;
SCritSect g_critical_sections[5];
}

void InitCriticalSections() {
    std::call_once(g_crit_sect_init_flag, [] {

    });
}

int32_t AllocEventFromPool(int type) {
    auto& pool = g_event_token_pools[type & 1];

    if (pool.available.fetch_sub(1, std::memory_order_acq_rel) < 0) {
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                           std::string("IAllocEvent: too many ") +
                               EventTokenPoolName(type & 1) + " event allocs");
    }

    const std::uint32_t index =
        (pool.alloc_index.fetch_add(1, std::memory_order_acq_rel) + 1) &
        kEventTokenMask;
    auto* const target = &pool.slots[index];

    std::uint32_t result = target->exchange(0, std::memory_order_acq_rel);
    if (result != 0) {
        return static_cast<std::int32_t>(result);
    }

    while (true) {
        int spins = g_event_token_spin_count.load(std::memory_order_relaxed);
        while (spins-- > 0) {
            result = target->exchange(0, std::memory_order_acq_rel);
            if (result != 0) {
                return static_cast<std::int32_t>(result);
            }
        }
        std::this_thread::yield();
    }
}

void FreeEventToPool(std::uint32_t token, int type, bool ) {
    if (token == 0) {
        return;
    }

    auto& pool = g_event_token_pools[type & 1];
    if (pool.available.fetch_add(1, std::memory_order_acq_rel) + 1 >
        static_cast<std::int32_t>(kInitialEventTokenCount)) {
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                           std::string("IFreeEvent: too many ") +
                               EventTokenPoolName(type & 1) + " event frees");
    }

    const std::uint32_t index =
        (pool.free_index.fetch_add(1, std::memory_order_acq_rel) + 1) &
        kEventTokenMask;
    auto* const target = &pool.slots[index];

    std::uint32_t displaced = target->exchange(token, std::memory_order_acq_rel);
    if (displaced == 0) {
        return;
    }

    while (true) {
        int spins = g_event_token_spin_count.load(std::memory_order_relaxed);
        while (spins-- > 0) {
            displaced = target->exchange(token, std::memory_order_acq_rel);
            if (displaced == 0) {
                return;
            }
        }
        std::this_thread::yield();
    }
}

}
