
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>

namespace openwow::core {

struct StormThreadBlock;

class SCritSect {
public:
    SCritSect() = default;

    ~SCritSect() = default;

    SCritSect(const SCritSect&) = delete;
    SCritSect& operator=(const SCritSect&) = delete;

    void Enter();

    void Leave();

    std::recursive_mutex& native_handle() { return mutex_; }

private:
    std::recursive_mutex mutex_;
};

class SEvent {
public:

    SEvent(bool manual_reset, bool initial_state);
    ~SEvent() = default;

    SEvent(const SEvent&) = delete;
    SEvent& operator=(const SEvent&) = delete;

    void Set();

    void Reset();

    uint32_t Wait(uint32_t milliseconds);

    [[nodiscard]] bool IsSignaled() const;

    void Destroy();

    bool IsValid() const { return valid_; }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool signaled_ = false;
    bool manual_reset_ = true;
    bool valid_ = true;
};

class SMutex {
public:
    SMutex();
    ~SMutex();

    SMutex(const SMutex&) = delete;
    SMutex& operator=(const SMutex&) = delete;

    void Create(bool initial_owner, const char* name);

    std::uint32_t Wait(std::uint32_t milliseconds);

    void Destroy();

    [[nodiscard]] bool IsValid() const { return valid_.load(); }

    bool Release();

    void Lock();
    void Unlock();

private:
    std::atomic_bool valid_{false};
#if defined(_WIN32)
    void* native_handle_ = nullptr;
#else
    mutable std::mutex state_mutex_;
    std::condition_variable state_cv_;
    std::thread::id owner_thread_{};
    std::uint32_t recursion_count_ = 0;
    int named_lock_fd_ = -1;
    bool uses_named_process_lock_ = false;
#endif
};

class SEventRWLock {
public:
    SEventRWLock();
    ~SEventRWLock();

    SEventRWLock(const SEventRWLock&) = delete;
    SEventRWLock& operator=(const SEventRWLock&) = delete;

    void Init();

    void Destroy();

    void Lock(bool exclusive);
    void Unlock(bool exclusive);

    void LockShared() { Lock(false); }
    void UnlockShared() { Unlock(false); }
    void LockExclusive() { Lock(true); }
    void UnlockExclusive() { Unlock(true); }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool gate_locked_ = false;
    bool exclusive_owner_ = false;
    std::uint32_t shared_owner_count_ = 0;
    std::uint32_t exclusive_wait_token_ = 0;
    std::uint32_t shared_wait_token_ = 0;
    bool initialized_ = false;
};

uint32_t SThread_WaitForMultiple(uint32_t count, SEvent** events,
                                  bool wait_all, uint32_t milliseconds);

class StormThreadHandle {
public:
    StormThreadHandle() = default;
    explicit StormThreadHandle(std::shared_ptr<StormThreadBlock> block);

    void Bind(std::shared_ptr<StormThreadBlock> block);

    [[nodiscard]] uint32_t Wait(uint32_t milliseconds) const;
    [[nodiscard]] bool SetPriority(int level) const;
    [[nodiscard]] std::uintptr_t native_handle_value() const;
    [[nodiscard]] std::string_view name() const;

private:
    std::shared_ptr<StormThreadBlock> block_;
};

void* SThread_CreateSimple(int (*proc)(void*), void* param,
                           uint32_t* out_thread_id, int reserved,
                           const char* thread_name, uint32_t stack_size);

bool SThread_CreateAndStart(int (*proc)(void*), void* param,
                            void** out_handle, const char* thread_name = nullptr,
                            uint32_t stack_size = 0);

bool SThread_SetPriority(void** handle_storage, int level);

void InitCriticalSections();

int32_t AllocEventFromPool(int type);

void FreeEventToPool(std::uint32_t token, int type, bool reset_wait_state);

}
