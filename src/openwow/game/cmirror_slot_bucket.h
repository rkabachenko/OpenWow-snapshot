#pragma once

#include "openwow/core/storm_intrusive_list.h"

#include <cstdint>
#include <vector>

namespace openwow::game {

using MirrorSlotBucket = core::StormIntrusiveListRootWords<std::uintptr_t>;

inline constexpr std::uint32_t kMirrorObjectSectionSlots     = 6;
inline constexpr std::uint32_t kMirrorItemSectionSlots       = 58;
inline constexpr std::uint32_t kMirrorContainerSectionSlots  = 74;
inline constexpr std::uint32_t kMirrorUnitSectionSlots       = 142;
inline constexpr std::uint32_t kMirrorPlayerVisibleSlots     = 176;
inline constexpr std::uint32_t kMirrorPlayerActiveSlots      = 1002;
inline constexpr std::uint32_t kMirrorGameObjectSectionSlots = 12;
inline constexpr std::uint32_t kMirrorDynObjSectionSlots     = 6;
inline constexpr std::uint32_t kMirrorCorpseSectionSlots     = 30;

void CMirrorHandler_ClearSlotBucketArray(
    std::uint32_t count,
    MirrorSlotBucket* buckets);

}
