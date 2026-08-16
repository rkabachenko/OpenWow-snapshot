
#include "storm_region.h"

#include "storm_error.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace openwow::core {

namespace {

constexpr float kSentinel = std::numeric_limits<float>::max();

bool RectsOverlap(const StormRect& a, const StormRect& b) {
    return a.right > b.left && a.bottom > b.top &&
           a.left < b.right && a.top < b.bottom;
}

void InvalidateEntry(RegionEntry& e) {
    e.rect = {kSentinel, kSentinel, kSentinel, kSentinel};
    e.flags = 0;
    e.sequence = 0xFFFFFFFFu;
    e.extra = 0;
}

}

void RectRegion_SubdivideAndInsert(
    std::vector<RegionEntry>& entries,
    uint32_t start_idx,
    uint32_t end_idx,
    bool overlap_found,
    const StormRect& new_rect,
    uint32_t flags,
    uint32_t sequence)
{
    uint32_t idx = start_idx;

    while (idx < end_idx) {
        const auto& entry = entries[idx];

        if (entry.rect.right <= new_rect.left ||
            entry.rect.bottom <= new_rect.top ||
            entry.rect.left >= new_rect.right ||
            entry.rect.top >= new_rect.bottom) {
            ++idx;
            continue;
        }

        if (entry.rect.left == new_rect.left &&
            entry.rect.top == new_rect.top &&
            entry.rect.right == new_rect.right &&
            entry.rect.bottom == new_rect.bottom) {

            entries[idx].extra |= 2u;
            overlap_found = true;
            ++idx;
            continue;
        }

        break;
    }

    if (idx >= end_idx) {
        RegionEntry new_entry;
        new_entry.rect = new_rect;
        new_entry.flags = flags;
        new_entry.sequence = sequence;
        new_entry.extra = (overlap_found ? 2u : 0u) | 1u;
        entries.push_back(new_entry);
        return;
    }

    const StormRect& A = new_rect;
    const StormRect& B = entries[idx].rect;

    const uint32_t saved_flags = entries[idx].flags;
    const uint32_t saved_sequence = entries[idx].sequence;
    const uint32_t saved_extra = entries[idx].extra;

    const StormRect* rects[2] = {&A, &B};

    const bool bl_lt_al = B.left < A.left;
    const bool al_lt_bl = A.left < B.left;
    const bool bt_lt_at = B.top < A.top;
    const bool at_lt_bt = A.top < B.top;
    const bool br_lt_ar = B.right < A.right;
    const bool ar_lt_br = A.right < B.right;
    const bool bb_lt_ab = B.bottom < A.bottom;
    const bool ab_lt_bb = A.bottom < B.bottom;

    const StormRect* minTopR = rects[bt_lt_at];
    const StormRect* maxTopR = rects[at_lt_bt];
    const StormRect* minBotR = rects[bb_lt_ab];
    const StormRect* maxBotR = rects[ab_lt_bb];
    const StormRect* minLeftR = rects[bl_lt_al];
    const StormRect* maxLeftR = rects[al_lt_bl];
    const StormRect* minRightR = rects[br_lt_ar];
    const StormRect* maxRightR = rects[ar_lt_br];

    StormRect sub_rects[5] = {

        {minTopR->left, minTopR->top, minTopR->right, maxTopR->top},

        {maxBotR->left, minBotR->bottom, maxBotR->right, maxBotR->bottom},

        {minLeftR->left, maxTopR->top, maxLeftR->left, minBotR->bottom},

        {minRightR->right, maxTopR->top, maxRightR->right, minBotR->bottom},

        {maxLeftR->left, maxTopR->top, minRightR->right, minBotR->bottom},
    };

    bool new_overlaps[5];
    bool existing_overlaps[5];

    for (int i = 0; i < 5; ++i) {
        if (sub_rects[i].right > sub_rects[i].left &&
            sub_rects[i].bottom > sub_rects[i].top) {
            new_overlaps[i] = RectsOverlap(A, sub_rects[i]);
            existing_overlaps[i] = RectsOverlap(B, sub_rects[i]);
        } else {
            new_overlaps[i] = false;
            existing_overlaps[i] = false;
        }
    }

    for (int i = 0; i < 5; ++i) {

        if (new_overlaps[i]) {
            bool combined = overlap_found || existing_overlaps[i];
            RectRegion_SubdivideAndInsert(
                entries, idx + 1, end_idx, combined,
                sub_rects[i], flags, sequence);
        }

        if (existing_overlaps[i]) {
            uint32_t frag_extra =
                (saved_extra & ~3u) | (new_overlaps[i] ? 2u : 0u);

            RegionEntry frag;
            frag.rect = sub_rects[i];
            frag.flags = saved_flags;
            frag.sequence = saved_sequence;
            frag.extra = frag_extra;
            entries.push_back(frag);
        }
    }

    InvalidateEntry(entries[idx]);
}

void RectRegion_FilterEntriesByCombineMode(
    int combine_mode,
    std::vector<RegionEntry>& entries)
{
    const int mode = combine_mode - 1;

    for (auto& entry : entries) {
        bool should_invalidate = false;

        switch (mode) {
            case 0:
                should_invalidate = (entry.extra & 2u) == 0;
                break;
            case 2:
                should_invalidate = (entry.extra & 2u) != 0;
                break;
            case 3:
                should_invalidate = (entry.extra & 3u) != 0;
                break;
            case 4:
                should_invalidate = (entry.extra & 1u) != 0;
                break;
            default:
                break;
        }

        if (should_invalidate) {
            InvalidateEntry(entry);
        }

        entry.extra = 0;
    }
}

void RectRegion_CompactInvalidEntries(std::vector<RegionEntry>& entries) {
    entries.erase(
        std::remove_if(entries.begin(), entries.end(),
            [](const RegionEntry& e) {
                return e.rect.right <= e.rect.left ||
                       e.rect.bottom <= e.rect.top;
            }),
        entries.end());
}

RegionSystem& RegionSystem::Instance() {
    static RegionSystem inst;
    return inst;
}

RegionHandle* RegionSystem::FindHandle(uint32_t handle) {
    auto it = handles_.find(handle);
    return (it != handles_.end()) ? &it->second : nullptr;
}

bool RegionSystem::CreateHandle(uint32_t* out_handle, const int reserved) {
    if (!out_handle) {
        SErrSetLastError(87);
        return false;
    }
    *out_handle = 0;
    if (reserved != 0) {
        SErrSetLastError(87);
        return false;
    }

    std::lock_guard lock(mutex_);
    do {
        ++next_handle_;
    } while (next_handle_ == 0 || handles_.contains(next_handle_));

    RegionHandle handle;
    handle.handle_id = next_handle_;
    handle.cached_bounds = {kSentinel, kSentinel, kSentinel, kSentinel};
    handles_.emplace(next_handle_, std::move(handle));
    *out_handle = next_handle_;
    return true;
}

void RegionSystem::UpdateHandle(uint32_t handle, const StormRect* rect,
                                 int flags, int combine_mode) {
    if (handle == 0 || !rect || combine_mode < 1 || combine_mode > 6) {
        SErrSetLastError(87);
        return;
    }

    std::lock_guard lock(mutex_);

    RegionHandle* const found = FindHandle(handle);
    if (!found) {
        return;
    }
    RegionHandle& rh = *found;
    ++rh.sequence;

    if (combine_mode == 2 || combine_mode == 6) {

        if (rect->right > rect->left && rect->bottom > rect->top) {
            RegionEntry entry;
            entry.rect = *rect;
            entry.flags = static_cast<uint32_t>(flags);
            entry.sequence = rh.sequence;
            entry.extra = (combine_mode == 6) ? 0x10000u : 0u;
            rh.entries.push_back(entry);
        }
    } else {
        if (!rect->IsEmpty()) {
            uint32_t old_count = static_cast<uint32_t>(rh.entries.size());
            RectRegion_SubdivideAndInsert(
                rh.entries, 0, old_count, false,
                *rect, static_cast<uint32_t>(flags), rh.sequence);
        }
        RectRegion_FilterEntriesByCombineMode(combine_mode, rh.entries);
        RectRegion_CompactInvalidEntries(rh.entries);
    }

    rh.dirty = true;
    rh.cached_bounds = {
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max()
    };
}

void RegionSystem::GetBounds(uint32_t handle, StormRect* out_rect) {
    if (handle == 0 || !out_rect) {
        SErrSetLastError(87);
        return;
    }

    out_rect->left = std::numeric_limits<float>::max();
    out_rect->top = std::numeric_limits<float>::max();

    out_rect->right = std::numeric_limits<float>::min();
    out_rect->bottom = std::numeric_limits<float>::min();

    std::lock_guard lock(mutex_);

    auto* rh = FindHandle(handle);
    if (!rh) return;

    for (const auto& entry : rh->entries) {
        if (entry.extra & 0x10000) continue;

        out_rect->left = std::min(out_rect->left, entry.rect.left);
        out_rect->top = std::min(out_rect->top, entry.rect.top);
        out_rect->right = std::max(out_rect->right, entry.rect.right);
        out_rect->bottom = std::max(out_rect->bottom, entry.rect.bottom);
    }

    if (out_rect->left >= out_rect->right ||
        out_rect->top >= out_rect->bottom) {
        *out_rect = {0.0f, 0.0f, 0.0f, 0.0f};
    }
}

void RegionSystem::DestroyHandle(uint32_t handle) {
    if (handle == 0) {
        SErrSetLastError(87);
        return;
    }

    std::lock_guard lock(mutex_);
    handles_.erase(handle);
}

void RegionSystem::ClearAll() {
    std::lock_guard lock(mutex_);
    handles_.clear();
    next_handle_ = 0;
}

SourceArray::~SourceArray() {
    std::free(data_);
    data_ = nullptr;
}

void SourceArray::Resize(uint32_t new_count) {
    if (new_count > std::numeric_limits<uint32_t>::max() / kElementSize) {
        return;
    }
    if (new_count == 0) {
        std::free(data_);
        data_ = nullptr;
        capacity_ = 0;
        count_ = 0;
        return;
    }

    const uint32_t new_size = new_count * kElementSize;
    uint8_t* old_data = data_;

    uint8_t* new_data = static_cast<uint8_t*>(
        std::realloc(data_, new_size));
    if (new_data) {
        data_ = new_data;
    } else {

        new_data = static_cast<uint8_t*>(std::malloc(new_size));
        if (!new_data) {
            return;
        }
        if (old_data) {
            const uint32_t copy_count = std::min(new_count, count_);
            std::memcpy(new_data, old_data, copy_count * kElementSize);
        }
        std::free(old_data);
        data_ = new_data;
    }

    capacity_ = new_count;
    count_ = new_count;
}

}
