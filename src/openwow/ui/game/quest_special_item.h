#pragma once

#include "openwow/game/hyperlink.h"
#include "openwow/game/inventory/search/action_item_inventory_search.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/items/item_on_use_spell.h"
#include "openwow/game/quest_log.h"
#include "openwow/game/quest_manager.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/loot_tooltip_support.h"
#include "openwow/ui/game/quest_log_interleaved.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace openwow::ui::game::detail {

inline bool QuestSpecialItemHasResolvedUseSpell(openwow::game::WorldSession &session,
                                                const openwow::game::ItemInstance &item) {
  if (const auto *item_template = session.query_cache().GetOrRequestItemTemplate(item.entry);
      item_template != nullptr && openwow::game::FindFirstOnUseSpell(*item_template) != nullptr) {
    return true;
  }

  if (const auto *cached_item = session.item_definitions().GetItem(item.entry);
      cached_item != nullptr && openwow::game::FindFirstOnUseSpell(*cached_item) != nullptr) {
    return true;
  }

  const auto *dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    return false;
  }

  return openwow::game::FindFirstOnUseSpellEnchantment(
             item, [dbc](const std::uint32_t enchantment_id) {
               return dbc->spell_item_enchantment().LookupEntry(enchantment_id);
             })
      .has_value();
}

inline const openwow::game::ItemInstance *
ResolveQuestSpecialCarriedItem(openwow::game::WorldSession &session,
                               const openwow::game::QuestTemplate &quest_template) {
  if (quest_template.src_item_id != 0) {
    if (const auto *item =
            openwow::game::FindFirstDefaultCarriedInventoryItem(
                session.inventory_replica(),
                [&](const openwow::game::ItemInstance& candidate) {
                  return candidate.entry == quest_template.src_item_id;
            });
        item != nullptr &&
        QuestSpecialItemHasResolvedUseSpell(session, *item)) {
      return item;
    }
  }

  if (!openwow::game::HasFlag(quest_template.flags,
                              openwow::game::QuestFlags::kDisplayItemInTracker)) {
    return nullptr;
  }

  for (const auto &objective_item : quest_template.item_drop_objectives) {
    if (objective_item.item_id == 0) {
      continue;
    }

    if (const auto *item =
            openwow::game::FindFirstDefaultCarriedInventoryItem(
                session.inventory_replica(),
                [&](const openwow::game::ItemInstance& candidate) {
                  return candidate.entry == objective_item.item_id;
            });
        item != nullptr &&
        QuestSpecialItemHasResolvedUseSpell(session, *item)) {
      return item;
    }
  }

  return nullptr;
}

inline const openwow::game::ItemInstance *
ResolveQuestLogSpecialItem(openwow::game::WorldSession &session,
                           const std::uint32_t quest_log_index) {
  if (quest_log_index == 0) {
    return nullptr;
  }

  const auto quest_id = ResolveQuestIdFromInterleavedIndex(
      session, static_cast<int>(quest_log_index));
  if (quest_id == 0) {
    return nullptr;
  }

  const auto *quest_template = session.quests().GetOrRequestTemplate(quest_id);
  if (quest_template == nullptr) {
    return nullptr;
  }

  return ResolveQuestSpecialCarriedItem(session, *quest_template);
}

inline openwow::game::ItemQuality ResolveQuestSpecialItemQuality(
    openwow::game::WorldSession &session,
    const openwow::game::ItemInstance &item) {
  if (const auto *item_template = session.query_cache().GetOrRequestItemTemplate(item.entry);
      item_template != nullptr &&
      static_cast<std::uint32_t>(item_template->quality) < 8) {
    return item_template->quality;
  }

  if (const auto *cached_item = session.item_definitions().GetItem(item.entry);
      cached_item != nullptr) {
    return cached_item->quality;
  }

  return openwow::game::ItemQuality::Common;
}

inline std::string ResolveQuestSpecialItemDisplayName(
    const openwow::data::dbc::DbcLoader *dbc,
    openwow::game::WorldSession &session,
    const openwow::game::ItemInstance &item) {
  if (const auto *item_template = session.query_cache().GetOrRequestItemTemplate(item.entry);
      item_template != nullptr && !item_template->name.empty()) {
    return ResolveLootItemDisplayName(dbc, item_template->name, item.random_property);
  }

  if (const auto *cached_item = session.item_definitions().GetItem(item.entry);
      cached_item != nullptr && !cached_item->name.empty()) {
    return ResolveLootItemDisplayName(dbc, cached_item->name, item.random_property);
  }

  return {};
}

inline std::uint32_t ResolveQuestSpecialItemLinkLevel(
    const openwow::game::WorldSession &session) {
  if (const auto *player = session.objects().GetActivePlayer(); player != nullptr) {
    return player->State().GetLevel();
  }

  return 0;
}

inline std::string BuildQuestSpecialItemLink(const openwow::data::dbc::DbcLoader *dbc,
                                             openwow::game::WorldSession &session,
                                             const openwow::game::ItemInstance &item) {
  const auto display_name = ResolveQuestSpecialItemDisplayName(dbc, session, item);

  return openwow::game::HyperlinkParser::BuildItemLink(
      item.entry, display_name,
      static_cast<std::uint32_t>(
          ResolveQuestSpecialItemQuality(session, item)),
      static_cast<std::int32_t>(item.GetPermanentEnchant()),
      static_cast<std::int32_t>(item.GetSocketEnchant(0)),
      static_cast<std::int32_t>(item.GetSocketEnchant(1)),
      static_cast<std::int32_t>(item.GetSocketEnchant(2)), item.random_property,
      static_cast<std::int32_t>(item.random_suffix),
      static_cast<std::int32_t>(ResolveQuestSpecialItemLinkLevel(session)));
}

inline std::int32_t ResolveQuestSpecialItemCharges(openwow::game::WorldSession &session,
                                                   const openwow::game::ItemInstance &item) {
  const auto *dbc = session.GetDbcLoader();
  if (dbc != nullptr) {
    const auto enchant_match = openwow::game::FindFirstOnUseSpellEnchantment(
        item, [dbc](const std::uint32_t enchantment_id) {
          return dbc->spell_item_enchantment().LookupEntry(enchantment_id);
        });
    if (enchant_match.has_value() &&
        (item.flags & openwow::game::ItemFlags::kQuestItem) == 0u) {
      return static_cast<std::int16_t>(static_cast<std::uint16_t>(
          item.enchantments[enchant_match->enchantment_slot].charges));
    }
  }

  if (const auto *item_template = session.query_cache().GetOrRequestItemTemplate(item.entry);
      item_template != nullptr) {
    const auto spell_index = openwow::game::FindFirstOnUseSpellIndex(*item_template);
    if (spell_index >= 0 &&
        static_cast<std::size_t>(spell_index) < item.charges.size()) {
      return item.charges[static_cast<std::size_t>(spell_index)];
    }
  } else if (const auto *cached_item = session.item_definitions().GetItem(item.entry);
             cached_item != nullptr) {
    const auto spell_index = openwow::game::FindFirstOnUseSpellIndex(*cached_item);
    if (spell_index >= 0 &&
        static_cast<std::size_t>(spell_index) < item.charges.size()) {
      return item.charges[static_cast<std::size_t>(spell_index)];
    }
  }

  return 0;
}

}
