
#pragma once

#include "storm_sync.h"

#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace openwow::core {

struct StormRect {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;

    bool IsEmpty() const {
        return left >= right || top >= bottom;
    }
};

struct RegionEntry {
    StormRect rect;
    uint32_t flags = 0;
    uint32_t sequence = 0;
    uint32_t extra = 0;
};

static_assert(sizeof(RegionEntry) == 28);

struct RegionHandle {
    uint32_t handle_id = 0;
    uint32_t entry_count = 0;
    std::vector<RegionEntry> entries;
    uint32_t sequence = 0;
    bool dirty = false;
    StormRect cached_bounds = {3.4028235e38f, 3.4028235e38f,
                               1.1754944e-38f, 1.1754944e-38f};
};

void RectRegion_SubdivideAndInsert(
    std::vector<RegionEntry>& entries,
    uint32_t start_idx,
    uint32_t end_idx,
    bool overlap_found,
    const StormRect& new_rect,
    uint32_t flags,
    uint32_t sequence);

void RectRegion_FilterEntriesByCombineMode(
    int combine_mode,
    std::vector<RegionEntry>& entries);

void RectRegion_CompactInvalidEntries(std::vector<RegionEntry>& entries);

class RegionSystem {
public:
    static RegionSystem& Instance();

    bool CreateHandle(uint32_t* out_handle, int reserved = 0);

    void UpdateHandle(uint32_t handle, const StormRect* rect,
                      int flags, int combine_mode);

    void GetBounds(uint32_t handle, StormRect* out_rect);

    void DestroyHandle(uint32_t handle);

    void ClearAll();

    RegionHandle* FindHandle(uint32_t handle);

private:
    RegionSystem() = default;

    std::recursive_mutex mutex_;
    std::unordered_map<uint32_t, RegionHandle> handles_;
    uint32_t next_handle_ = 0;
};

class SourceArray {
public:
    void Resize(uint32_t new_count);

    uint32_t count() const { return count_; }
    void* data() const { return data_; }

    ~SourceArray();

private:
    uint32_t capacity_ = 0;
    uint32_t count_ = 0;
    uint8_t* data_ = nullptr;
    static constexpr uint32_t kElementSize = 28;
};

}
