#pragma once

#include "openwow/game/inventory/items/item_on_use_spell.h"

namespace openwow::game {

template <typename ItemTemplateT>
inline bool ItemPassesArenaUseRestrictions(const ItemTemplateT& item) {
  if ((item.flags & 0x00200000u) != 0u) {
    return true;
  }
  if (item.bonding == 4u || (item.flags & 0x04000000u) != 0u) {
    return false;
  }
  const auto spell = ResolveFirstOnUseSpellArenaData(item);
  return !spell.has_value() ||
         (spell->charges >= 0 && spell->cooldown_ms <= 900000 &&
          spell->category_cooldown_ms <= 900000);
}

inline bool IsArenaMapType(const std::uint32_t map_type) {
  return map_type == 4u;
}

}
