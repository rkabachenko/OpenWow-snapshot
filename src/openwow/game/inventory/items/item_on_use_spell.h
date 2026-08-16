#pragma once

#include "openwow/data/formats/dbc/dbc_entries_gameplay.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace openwow::game {

constexpr std::uint32_t kEnchantmentTypeUseSpell = 7u;

struct ItemOnUseSpellArenaData {
  std::int32_t charges = 0;
  std::int32_t cooldown_ms = 0;
  std::int32_t category_cooldown_ms = 0;
};

struct ItemOnUseSpellEnchantmentMatch {
  std::size_t enchantment_slot = 0;
  std::uint32_t enchantment_id = 0;
  std::size_t effect_index = 0;
  const data::dbc::SpellItemEnchantmentEntry* enchantment = nullptr;

  [[nodiscard]] std::uint32_t spell_id() const {
    return enchantment != nullptr ? enchantment->spell_id[effect_index] : 0u;
  }
};

inline std::optional<std::size_t> FindOnUseSpellEnchantmentEffectIndex(
    const data::dbc::SpellItemEnchantmentEntry& enchantment) {
  for (std::size_t index = 0; index < enchantment.type.size(); ++index) {
    if (enchantment.type[index] == kEnchantmentTypeUseSpell &&
        enchantment.spell_id[index] != 0) {
      return index;
    }
  }
  return std::nullopt;
}

template <typename ItemInstanceT, typename ItemMetadataT>
inline bool ItemPassesEquippedWeaponSpellCheck(
    const ItemInstanceT& item, const ItemMetadataT& item_metadata) {
  if (item_metadata.item_class != 2u) {
    return false;
  }
  return item.max_durability == 0 || item.durability != 0;
}

template <typename ItemTemplateT>
inline std::ptrdiff_t FindFirstOnUseSpellIndex(const ItemTemplateT& item) {
  for (std::size_t index = 0; index < item.spells.size(); ++index) {
    const auto& spell = item.spells[index];
    if (spell.spell_id != 0 && spell.trigger == 0) {
      return static_cast<std::ptrdiff_t>(index);
    }
  }
  return -1;
}

template <typename ItemTemplateT>
inline const auto* FindFirstOnUseSpell(const ItemTemplateT& item) {
  const auto index = FindFirstOnUseSpellIndex(item);
  return index < 0 ? static_cast<decltype(&item.spells[0])>(nullptr)
                   : &item.spells[static_cast<std::size_t>(index)];
}

template <typename ItemInstanceT, typename LookupFn>
inline std::optional<ItemOnUseSpellEnchantmentMatch>
FindFirstOnUseSpellEnchantment(const ItemInstanceT& item, LookupFn&& lookup) {
  for (std::size_t slot = 0; slot < item.enchantments.size(); ++slot) {
    const auto enchantment_id = item.enchantments[slot].id;
    const auto* enchantment =
        enchantment_id != 0 ? lookup(enchantment_id) : nullptr;
    if (enchantment == nullptr) {
      continue;
    }
    const auto effect_index =
        FindOnUseSpellEnchantmentEffectIndex(*enchantment);
    if (effect_index.has_value()) {
      return ItemOnUseSpellEnchantmentMatch{
          slot, enchantment_id, *effect_index, enchantment};
    }
  }
  return std::nullopt;
}

template <typename ItemTemplateT>
inline std::optional<ItemOnUseSpellArenaData> ResolveFirstOnUseSpellArenaData(
    const ItemTemplateT& item) {
  const auto* spell = FindFirstOnUseSpell(item);
  if (spell == nullptr) {
    return std::nullopt;
  }
  return ItemOnUseSpellArenaData{
      static_cast<std::int32_t>(spell->charges),
      static_cast<std::int32_t>(spell->cooldown),
      static_cast<std::int32_t>(spell->category_cooldown)};
}

}
