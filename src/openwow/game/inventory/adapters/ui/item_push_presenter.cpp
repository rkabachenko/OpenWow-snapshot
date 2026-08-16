#include "openwow/game/inventory/adapters/ui/item_push_presenter.h"

#include "openwow/game/hyperlink.h"
#include "openwow/game/inventory/adapters/protocol/inventory_messages.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/items/adapters/retail/item_display_name_formatter.h"
#include "openwow/game/inventory/items/item_icon_resolver.h"
#include "openwow/game/localization.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/tutorial_system.h"

#include <array>
#include <cstdio>
#include <string>

namespace openwow::game::inventory::ui {
namespace {

constexpr std::uint32_t kMainBagSlot = 0xFFu;
constexpr std::uint32_t kHearthstoneItemEntry = 6948u;
constexpr std::uint32_t kEquipTutorialPrerequisite = 0x37u;
constexpr std::array<std::uint32_t, 29> kInventoryTypeSlotMasks{
    0u,   1u,     2u,     4u,     8u,     16u,     32u,     64u,    128u,     256u,
    512u, 3072u,  12288u, 98304u, 65536u, 131072u, 16384u,  98304u, 7864320u, 262144u,
    16u,  98304u, 98304u, 65536u, 0u,     131072u, 131072u, 0u,     131072u,
};

std::string ResolveDisplayName(Localization& localization,
                               const openwow::data::dbc::DbcLoader* dbc,
                               const ItemTemplate &item_template,
                               const std::int32_t random_property_id) {
  return FormatItemDisplayNameWithRandomProperty(
      localization, dbc, item_template.name, random_property_id);
}

std::string BuildItemLink(const ItemPushResult &result,
                          const ItemTemplate &item_template,
                          const std::string &display_name) {
  return HyperlinkParser::BuildItemLink(
      result.item_entry, display_name,
      static_cast<std::uint32_t>(item_template.quality), 0, 0, 0, 0,
      result.random_property_id, static_cast<std::int32_t>(result.suffix_factor), 0, 0);
}

int ComputeEventSlot(const ItemPushResult &result) {
  if (result.bag_slot != kMainBagSlot) {
    return static_cast<int>(result.bag_slot) + 1;
  }

  const bool is_keyring_slot =
      result.item_slot >= inventory_constants::kKeyringStart &&
      result.item_slot < inventory_constants::kKeyringEnd;
  return is_keyring_slot ? -2 : 0;
}

bool IsLocalPlayerResult(const ObjectManager& objects, const ItemPushResult &result) {
  return result.player_guid == objects.GetLocalPlayerGuid();
}

bool HasTutorial8Trigger(const ItemTemplate &item_template) {
  const bool suppress_food_drink =
      item_template.item_class == ItemClass::Consumable &&
      (item_template.subclass == 4 || item_template.subclass == 5);
  if (suppress_food_drink) {
    return false;
  }

  for (const auto &spell : item_template.spells) {
    if (spell.spell_id != 0 && spell.trigger == 0) {
      return true;
    }
  }

  return false;
}

bool HasEquipTutorialPrerequisite(
    const ItemTemplate& item_template,
    const TutorialSystem& tutorials) {
  const auto inventory_type =
      static_cast<std::size_t>(item_template.inventory_type);
  if (inventory_type >= kInventoryTypeSlotMasks.size()) {
    return false;
  }

  if (kInventoryTypeSlotMasks[inventory_type] == 0) {
    return false;
  }

  if (!tutorials.IsCompletedBitsInitialized()) {
    return false;
  }

  const auto &completed_bits = tutorials.completed_bits();
  const auto word_index = kEquipTutorialPrerequisite >> 5;
  const auto bit = 1u << (kEquipTutorialPrerequisite & 0x1F);
  return word_index < completed_bits.size() && (completed_bits[word_index] & bit) != 0;
}

const char *ResolveChatFormatKey(const ItemPushResult &result, const bool is_local_player) {
  if (is_local_player) {
    if (result.count > 1) {
      if (result.created != 0) {
        return "LOOT_ITEM_CREATED_SELF_MULTIPLE";
      }
      if (result.pushed != 0) {
        return "LOOT_ITEM_PUSHED_SELF_MULTIPLE";
      }
      return "LOOT_ITEM_SELF_MULTIPLE";
    }

    if (result.created != 0) {
      return "LOOT_ITEM_CREATED_SELF";
    }
    if (result.pushed != 0) {
      return "LOOT_ITEM_PUSHED_SELF";
    }
    return "LOOT_ITEM_SELF";
  }

  if (result.pushed != 0 && result.created == 0) {
    return nullptr;
  }
  if (result.count > 1) {
    return result.created != 0 ? "CREATED_ITEM_MULTIPLE" : "LOOT_ITEM_MULTIPLE";
  }
  return result.created != 0 ? "CREATED_ITEM" : "LOOT_ITEM";
}

void DisplayChatMessage(
    const ObjectManager& objects,
    const openwow::data::dbc::DbcLoader* dbc,
    const ItemPushResult& result,
    const ItemTemplate& item_template, const CGPlayer_C& source_player,
    const ItemPushPresentationCapabilities& capabilities) {
  if (result.display_in_chat == 0) {
    return;
  }

  const bool is_local_player = IsLocalPlayerResult(objects, result);
  const char *format_key = ResolveChatFormatKey(result, is_local_player);
  if (format_key == nullptr) {
    return;
  }

  if (capabilities.localization == nullptr ||
      !capabilities.display_loot_message) {
    return;
  }
  const auto format =
      capabilities.localization->GetString(format_key, format_key);
  if (format.empty()) {
    return;
  }

  const auto display_name =
      ResolveDisplayName(*capabilities.localization, dbc, item_template,
                         result.random_property_id);
  const auto item_link = BuildItemLink(result, item_template, display_name);
  std::array<char, 3000> buffer{};

  if (is_local_player) {
    if (result.count > 1) {
      FormatRuntimeStringTemplateInto(buffer.data(), buffer.size(), format.c_str(),
                                      item_link.c_str(), static_cast<int>(result.count));
    } else {
      FormatRuntimeStringTemplateInto(buffer.data(), buffer.size(), format.c_str(),
                                      item_link.c_str());
    }
  } else {
    const auto source_name = source_player.GetPlayerName();
    if (result.count > 1) {
      FormatRuntimeStringTemplateInto(buffer.data(), buffer.size(), format.c_str(),
                                      source_name.c_str(), item_link.c_str(),
                                      static_cast<int>(result.count));
    } else {
      FormatRuntimeStringTemplateInto(buffer.data(), buffer.size(), format.c_str(),
                                      source_name.c_str(), item_link.c_str());
    }
  }

  capabilities.display_loot_message(buffer.data());
}

void FireItemPushEvent(
                       const ObjectManager& objects,
                       const openwow::data::dbc::DbcLoader* dbc,
                       const ItemPushResult &result,
                       const ItemTemplate &item_template,
                       const ItemPushPresentationCapabilities& capabilities) {
  if (!IsLocalPlayerResult(objects, result) ||
      !capabilities.fire_item_push) {
    return;
  }

  capabilities.fire_item_push(
      ComputeEventSlot(result),
      ResolveItemInventoryIconTexturePath(dbc,
                                          item_template.display_id));
}

void TriggerTutorials(const ObjectManager& objects, const ItemPushResult &result,
                      const ItemTemplate &item_template,
                      const CGPlayer_C &source_player,
                      const ItemPushPresentationCapabilities& capabilities) {
  if (!IsLocalPlayerResult(objects, result) ||
      capabilities.tutorials == nullptr) {
    return;
  }

  auto& tutorials = *capabilities.tutorials;
  tutorials.TriggerTutorial(7);

  if (HasTutorial8Trigger(item_template)) {
    tutorials.TriggerTutorial(8);
  }
  if (HasEquipTutorialPrerequisite(item_template, tutorials)) {
    tutorials.TriggerTutorial(0x17u);
  }
  if (item_template.item_class == ItemClass::Container) {
    tutorials.TriggerTutorial(9);
  }
  if (result.bag_slot == kMainBagSlot &&
      result.item_slot >= inventory_constants::kKeyringStart &&
      result.item_slot < inventory_constants::kKeyringEnd) {
    tutorials.TriggerTutorial(0x31u);
  }
  if (result.item_entry == kHearthstoneItemEntry) {
    tutorials.TriggerTutorial(0x1Eu);
  }
  if (source_player.State().GetLevel() >= 2 &&
      item_template.item_class == ItemClass::Consumable &&
      (item_template.subclass == 4 || item_template.subclass == 5)) {
    tutorials.TriggerTutorial(0x0Au);
    tutorials.TriggerTutorial(0x0Bu);
  }
  if (((item_template.inventory_type == InventoryType::Ranged ||
        item_template.inventory_type == InventoryType::RangedRight) &&
       source_player.State().GetClass() != 3) ||
      (item_template.item_class == ItemClass::Weapon &&
       item_template.subclass == 19)) {
    tutorials.TriggerTutorial(0x2Bu);
  }
}

void PresentResolvedResult(
                           ObjectManager& objects,
                           const openwow::data::dbc::DbcLoader* dbc,
                           const ItemPushResult &result,
                           const ItemTemplate &item_template,
                           const ItemPushPresentationCapabilities& capabilities) {
  const auto *source_player = objects.GetPlayer(result.player_guid);
  if (source_player == nullptr) {
    return;
  }

  FireItemPushEvent(objects, dbc, result, item_template, capabilities);
  DisplayChatMessage(
      objects, dbc, result, item_template, *source_player, capabilities);
  TriggerTutorials(
      objects, result, item_template, *source_player, capabilities);
}

}

void PresentItemPushResult(
    ObjectManager& objects, QueryCache& queries,
    const openwow::data::dbc::DbcLoader* dbc,
    const ItemPushResult& result,
    ItemPushPresentationCapabilities capabilities) {
  if (result.item_entry == 0) {
    return;
  }

  const auto *item_template = queries.GetOrRequestItemTemplate(
      result.item_entry,
      QueryCache::QueryRequestOptions{
          .dedupe_callbacks = false,
          .callback =
              [&objects, &queries, dbc, result, capabilities](const bool success) {
                if (success) {
                  PresentItemPushResult(
                      objects, queries, dbc, result, capabilities);
                }
              },
      });
  if (item_template != nullptr) {
    PresentResolvedResult(
        objects, dbc, result, *item_template, capabilities);
  }
}

}
