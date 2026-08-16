#include "openwow/game/inventory/items/adapters/lua/item_detail_lua_api.h"
#include "openwow/game/inventory/items/adapters/lua/item_lua_adapter.h"
#include "openwow/game/actions/held_cursor/held_cursor.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"

#include "openwow/core/storm_string.h"
#include "openwow/game/inventory/equipment/equipped_item_type_matcher.h"
#include "openwow/game/inventory/adapters/ui/item_cursor_pickup_controller.h"
#include "openwow/game/inventory/adapters/ui/item_spell_target_controller.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/items/adapters/retail/item_display_name_formatter.h"
#include "openwow/game/inventory/items/item_link_parser.h"
#include "openwow/game/localization.h"
#include "openwow/game/objects/cgitem.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/spell_target_validation.h"
#include "openwow/ui/game/game_ui_core.h"
#include "openwow/ui/game/trade_cursor_utils.h"
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <utility>

namespace openwow::ui::game::detail {

namespace item_cursor = ::openwow::game::inventory::ui;
namespace item_targeting = ::openwow::game::inventory::ui;

namespace {

struct UseItemByNameMatch {
  openwow::game::ItemInstance item;
  std::uint8_t bag = 0;
  std::uint8_t slot = 0;
};

struct EquipItemByNameMatch {
  openwow::game::ItemInstance item;
  std::uint64_t container_guid = 0;
  std::uint8_t server_bag = openwow::game::InventorySlots::kMainBag;
  std::uint8_t source_slot = 0;
};

constexpr std::uint32_t kUseItemByNameNoSocketsFlag = 0x8u;
constexpr std::uint32_t kUseItemByNameUnusableFlag = 0x10u;
constexpr std::uint32_t kUniqueEquippedMarkerFlag = 0x00080000u;
constexpr char kGetItemUniquenessUsage[] = "Usage: GetItemUniqueness(itemID|\"name\"|\"itemlink\")";

std::uint32_t ResolveItemUniquenessArg(lua_State *L, const int index) {
  if (lua_isnumber(L, index) != 0) {
    return static_cast<std::uint32_t>(static_cast<int>(lua_tonumber(L, index)));
  }

  if (lua_isstring(L, index) == 0) {
    luaL_error(L, kGetItemUniquenessUsage);
    return 0;
  }

  const char *value = lua_tostring(L, index);
  if (value == nullptr || *value == '\0') {
    return 0;
  }

  if (const auto parsed_link = ::openwow::game::ItemLinkParser::Parse(value);
      parsed_link.has_value()) {
    return parsed_link->itemId;
  }

  if (const char *payload = FindAsciiSubstringIgnoreCase(value, "item:"); payload != nullptr) {
    return static_cast<std::uint32_t>(std::strtoul(payload + 5, nullptr, 10));
  }

  const auto* item_template =
      RequireItemLuaAdapter(L).queries().GetItemTemplateByName(value);
  return item_template != nullptr ? item_template->entry : 0u;
}

const openwow::data::dbc::ItemLimitCategoryEntry *
FindItemLimitCategoryEntry(lua_State *L, const std::uint32_t id) {
  if (id == 0u) {
    return nullptr;
  }

  const auto* dbc = RequireItemLuaAdapter(L).dbc();
  if (dbc == nullptr) {
    return nullptr;
  }

  return dbc->item_limit_category().LookupEntry(id);
}

std::string ResolveItemDisplayNameWithRandomProperty(lua_State *L, std::string display_name,
                                                     std::int32_t random_property_id) {
  auto& adapter = RequireItemLuaAdapter(L);
  return ::openwow::game::FormatItemDisplayNameWithRandomProperty(
      adapter.localization(), GetDbcLoader(L), display_name,
      random_property_id);
}

bool MatchesUseItemByNameLinkQuery(const openwow::game::ItemInstance &item,
                                   const openwow::game::ItemLinkData &query) {
  if (item.entry != query.itemId) {
    return false;
  }

  if ((item.flags & kUseItemByNameNoSocketsFlag) != 0) {
    return query.enchantId == 0 && query.gemIds[0] == 0 && query.gemIds[1] == 0 &&
           query.gemIds[2] == 0 && query.randomPropertyId == 0 && query.suffixFactor == 0;
  }

  if (query.enchantId != 0 && query.enchantId != item.GetPermanentEnchant()) {
    return false;
  }

  for (std::size_t i = 0; i < query.gemIds.size(); ++i) {
    if (query.gemIds[i] != 0 &&
        query.gemIds[i] != item.GetSocketEnchant(static_cast<std::uint8_t>(i))) {
      return false;
    }
  }

  if (query.randomPropertyId != 0 && query.randomPropertyId != item.random_property) {
    return false;
  }

  if (query.suffixFactor != 0 &&
      query.suffixFactor != static_cast<std::int32_t>(item.random_suffix)) {
    return false;
  }

  return true;
}

bool MatchesUseItemByNameDisplayQuery(lua_State *L, const openwow::game::ItemInstance &item,
                                      const std::string &base_name, std::string_view query) {
  const auto display_name =
      ResolveItemDisplayNameWithRandomProperty(L, base_name, item.random_property);
  return !display_name.empty() &&
         openwow::core::SStrCmpUTF8NoCase(display_name.c_str(), std::string(query).c_str(),
                                          0x7FFFFFFF) == 0;
}

bool ShouldSkipUseItemByNameCandidate(const openwow::game::ItemInstance &item) {
  if (item.IsEmpty()) {
    return true;
  }

  if ((item.flags & kUseItemByNameUnusableFlag) != 0) {
    return true;
  }

  return (item.flags & kUseItemByNameNoSocketsFlag) == 0 && item.random_property != 0 &&
         item.random_suffix == 0;
}

void UpdateBestUseItemByNameMatch(const openwow::game::ItemInstance &item, std::uint8_t bag,
                                  std::uint8_t slot,
                                  std::optional<UseItemByNameMatch> *best_match) {
  if (best_match == nullptr) {
    return;
  }

  if (!best_match->has_value() || item.count < best_match->value().item.count) {
    *best_match = UseItemByNameMatch{item, bag, slot};
  }
}

std::optional<UseItemByNameMatch> FindCarriedItemByEntry(
    const openwow::game::PlayerInventoryReplica& inventory,
    std::uint32_t item_entry) {
  if (item_entry == 0) {
    return std::nullopt;
  }

  std::optional<UseItemByNameMatch> best_match;

  for (std::uint8_t slot = 0; slot < openwow::game::PlayerInventoryReplica::kBackpackSize; ++slot) {
    const auto *item = inventory.GetBackpackSlot(slot);
    if (item == nullptr || ShouldSkipUseItemByNameCandidate(*item) || item->entry != item_entry) {
      continue;
    }

    UpdateBestUseItemByNameMatch(*item, openwow::game::InventorySlots::kMainBag, slot, &best_match);
  }

  for (std::uint8_t bag = 1; bag <= openwow::game::PlayerInventoryReplica::kMaxBags; ++bag) {
    const auto *bag_info = inventory.GetBag(bag);
    if (bag_info == nullptr) {
      continue;
    }

    for (std::uint8_t slot = 0; slot < bag_info->num_slots; ++slot) {
      const auto *item = inventory.GetBagSlot(bag, slot);
      if (item == nullptr || ShouldSkipUseItemByNameCandidate(*item) || item->entry != item_entry) {
        continue;
      }

      UpdateBestUseItemByNameMatch(*item, bag, slot, &best_match);
    }
  }

  return best_match;
}

std::optional<UseItemByNameMatch> FindCarriedItemByNameOrLink(lua_State *L,
                                                              ItemLuaAdapter& adapter,
                                                              std::string_view query) {
  if (query.empty()) {
    return std::nullopt;
  }

  const auto parsed_link = openwow::game::ItemLinkParser::Parse(std::string(query));
  auto& inventory = RequireItemLuaAdapter(L).inventory();
  std::optional<UseItemByNameMatch> best_match;

  const auto matches_item = [&](const openwow::game::ItemInstance &item) {
    if (ShouldSkipUseItemByNameCandidate(item)) {
      return false;
    }

    if (parsed_link.has_value()) {
      return MatchesUseItemByNameLinkQuery(item, *parsed_link);
    }

    const auto *item_template = adapter.queries().GetItemTemplate(item.entry);
    if (item_template == nullptr) {
      return false;
    }

    return MatchesUseItemByNameDisplayQuery(L, item, item_template->name, query);
  };

  for (std::uint8_t slot = 0; slot < openwow::game::PlayerInventoryReplica::kBackpackSize; ++slot) {
    const auto *item = inventory.GetBackpackSlot(slot);
    if (item != nullptr && matches_item(*item)) {
      UpdateBestUseItemByNameMatch(*item, openwow::game::InventorySlots::kMainBag, slot,
                                   &best_match);
    }
  }

  for (std::uint8_t bag = 1; bag <= openwow::game::PlayerInventoryReplica::kMaxBags; ++bag) {
    const auto *bag_info = inventory.GetBag(bag);
    if (bag_info == nullptr) {
      continue;
    }

    for (std::uint8_t slot = 0; slot < bag_info->num_slots; ++slot) {
      const auto *item = inventory.GetBagSlot(bag, slot);
      if (item != nullptr && matches_item(*item)) {
        UpdateBestUseItemByNameMatch(*item, bag, slot, &best_match);
      }
    }
  }

  return best_match;
}

template <typename MatchesItem>
std::optional<EquipItemByNameMatch>
FindFirstEquipItemByNameMatch(ItemLuaAdapter& adapter, MatchesItem &&matches_item) {
  namespace InventorySlots = openwow::game::InventorySlots;

  auto &inventory = adapter.inventory();
  const auto player_guid = adapter.objects().GetActivePlayerGuid().GetRawValue();
  if (player_guid == 0) {
    return std::nullopt;
  }

  for (std::uint8_t slot = 0; slot < openwow::game::PlayerInventoryReplica::kBackpackSize; ++slot) {
    if (const auto *item = inventory.GetBackpackSlot(slot);
        item != nullptr && matches_item(*item)) {
      return EquipItemByNameMatch{
          .item = *item,
          .container_guid = player_guid,
          .server_bag = InventorySlots::kMainBag,
          .source_slot = static_cast<std::uint8_t>(InventorySlots::kBackpackStart + slot),
      };
    }
  }

  for (std::uint8_t bag = 1; bag <= openwow::game::PlayerInventoryReplica::kMaxBags; ++bag) {
    const auto *bag_info = inventory.GetBag(bag);
    if (bag_info == nullptr || bag_info->guid == 0) {
      continue;
    }

    for (std::uint8_t slot = 0; slot < bag_info->num_slots; ++slot) {
      if (const auto *item = inventory.GetBagSlot(bag, slot);
          item != nullptr && matches_item(*item)) {
        return EquipItemByNameMatch{
            .item = *item,
            .container_guid = bag_info->guid,
            .server_bag = static_cast<std::uint8_t>(InventorySlots::kBagSlotsStart + bag - 1),
            .source_slot = slot,
        };
      }
    }
  }

  for (std::uint8_t slot = InventorySlots::kEquipStart; slot < InventorySlots::kEquipEnd; ++slot) {
    if (const auto *item = inventory.GetEquipSlot(slot); item != nullptr && matches_item(*item)) {
      return EquipItemByNameMatch{
          .item = *item,
          .container_guid = player_guid,
          .server_bag = InventorySlots::kMainBag,
          .source_slot = slot,
      };
    }
  }

  for (std::uint8_t bag = 1; bag <= openwow::game::PlayerInventoryReplica::kMaxBags; ++bag) {
    const auto *bag_info = inventory.GetBag(bag);
    if (bag_info == nullptr || bag_info->guid == 0 || bag_info->entry == 0) {
      continue;
    }

    openwow::game::ItemInstance bag_item;
    bag_item.guid = bag_info->guid;
    bag_item.entry = bag_info->entry;
    bag_item.count = 1;
    if (!matches_item(bag_item)) {
      continue;
    }

    return EquipItemByNameMatch{
        .item = bag_item,
        .container_guid = player_guid,
        .server_bag = InventorySlots::kMainBag,
        .source_slot = static_cast<std::uint8_t>(InventorySlots::kBagSlotsStart + bag - 1),
    };
  }

  return std::nullopt;
}

std::optional<EquipItemByNameMatch> FindEquipItemByEntry(
                                                         ItemLuaAdapter& adapter,
                                                         const std::uint32_t item_entry) {
  if (item_entry == 0) {
    return std::nullopt;
  }

  return FindFirstEquipItemByNameMatch(adapter,
                                       [item_entry](const openwow::game::ItemInstance &item) {
                                         return !item.IsEmpty() && item.entry == item_entry;
                                       });
}

std::optional<EquipItemByNameMatch> FindEquipItemByNameOrLink(lua_State *L,
                                                              ItemLuaAdapter& adapter,
                                                              std::string_view query) {
  if (query.empty()) {
    return std::nullopt;
  }

  const auto parsed_link = openwow::game::ItemLinkParser::Parse(std::string(query));
  return FindFirstEquipItemByNameMatch(adapter, [&](const openwow::game::ItemInstance &item) {
    if (item.IsEmpty()) {
      return false;
    }

    if (parsed_link.has_value()) {
      return MatchesUseItemByNameLinkQuery(item, *parsed_link);
    }

    const auto *item_template = adapter.queries().GetItemTemplate(item.entry);
    if (item_template == nullptr) {
      return false;
    }

    return MatchesUseItemByNameDisplayQuery(L, item, item_template->name, query);
  });
}

std::optional<openwow::game::ItemLinkData> ParseUseItemByNameLinkQuery(std::string_view query) {
  auto parsed = openwow::game::ItemLinkParser::Parse(std::string(query));
  if (parsed.has_value()) {
    return parsed;
  }

  std::string normalized(query);
  std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return openwow::game::ItemLinkParser::Parse(normalized);
}

template <typename Matcher>
std::optional<UseItemByNameMatch> FindUseItemByNameMatch(
                                                         const openwow::game::PlayerInventoryReplica& inventory,
                                                         Matcher &&matcher,
                                                         const bool equipped_only = false) {

  for (std::uint8_t slot = openwow::game::InventorySlots::kEquipStart;
       slot < openwow::game::InventorySlots::kEquipEnd; ++slot) {
    const auto *item = inventory.GetEquipSlot(slot);
    if (item != nullptr && matcher(*item)) {
      return UseItemByNameMatch{
          *item,
          openwow::game::InventorySlots::kMainBag,
          slot,
      };
    }
  }

  if (equipped_only) {
    return std::nullopt;
  }

  for (std::uint8_t slot = 0; slot < openwow::game::PlayerInventoryReplica::kBackpackSize; ++slot) {
    const auto *item = inventory.GetBackpackSlot(slot);
    if (item != nullptr && matcher(*item)) {
      return UseItemByNameMatch{
          *item,
          openwow::game::InventorySlots::kMainBag,
          static_cast<std::uint8_t>(openwow::game::InventorySlots::kBackpackStart + slot),
      };
    }
  }

  for (std::uint8_t bag = 1; bag <= openwow::game::PlayerInventoryReplica::kMaxBags; ++bag) {
    const auto *bag_info = inventory.GetBag(bag);
    if (bag_info == nullptr) {
      continue;
    }

    for (std::uint8_t slot = 0; slot < bag_info->num_slots; ++slot) {
      const auto *item = inventory.GetBagSlot(bag, slot);
      if (item != nullptr && matcher(*item)) {
        return UseItemByNameMatch{
            *item,
            static_cast<std::uint8_t>(openwow::game::InventorySlots::kBagSlotsStart + (bag - 1)),
            slot,
        };
      }
    }
  }

  return std::nullopt;
}

std::optional<UseItemByNameMatch> FindUseItemByNameMatch(lua_State *L,
                                                         ItemLuaAdapter& adapter,
                                                         const int argument_index,
                                                         const bool equipped_only = false) {
  if (lua_isnumber(L, argument_index) != 0) {
    const auto item_entry = TruncateLuaNumberToWrappedLowU32(lua_tonumber(L, argument_index));
    if (item_entry == 0) {
      return std::nullopt;
    }

    return FindUseItemByNameMatch(
        RequireItemLuaAdapter(L).inventory(),
        [item_entry](const openwow::game::ItemInstance &item) { return item.entry == item_entry; },
        equipped_only);
  }

  const auto query = SafeLuaString(L, argument_index);
  if (query.empty()) {
    return std::nullopt;
  }

  if (const auto parsed_link = ParseUseItemByNameLinkQuery(query); parsed_link.has_value()) {
    return FindUseItemByNameMatch(
        RequireItemLuaAdapter(L).inventory(),
        [&parsed_link](const openwow::game::ItemInstance &item) {
          return MatchesUseItemByNameLinkQuery(item, *parsed_link);
        },
        equipped_only);
  }

  return FindUseItemByNameMatch(
      RequireItemLuaAdapter(L).inventory(),
      [L, &adapter, &query](const openwow::game::ItemInstance &item) {
        std::string base_name;
        if (const auto *item_template = adapter.queries().GetItemTemplate(item.entry);
            item_template != nullptr) {
          base_name = item_template->name;
        } else if (const auto* cached_item =
                       RequireItemLuaAdapter(L).items().GetItem(item.entry);
                   cached_item != nullptr) {
          base_name = cached_item->name;
        }

        return !base_name.empty() && MatchesUseItemByNameDisplayQuery(L, item, base_name, query);
      },
      equipped_only);
}

}

int LuaItemHasRange(lua_State *L) {
  const auto view = ResolveItemSpellView(L, 1);
  if (!view ||
      RequireItemLuaAdapter(L).objects().GetLocalPlayerTyped() == nullptr) {
    lua_pushnil(L);
    return 1;
  }

  lua_pushwowbool(L, SpellHasScriptRange(L, *view.spell));
  return 1;
}

int LuaIsItemInRange(lua_State *L) {
  const auto view = ResolveItemSpellView(L, 1);
  auto& adapter = RequireItemLuaAdapter(L);
  if (!view || !adapter.objects().GetLocalPlayerTyped()) {
    lua_pushnil(L);
    return 1;
  }

  const auto unit_id = SafeLuaString(L, 2);
  if (unit_id.empty()) {
    lua_pushnil(L);
    return 1;
  }

  const auto target_guid = adapter.ResolveUnit(unit_id);
  if (target_guid.IsEmpty()) {
    lua_pushnil(L);
    return 1;
  }

  const auto result = adapter.ItemInRange(view.spell_id, target_guid);
  if (!result.has_value()) {
    lua_pushnil(L);
    return 1;
  }

  if (*result) {
    lua_pushnumber(L, 1.0);
    return 1;
  }
  lua_pushnumber(L, 0.0);
  return 1;
}

int LuaUseItemByName(lua_State *L) {
  auto& adapter = RequireItemLuaAdapter(L);
  if (adapter.objects().GetLocalPlayerTyped() == nullptr) {
    return 0;
  }

  std::optional<UseItemByNameMatch> item_match;
  if (lua_isnumber(L, 1) != 0) {
    const auto item_entry = TruncateLuaNumberToWrappedLowU32(lua_tonumber(L, 1));
    item_match = FindCarriedItemByEntry(
        RequireItemLuaAdapter(L).inventory(), item_entry);
  } else {
    item_match = FindCarriedItemByNameOrLink(L, adapter, SafeLuaString(L, 1));
  }
  if (!item_match.has_value()) {
    return 0;
  }

  const auto unit_query = SafeLuaString(L, 2);
  std::uint64_t target_guid = 0;
  const auto resolved_target = adapter.ResolveItemUseTarget(unit_query);
  if (!resolved_target.has_value()) {
    return 0;
  }
  target_guid = *resolved_target;

  if (const auto* session = GetWorldSession(L); session != nullptr) {
    const auto targeting = session->spells().GetTargeting().GetState();
    if (targeting.isActive && targeting.spellId != 0u) {
      adapter.PromptItemTarget(
          ::openwow::game::ObjectGuid(item_match->item.guid));
      return 0;
    }
  }

  if (adapter.held_cursor() != nullptr) {
    adapter.held_cursor()->Clear();
  }

  if (adapter.interaction().TryQueueBindOnUseConfirmation(
          item_match->item.guid, item_match->item.entry, item_match->item.flags, target_guid)) {
    return 0;
  }

  if (target_guid == 0) {
    if (const auto *item_template =
            adapter.queries().GetOrRequestItemTemplate(item_match->item.entry);
        item_template != nullptr &&
        adapter.StartItemTargeting(item_match->item, *item_template)) {
      return 0;
    }
  }

  adapter.interaction().SendUseItemByGuid(
      item_match->item.guid, 0, target_guid);
  return 0;
}

int LuaEquipItemByName(lua_State *L) {
  auto& adapter = RequireItemLuaAdapter(L);
  if (adapter.objects().GetActivePlayerGuid().IsEmpty()) {
    return 0;
  }
  auto* cursor = adapter.held_cursor();
  if (cursor == nullptr) {
    return 0;
  }

  int dst_slot = 0;
  const bool has_dst_slot = lua_isnoneornil(L, 2) == 0;
  if (has_dst_slot &&
      (!ResolveInventorySlotArgument(L, 2, &dst_slot) || dst_slot < 0 || dst_slot > 18)) {
    return luaL_error(L, "EquipItemByName(): Invalid inventory dstSlot");
  }

  std::optional<EquipItemByNameMatch> match;
  if (lua_isnumber(L, 1) != 0) {
    const auto item_id = static_cast<lua_Number>(lua_tonumber(L, 1));
    if (item_id <= 0 ||
        item_id > static_cast<lua_Number>(std::numeric_limits<std::uint32_t>::max())) {
      return 0;
    }
    match = FindEquipItemByEntry(adapter, static_cast<std::uint32_t>(item_id));
  } else if (lua_isstring(L, 1) != 0) {
    match = FindEquipItemByNameOrLink(L, adapter, SafeLuaString(L, 1));
  }

  if (!match.has_value()) {
    return 0;
  }
  const auto* live_item =
      adapter.objects().GetItem(openwow::game::ObjectGuid(match->item.guid));
  if (live_item == nullptr || live_item->IsLocked()) {
    return 0;
  }

  const auto* held_item = cursor->live_item();
  const auto held_container_guid =
      held_item != nullptr ? held_item->source_container_guid : 0u;
  if (has_dst_slot) {
    if (held_container_guid == match->container_guid &&
        match->source_slot == static_cast<std::uint8_t>(dst_slot)) {
      return 0;
    }

    if (adapter.trade().IsLocalPlayerTradeItemGuid(match->item.guid)) {
      return 0;
    }

    adapter.world_session().inventory_commands().RequestEquipInSlot(
        {
            .item_guid = match->item.guid,
            .source_container_guid = match->container_guid,
            .source_slot = match->source_slot,
            .item_entry = match->item.entry,
        },
        static_cast<std::uint32_t>(dst_slot));
    return 0;
  }

  if (held_container_guid == match->container_guid &&
      match->source_slot <= openwow::game::InventorySlots::kTabard) {
    return 0;
  }

  if (adapter.trade().IsLocalPlayerTradeItemGuid(match->item.guid)) {
    return 0;
  }

  (void)item_cursor::PickupItemCursor(
      *cursor, adapter.inventory(), adapter.items(), adapter.dbc(),
      adapter.objects().GetActivePlayerGuid(),
      ::openwow::game::ObjectGuid(match->item.guid),
      {
          .source =
              {
                  .container = ::openwow::game::ObjectGuid(match->container_guid),
                  .slot = match->source_slot,
              },
          .sound = item_cursor::ItemCursorPickupSound::kSilent,
      });
  GameUI_OnMouseoverUnitEnter(match->item.guid);

  adapter.world_session().inventory_commands().RequestAutoEquip({
      .item_guid = match->item.guid,
      .source_container_guid = match->container_guid,
      .source_slot = match->source_slot,
      .item_entry = match->item.entry,
  });
  return 0;
}

int LuaIsEquippedItem(lua_State *L) {
  auto& adapter = RequireItemLuaAdapter(L);
  if (adapter.objects().GetLocalPlayerTyped() == nullptr) {
    lua_pushnil(L);
    return 1;
  }

  if (FindUseItemByNameMatch(L, adapter, 1, true).has_value()) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaIsEquippedItemType(lua_State *L) {
  const char *equipped_type = lua_tostring(L, 1);
  if (equipped_type == nullptr || *equipped_type == '\0') {
    return luaL_error(L, "Usage: IsEquippedItemType(\"type\")");
  }

  auto& adapter = RequireItemLuaAdapter(L);
  if (adapter.objects().GetLocalPlayerTyped() == nullptr) {
    lua_pushnil(L);
    return 1;
  }

  if (openwow::game::MatchesEquippedItemTypeQuery(
          adapter.inventory(), equipped_type,
          {.query_cache = &adapter.queries(),
           .dbc = adapter.dbc(),
           .localize =
               [&adapter](const std::string_view key) {
                 return adapter.localization().GetString(std::string(key));
               }})) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }

  return 1;
}

int LuaIsCurrentItem(lua_State *L) {
  const auto item_id = ResolveItemIdArg(L, 1);
  lua_pushwowbool(
      L, ::openwow::game::IsCurrentItemEntry(
             RequireItemLuaAdapter(L).world_session(), item_id));
  return 1;
}

int LuaGetItemUniqueness(lua_State *L) {
  const auto item_id = ResolveItemUniquenessArg(L, 1);
  if (item_id == 0u) {
    return 0;
  }

  bool have_template = false;
  std::uint32_t flags2 = 0;
  std::uint32_t item_limit_category = 0;

  auto& adapter = RequireItemLuaAdapter(L);
  if (const auto* item_template = adapter.queries().GetItemTemplate(item_id);
      item_template != nullptr) {
    have_template = true;
    flags2 = item_template->flags2;
    item_limit_category = item_template->item_limit_category;
  } else {
    (void)adapter.queries().GetOrRequestItemTemplate(item_id);
  }

  if (!have_template) {
    if (const auto* item_template = adapter.items().GetItem(item_id);
        item_template != nullptr) {
      have_template = true;
      flags2 = item_template->flags2;
      item_limit_category = item_template->item_limit_category;
    }
  }

  if (!have_template) {
    return 0;
  }

  if ((flags2 & kUniqueEquippedMarkerFlag) != 0u) {
    lua_pushnumber(L, -1.0);
    lua_pushnumber(L, 1.0);
    return 2;
  }

  const auto *limit_entry = FindItemLimitCategoryEntry(L, item_limit_category);
  if (limit_entry == nullptr) {
    return 0;
  }

  lua_pushnumber(L, static_cast<lua_Number>(item_limit_category));
  lua_pushnumber(L, static_cast<lua_Number>(limit_entry->quantity));
  return 2;
}

}
