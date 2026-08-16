
#include "openwow/core/rcstring.h"

#include "openwow/core/storm_error.h"
#include "openwow/core/storm_string.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <unordered_map>
#include <vector>

namespace openwow::core {
namespace {

constexpr std::size_t kRCStringCompareLimit = 0x7FFFFFFFu;

class RCStringBlockManager {
public:
    ~RCStringBlockManager() {
        Clear();
    }

    void* AllocateBlock(uint32_t requested_size) {
        uint32_t block_size = requested_size;
        if (block_size < kRCStringMinBlockSize) {
            block_size = kRCStringMinBlockSize;
        }

        const uint32_t previous_count = block_count_;
        const uint32_t new_count = previous_count + 1;
        if (new_count > previous_count && new_count > block_capacity_) {
            uint32_t growth_step = growth_step_;
            if (growth_step == 0) {
                growth_step = ResolveAutoGrowQuantum(new_count);
            }

            uint32_t new_capacity = new_count;
            if (const uint32_t remainder = new_capacity % growth_step; remainder != 0) {
                new_capacity += growth_step - remainder;
            }

            blocks_.reserve(new_capacity);
            block_capacity_ = new_capacity;
        }

        if (blocks_.size() < new_count) {
            blocks_.resize(new_count, nullptr);
        }

        void* const block = SMemAlloc(block_size, ".\\RCString.cpp", 0x26, 0);
        blocks_[previous_count] = block;
        block_count_ = new_count;
        current_ = static_cast<std::byte*>(block);
        end_ = current_ != nullptr ? current_ + block_size : nullptr;
        return block;
    }

    void Clear() {
        for (void* block : blocks_) {
            if (block != nullptr) {
                (void)SMemFree(block, ".\\RCString.cpp", 0x1C, 0);
            }
        }

        blocks_.clear();
        block_capacity_ = 0;
        block_count_ = 0;
        growth_step_ = 0;
        current_ = nullptr;
        end_ = nullptr;
    }

    char* AllocateEntryStorage(const char* str) {
        const uint32_t entry_size = ComputeEntrySize(str);
        if (current_ == nullptr || current_ + entry_size > end_) {
            AllocateBlock(entry_size);
        }

        if (current_ == nullptr || current_ + entry_size > end_) {
            return nullptr;
        }

        std::byte* const entry_base = current_;
        current_ += entry_size;

        char* const value = reinterpret_cast<char*>(entry_base + 4);
        (void)SStrCopy(value, str, kRCStringCompareLimit);
        return value;
    }

private:
    uint32_t ResolveAutoGrowQuantum(const uint32_t requested_count) {
        if (requested_count >= 0x40) {
            growth_step_ = 0x40;
            return 0x40;
        }

        uint32_t quantum = requested_count;
        for (uint32_t value = requested_count & (requested_count - 1); value != 0;
             value &= (value - 1)) {
            quantum = value;
        }

        return quantum == 0 ? 1u : quantum;
    }

    static uint32_t ComputeEntrySize(const char* str) {
        const std::size_t string_length = SStrLen(str);
        const std::size_t raw_size = (string_length + 8u) & ~std::size_t{3u};
        if (raw_size > std::numeric_limits<uint32_t>::max()) {
            return 0;
        }

        return static_cast<uint32_t>(raw_size);
    }

    std::vector<void*> blocks_{};
    uint32_t block_capacity_{0};
    uint32_t block_count_{0};
    uint32_t growth_step_{0};
    std::byte* current_{nullptr};
    std::byte* end_{nullptr};
};

class RCStringPoolState {
public:
    char* LookupOrInsert(const char* str) {
        const uint32_t bucket = SStrHashCI(str) % kRCStringHashBuckets;
        for (Entry* entry = buckets_[bucket]; entry != nullptr; entry = entry->next) {
            if (SStrCmpI(str, entry->value, kRCStringCompareLimit) == 0) {
                return entry->value;
            }
        }

        char* const value = block_manager_.AllocateEntryStorage(str);
        if (value == nullptr) {
            return nullptr;
        }

        auto entry = std::make_unique<Entry>();
        Entry* const inserted = entry.get();
        inserted->value = value;
        inserted->next = buckets_[bucket];
        buckets_[bucket] = inserted;
        entries_.push_back(std::move(entry));
        return value;
    }

    void* AllocateBlock(const uint32_t requested_size) {
        return block_manager_.AllocateBlock(requested_size);
    }

    void Reset() {
        entries_.clear();
        buckets_.fill(nullptr);
        block_manager_.Clear();
    }

    void Clear() {
        Reset();
    }

private:
    struct Entry {
        Entry* next{nullptr};
        char* value{nullptr};
    };

    std::array<Entry*, kRCStringHashBuckets> buckets_{};
    std::vector<std::unique_ptr<Entry>> entries_{};
    RCStringBlockManager block_manager_{};
};

using RCStringPoolRegistry = std::unordered_map<void*, std::unique_ptr<RCStringPoolState>>;

RCStringPoolRegistry& GetRCStringPoolRegistry() {
    static RCStringPoolRegistry registry;
    return registry;
}

void*& GetGlobalRCStringPoolBase() {
    static void* global_pool = nullptr;
    return global_pool;
}

void DestroyRCStringPoolState(void* pool) {
    if (!pool) {
        return;
    }

    auto& registry = GetRCStringPoolRegistry();
    registry.erase(pool);
}

RCStringPoolState* FindRCStringPoolState(void* pool) {
    auto& registry = GetRCStringPoolRegistry();
    auto  it = registry.find(pool);
    return it == registry.end() ? nullptr : it->second.get();
}

RCStringPoolState& EnsureRCStringPoolState(void* pool) {
    auto& state = GetRCStringPoolRegistry()[pool];
    if (!state) {
        state = std::make_unique<RCStringPoolState>();
    }
    return *state;
}

void* ResolvePoolBaseFromBlockManager(void* block_manager) {
    if (!block_manager) {
        return nullptr;
    }

    return reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(block_manager) - kRCStringBlockManagerOffset);
}

void* ResolveBlockManagerFromPoolBase(void* pool) {
    if (!pool) {
        return nullptr;
    }

    return reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(pool) + kRCStringBlockManagerOffset);
}

char** ResolveRCStringValueSlot(void* rcstring_this) {
    return reinterpret_cast<char**>(static_cast<std::byte*>(rcstring_this) +
                                    kRCStringValueOffset);
}

}

void RCString_FreeAll(void* block_manager) {
    if (!block_manager) {
        block_manager = ResolveBlockManagerFromPoolBase(GetGlobalRCStringPoolBase());
    }
    if (!block_manager) {
        return;
    }

    void* const pool_base = ResolvePoolBaseFromBlockManager(block_manager);
    if (!pool_base) {
        return;
    }
    if (RCStringPoolState* state = FindRCStringPoolState(pool_base)) {
        state->Clear();
    }
    DestroyRCStringPoolState(pool_base);
    std::memset(pool_base, 0, kRCStringPoolSize);
    if (GetGlobalRCStringPoolBase() == pool_base) {
        GetGlobalRCStringPoolBase() = nullptr;
    }
}

void* RCString_AllocBlock(void* block_manager, uint32_t size) {
    if (!block_manager) {
        return nullptr;
    }

    void* const pool_base = ResolvePoolBaseFromBlockManager(block_manager);
    RCStringPoolState& state = EnsureRCStringPoolState(pool_base);
    return state.AllocateBlock(size);
}

void* RCString_Lookup(void* pool, const char* str) {
    if (!pool || !str) {
        return nullptr;
    }

    RCStringPoolState* state = FindRCStringPoolState(pool);
    if (!state) {
        return nullptr;
    }

    return state->LookupOrInsert(str);
}

void* RCString_PoolInit(void* pool) {
    if (!pool) {
        return nullptr;
    }

    DestroyRCStringPoolState(pool);
    std::memset(pool, 0, kRCStringPoolSize);
    RCStringPoolState& state = EnsureRCStringPoolState(pool);
    state.Reset();
    (void)RCString_AllocBlock(ResolveBlockManagerFromPoolBase(pool), 0);
    return pool;
}

void RCString_Set(void* rcstring_this, const char* str) {
    if (!rcstring_this) {
        return;
    }

    char** const value_slot = ResolveRCStringValueSlot(rcstring_this);
    if (!str) {
        *value_slot = nullptr;
        return;
    }

    void*& global_pool_base = GetGlobalRCStringPoolBase();
    if (!global_pool_base) {
        void* pool_storage = SMemAlloc(kRCStringPoolSize, ".\\RCString.cpp", 65, 0);
        if (!pool_storage) {
            SErrSetLastError(87);
            return;
        }
        pool_storage = RCString_PoolInit(pool_storage);
        if (!pool_storage) {
            SErrSetLastError(87);
            return;
        }
        global_pool_base = pool_storage;
    }

    *value_slot = static_cast<char*>(RCString_Lookup(global_pool_base, str));
}

}
