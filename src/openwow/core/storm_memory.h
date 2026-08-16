
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace openwow::core {

inline constexpr size_t kStormArenaSize = 300000;

inline constexpr size_t kStormArenaTotalBytes = 300004;

class StormMemory {
public:
    static StormMemory& Instance();

    void Init();

    bool IsInitialized() const;

    void* GetThreadArena();

    void* ArenaAlloc(size_t size);

    void ArenaFree(void* ptr);

    void ResetThreadArena();

    size_t GetArenaUsedBytes() const;
    size_t GetArenaRemainingBytes() const;

private:
    StormMemory() = default;

    std::once_flag       initOnce_;
    std::atomic<bool>    initialized_{false};
};

}
