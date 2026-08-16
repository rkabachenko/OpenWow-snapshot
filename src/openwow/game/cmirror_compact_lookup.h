#pragma once

#include <cstdint>

namespace openwow::game {

inline constexpr std::uint32_t kGameObjectCompactSlotCount = 4;

inline constexpr std::uint32_t kGameObjectCompactSlotSentinel = 12;

inline constexpr std::uint32_t
    kGameObjectCompactTable[kGameObjectCompactSlotCount] = {
        3,
        8,
        11,
        12,
};

std::uint32_t ObjectUpdate_GameObjectFieldLocalIndexToCompactIndex(
    std::uint32_t gameobject_relative_dword) noexcept;

inline constexpr std::uint32_t kCorpseCompactSlotCount = 3;

inline constexpr std::uint32_t kCorpseCompactSlotSentinel = 30;

inline constexpr std::uint32_t kCorpseCompactTable[kCorpseCompactSlotCount] = {
    27,
    28,
    30,
};

std::uint32_t CMirrorHandler_LookupCorpseCompactSlotIndex(
    std::uint32_t corpse_relative_dword) noexcept;

}
