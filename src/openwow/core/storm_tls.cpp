
#include "storm_tls.h"

#include "storm_memory.h"

#include <cstddef>
#include <unordered_map>

namespace openwow::core {

namespace {

struct ThreadTlsData {
    std::unordered_map<TlsSlotHandle, void*> values;
    bool                                      exitCleanupComplete = false;

    ~ThreadTlsData();
};

static_assert(alignof(ThreadTlsData) >= alignof(void*),
              "ThreadTlsData must be at least pointer-aligned.");
static_assert(alignof(std::unordered_map<TlsSlotHandle, void*>) >= alignof(void*),
              "TLS value map must be at least pointer-aligned.");
static_assert(alignof(bool) >= 1, "bool alignment is at least 1-byte.");

void RunThreadExitCleanup(ThreadTlsData& data);

thread_local ThreadTlsData t_tlsData;

ThreadTlsData::~ThreadTlsData() {
    RunThreadExitCleanup(*this);
}

}

StormTls& StormTls::Instance() {
    static StormTls* inst = new StormTls();
    return *inst;
}

void StormTls::InitMasterSlots() {
    std::lock_guard lock(mutex_);
    if (threadMemorySlot_ != kInvalidTlsSlot) {
        return;
    }

    threadMemorySlot_ = AllocateSlotLocked(nullptr);
    secondaryMasterSlot_ = AllocateSlotLocked(nullptr);
}

bool StormTls::MasterSlotsInitialized() const {
    std::lock_guard lock(mutex_);
    return threadMemorySlot_ != kInvalidTlsSlot && secondaryMasterSlot_ != kInvalidTlsSlot;
}

TlsSlotHandle StormTls::AllocSlot(TlsDestructor destructor) {
    StormMemory::Instance().Init();
    std::lock_guard lock(mutex_);
    try {
        return AllocateSlotLocked(std::move(destructor));
    } catch (...) {
        return kInvalidTlsSlot;
    }
}

bool StormTls::InitSlot(TlsSlotHandle& slot, TlsDestructor destructor) {
    StormMemory::Instance().Init();

    std::lock_guard lock(mutex_);
    if (slot != kInvalidTlsSlot) return true;
    try {
        slot = AllocateSlotLocked(std::move(destructor));
    } catch (...) {
        return false;
    }
    return slot != kInvalidTlsSlot;
}

void* StormTls::GetValue(TlsSlotHandle slot) const {
    if (!IsSlotActive(slot)) return nullptr;
    auto it = t_tlsData.values.find(slot);
    if (it == t_tlsData.values.end()) return nullptr;
    return it->second;
}

bool StormTls::IsSlotActive(TlsSlotHandle slot) const {
    if (slot == kInvalidTlsSlot) {
        return false;
    }

    std::lock_guard lock(mutex_);
    return slot <= slots_.size() && slots_[slot - 1].active;
}

bool StormTls::SetValue(TlsSlotHandle slot, void* value) {
    std::lock_guard lock(mutex_);
    if (slot == kInvalidTlsSlot || slot > slots_.size() || !slots_[slot - 1].active) {
        return false;
    }

    if (value == nullptr) {
        t_tlsData.values.erase(slot);
    } else {
        t_tlsData.values[slot] = value;
        t_tlsData.exitCleanupComplete = false;
    }

    return true;
}

void StormTls::CallDestructors() {
    std::lock_guard lock(mutex_);

    for (TlsSlotHandle slot = 1; slot < nextSlot_; ++slot) {
        const auto& info = slots_[slot - 1];
        if (!info.active || !info.destructor) {
            continue;
        }

        auto it = t_tlsData.values.find(slot);
        if (it != t_tlsData.values.end() && it->second != nullptr) {
            info.destructor(it->second);
        }
    }

    t_tlsData.exitCleanupComplete = true;
}

void StormTls::FreeSlot(TlsSlotHandle slot) {
    if (slot == kInvalidTlsSlot) return;
    std::lock_guard lock(mutex_);
    if (slot > 0 && slot <= slots_.size()) {
        slots_[slot - 1].active = false;
        slots_[slot - 1].destructor = nullptr;
        t_tlsData.values.erase(slot);
    }
}

void* StormTls::GetThreadMemory() const {
    if (!MasterSlotsInitialized()) {
        StormMemory::Instance().Init();
    }

    return GetValue(threadMemorySlot_);
}

void StormTls::SetThreadMemory(void* mem) {
    InitMasterSlots();
    (void)SetValue(threadMemorySlot_, mem);
}

TlsSlotHandle StormTls::GetThreadMemorySlot() const {
    std::lock_guard lock(mutex_);
    return threadMemorySlot_;
}

TlsSlotHandle StormTls::GetSecondaryMasterSlot() const {
    std::lock_guard lock(mutex_);
    return secondaryMasterSlot_;
}

TlsSlotHandle StormTls::AllocateSlotLocked(TlsDestructor destructor) {
    const TlsSlotHandle handle = nextSlot_++;
    if (handle >= slots_.size() + 1) {
        slots_.resize(handle);
    }

    auto& info = slots_[handle - 1];
    info.active = true;
    info.destructor = std::move(destructor);
    return handle;
}

namespace {

void RunThreadExitCleanup(ThreadTlsData& data) {
    if (data.exitCleanupComplete) {
        return;
    }

    StormTls::Instance().CallDestructors();
}

}

}
