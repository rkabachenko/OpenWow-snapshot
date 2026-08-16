
#include "storm_memory.h"

#include "storm_tls.h"
#include "storm_thread.h"

#include <cstdlib>
#include <memory>
#include <new>

namespace openwow::core {

namespace {

size_t AlignArenaAllocation(size_t size) {
    return (size + 3u) & ~size_t(3u);
}

struct ArenaBlock {
    ArenaBlock() = default;

    [[nodiscard]] char* Base() {
        return reinterpret_cast<char*>(data);
    }

    [[nodiscard]] char* Current() {
        return Base() + usedBytes;
    }

    [[nodiscard]] size_t RemainingBytes() const {
        return kStormArenaSize - usedBytes;
    }

    [[nodiscard]] bool Contains(const void* ptr) const {
        const auto address = reinterpret_cast<std::uintptr_t>(ptr);
        const auto start = reinterpret_cast<std::uintptr_t>(data);
        return address >= start && address < (start + kStormArenaSize);
    }

    void Advance(size_t bytes) {
        usedBytes += bytes;
    }

    void Reset() {
        usedBytes = 0;
    }

    alignas(16) uint8_t data[kStormArenaSize] = {};
    size_t              usedBytes = 0;
};

thread_local std::unique_ptr<ArenaBlock> t_arena;

ArenaBlock* EnsureArena() {
    if (!t_arena) {
        auto arena = std::unique_ptr<ArenaBlock>(new (std::nothrow) ArenaBlock());
        if (arena) {
            t_arena = std::move(arena);
        }
    }
    return t_arena.get();
}

}

StormMemory& StormMemory::Instance() {
    static StormMemory inst;
    return inst;
}

void StormMemory::Init() {
    std::call_once(initOnce_, [this]() {
        StormTls::Instance().InitMasterSlots();

        StormThread::Instance().EnsurePrimaryThreadBlock();
        initialized_.store(true, std::memory_order_release);
    });
}

bool StormMemory::IsInitialized() const {
    return initialized_.load(std::memory_order_acquire);
}

void* StormMemory::GetThreadArena() {
    ArenaBlock* arena = EnsureArena();
    return arena ? arena->Base() : nullptr;
}

void* StormMemory::ArenaAlloc(size_t size) {
    const size_t aligned = AlignArenaAllocation(size);
    ArenaBlock* arena = EnsureArena();
    if (!arena) return nullptr;

    char* result = arena->Current();

    if (aligned >= arena->RemainingBytes()) {
        return std::malloc(aligned);
    }

    arena->Advance(aligned);
    return result;
}

void StormMemory::ArenaFree(void* ptr) {

    ArenaBlock* arena = EnsureArena();
    if (!arena) {

        if (reinterpret_cast<std::uintptr_t>(ptr) >= kStormArenaSize) {
            std::free(ptr);
        }
        return;
    }

    if (!arena->Contains(ptr)) {

        std::free(ptr);
    }

}

void StormMemory::ResetThreadArena() {
    if (t_arena) {
        t_arena->Reset();
    }
}

size_t StormMemory::GetArenaUsedBytes() const {
    if (!t_arena) return 0;
    return t_arena->usedBytes;
}

size_t StormMemory::GetArenaRemainingBytes() const {
    if (!t_arena) return kStormArenaSize;
    return t_arena->RemainingBytes();
}

}
