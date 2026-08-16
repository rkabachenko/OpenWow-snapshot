
#include "openwow/game/cmirror_compact_lookup.h"

namespace openwow::game {

std::uint32_t ObjectUpdate_GameObjectFieldLocalIndexToCompactIndex(
    std::uint32_t gameobject_relative_dword) noexcept {
  for (std::uint32_t i = 0; i < kGameObjectCompactSlotCount; ++i) {
    if (kGameObjectCompactTable[i] == gameobject_relative_dword) {
      return i;
    }
  }
  return kGameObjectCompactSlotSentinel;
}

std::uint32_t CMirrorHandler_LookupCorpseCompactSlotIndex(
    std::uint32_t corpse_relative_dword) noexcept {
  for (std::uint32_t i = 0; i < kCorpseCompactSlotCount; ++i) {
    if (kCorpseCompactTable[i] == corpse_relative_dword) {
      return i;
    }
  }
  return kCorpseCompactSlotSentinel;
}

}
