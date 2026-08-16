#pragma once

#include "openwow/game/spell_cast_runtime.h"

#include <cstdint>
#include <optional>

namespace openwow::game::inventory::ui {

enum class ItemSpellEffect : std::uint32_t {
  kEnchantPermanent = 53,
  kEnchantTemporary = 54,
  kProspecting = 127,
  kEnchantPrismatic = 156,
  kMilling = 158,
};

enum class ItemTemplateProcessingFlag : std::uint32_t {
  kMillable = 0x00040000u,
  kProspectable = 0x20000000u,
};

struct ItemProcessingRule {
  ItemTemplateProcessingFlag required_flag;
  SpellCastResult failure;
};

[[nodiscard]] constexpr std::optional<ItemProcessingRule>
GetItemProcessingRule(const ItemSpellEffect effect) {
  switch (effect) {
  case ItemSpellEffect::kProspecting:
    return ItemProcessingRule{
        .required_flag = ItemTemplateProcessingFlag::kProspectable,
        .failure = SpellCastResult::kCantBeProspected,
    };
  case ItemSpellEffect::kMilling:
    return ItemProcessingRule{
        .required_flag = ItemTemplateProcessingFlag::kMillable,
        .failure = SpellCastResult::kCantBeMilled,
    };
  default:
    return std::nullopt;
  }
}

}
