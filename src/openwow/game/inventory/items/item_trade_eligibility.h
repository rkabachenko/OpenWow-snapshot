#pragma once

#include "openwow/game/inventory/player_inventory_replica.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace openwow::game {

constexpr std::uint32_t kBoundTradeWindowSeconds = 7200u;

template <typename ItemInstanceT, typename LookupFn>
inline bool ItemHasBindingEnchantSlot(const ItemInstanceT& item,
                                      LookupFn&& lookup) {
  if ((item.flags & ItemFlags::kQuestItem) != 0u) {
    return false;
  }
  for (const auto& enchantment : item.enchantments) {
    const auto* entry =
        enchantment.id != 0 ? lookup(enchantment.id) : nullptr;
    if (entry != nullptr && (entry->slot & 1u) != 0u) {
      return true;
    }
  }
  return false;
}

template <typename ItemInstanceT, typename LookupFn>
inline bool ItemIsEffectivelySoulbound(const ItemInstanceT& item,
                                       LookupFn&& lookup) {
  return (item.flags & ItemFlags::kSoulbound) != 0u ||
         ItemHasBindingEnchantSlot(item, std::forward<LookupFn>(lookup));
}

inline bool IsBoundTradeWindowExpired(const std::uint32_t created,
                                      const std::uint32_t played) {
  return static_cast<std::uint64_t>(created) + kBoundTradeWindowSeconds <=
         static_cast<std::uint64_t>(played);
}

inline bool HasBoundTradeWindowRemaining(const std::uint32_t created,
                                         const std::uint32_t played) {
  return !IsBoundTradeWindowExpired(created, played);
}

inline std::optional<std::uint32_t> ResolveBoundTradeWindowRemainingSeconds(
    const std::uint32_t created, const std::uint32_t played) {
  if (IsBoundTradeWindowExpired(created, played)) {
    return std::nullopt;
  }
  return static_cast<std::uint32_t>(
      static_cast<std::uint64_t>(created) + kBoundTradeWindowSeconds - played);
}

inline bool ItemBoundTradeExpiredForActivePlayer(
    const std::uint32_t flags, const std::uint32_t created,
    const std::uint32_t played, const bool has_binding_enchant) {
  if ((flags & ItemFlags::kSoulbound) == 0u && !has_binding_enchant) {
    return false;
  }
  return (flags & ItemFlags::kTradeWindow) == 0u ||
         IsBoundTradeWindowExpired(created, played);
}

template <typename ItemInstanceT, typename LookupFn>
inline bool ItemBoundTradeExpiredForActivePlayer(
    const ItemInstanceT& item, const std::uint32_t played, LookupFn&& lookup) {
  return ItemBoundTradeExpiredForActivePlayer(
      item.flags, item.create_played_time, played,
      ItemHasBindingEnchantSlot(item, std::forward<LookupFn>(lookup)));
}

template <typename ItemInstanceT>
inline bool ItemHasRefundBlockingEnchantmentState(const ItemInstanceT& item) {
  if ((item.flags & ItemFlags::kQuestItem) != 0u) {
    return false;
  }
  const auto has_enchant = [&item](const EnchantmentSlot slot) {
    return item.enchantments[static_cast<std::size_t>(slot)].id != 0u;
  };
  return has_enchant(EnchantmentSlot::Permanent) ||
         has_enchant(EnchantmentSlot::Socket1) ||
         has_enchant(EnchantmentSlot::Socket2) ||
         has_enchant(EnchantmentSlot::Socket3) ||
         has_enchant(EnchantmentSlot::Prismatic);
}

}
