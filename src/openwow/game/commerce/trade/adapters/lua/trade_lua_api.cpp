
#include "openwow/game/commerce/trade/adapters/lua/trade_lua_adapter.h"
#include "openwow/game/commerce/trade/adapters/lua/trade_lua_offer_support.h"
#include "openwow/game/actions/held_cursor/held_cursor.h"
#include "openwow/game/commerce/trade/trade_item_location.h"
#include "openwow/game/hyperlink.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/items/adapters/retail/item_display_name_formatter.h"
#include "openwow/game/inventory/items/item_icon_resolver.h"
#include "openwow/game/inventory/items/item_link_parser.h"
#include "openwow/game/inventory/items/item_use_requirements.h"
#include "openwow/game/interaction_sender.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/commerce/trade/trade_interaction.h"
#include "openwow/game/inventory/items/item_trade_eligibility.h"
#include "openwow/ui/lua_numeric.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace openwow::ui::game::detail {

namespace {

constexpr int kErrEquippedBagSlot = 16;

constexpr int kErrSoulboundItem = 43;

constexpr int kErrQuestItemBonding = 44;

constexpr int kErrBindingEnchant = 45;

constexpr int kErrNonEmptyBag = 47;

bool HasBindingEnchantForTrade(const ::openwow::game::ItemInstance &item,
                               const ::openwow::data::dbc::DbcLoader *dbc) {
  if (!dbc) {
    return false;
  }
  return ::openwow::game::ItemHasBindingEnchantSlot(
      item, [dbc](const std::uint32_t id) {
        return dbc->spell_item_enchantment().LookupEntry(id);
      });
}

bool IsBoundTradeExpiredForTrade(const ::openwow::game::ItemInstance &item,
                                 const std::uint32_t current_played_time,
                                 const bool has_binding_enchant) {
  return ::openwow::game::ItemBoundTradeExpiredForActivePlayer(
      item.flags, item.create_played_time, current_played_time, has_binding_enchant);
}

bool BlockContainerOrEquippedBag(lua_State* L,
                                 const ::openwow::game::ItemInstance &cursor_item,
                                 const ::openwow::game::PlayerInventoryReplica &inventory) {

  auto &item_definitions = RequireTradeLuaAdapter(L).items();
  const auto *tmpl = item_definitions.GetItem(cursor_item.entry);
  if (!tmpl || tmpl->container_slots == 0) {
    return false;
  }

  for (std::uint8_t bag = 1; bag <= ::openwow::game::PlayerInventoryReplica::kMaxBags; ++bag) {
    const auto *bag_info = inventory.GetBag(bag);
    if (!bag_info || bag_info->item.guid != cursor_item.guid) {
      continue;
    }

    std::uint32_t occupied = 0;
    for (const auto &slot : bag_info->slots) {
      if (!slot.IsEmpty()) {
        ++occupied;
      }
    }
    if (occupied > 0) {
      RequireTradeLuaAdapter(L).ShowSystemMessage(kErrNonEmptyBag);
      return true;
    }

    const auto slot_index = inventory.FindSlotByGuid(cursor_item.guid);
    if (slot_index >= 0) {
      const auto abs_slot = static_cast<std::uint8_t>(slot_index);
      if (abs_slot >= ::openwow::game::InventorySlots::kBagSlotsStart &&
          abs_slot < ::openwow::game::InventorySlots::kBagSlotsEnd) {
        RequireTradeLuaAdapter(L).ShowSystemMessage(kErrEquippedBagSlot);
        return true;
      }
    }
    return false;
  }
  return false;
}

std::optional<::openwow::game::LocalPlayerTradeSlot>
GetLocalTradeSlot(lua_State* L, int index) {
  if (index < 1 || index > static_cast<int>(::openwow::game::kTradeSlotCount)) {
    return std::nullopt;
  }
  return RequireTradeLuaAdapter(L).trade().GetLocalPlayerTradeSlot(
      static_cast<std::size_t>(index - 1));
}

const ::openwow::game::ItemInstance *ResolveLocalTradeItem(lua_State* L, int index) {
  auto slot = GetLocalTradeSlot(L, index);
  if (!slot) {
    return nullptr;
  }
  return ::openwow::game::GetTradeContainerItem(
      RequireTradeLuaAdapter(L).inventory(), slot->source_bag,
      slot->source_slot);
}

const ::openwow::game::ItemTemplate *GetCachedTradeItemTemplate(
    lua_State* L, std::uint32_t item_entry) {
  if (item_entry == 0) {
    return nullptr;
  }

  return RequireTradeLuaAdapter(L).queries().GetOrRequestItemTemplate(
      item_entry);
}

std::string ResolveTradeDisplayName(lua_State *L, std::string display_name,
                                    std::int32_t random_property_id) {
  return ::openwow::game::FormatItemDisplayNameWithRandomProperty(
      RequireTradeLuaAdapter(L).localization(),
      RequireTradeLuaAdapter(L).dbc(), display_name, random_property_id);
}

std::string ResolveLocalTradePlayerItemName(lua_State *L, TradeLuaAdapter *adapter,
                                            const ::openwow::game::ItemInstance &item,
                                            const ::openwow::game::ItemTemplate *item_template) {
  if (item_template && !item_template->name.empty()) {
    return ResolveTradeDisplayName(L, item_template->name, item.random_property);
  }

  if (adapter) {
    if (const auto *query_template = adapter->queries().GetItemTemplate(item.entry);
        query_template && !query_template->name.empty()) {
      return ResolveTradeDisplayName(L, query_template->name, item.random_property);
    }
  }

  return {};
}

std::uint32_t ResolveLocalTradeItemDisplayId(
    lua_State *L, const ::openwow::game::ItemInstance &item,
    const ::openwow::game::ItemTemplate *item_template) {
  if (const auto *dbc = RequireTradeLuaAdapter(L).dbc()) {
    if (const auto *item_entry = dbc->item().LookupEntry(item.entry)) {
      return item_entry->display_info_id;
    }
  }

  if (item_template) {
    return item_template->display_id;
  }

  return 0;
}

void SetCursorToTradeItem(lua_State *L, const ::openwow::game::ItemInstance &item,
                          const ::openwow::game::LocalPlayerTradeSlot &slot) {
  RequireTradeLuaAdapter(L).HoldTradeItem(
      item, slot.source_bag, slot.source_slot);
}

std::uint32_t GetTraderTradeGold(
    const ::openwow::game::TradeInteraction& trade) {
  return trade.trader_gold();
}

std::optional<std::size_t> ParseTradeLuaIndex(lua_State *L, const char *usage) {
  if (!lua_isnumber(L, 1)) {
    luaL_error(L, usage);
    return std::nullopt;
  }

  const auto trade_index =
      ::openwow::ui::TruncateLuaNumberToI32(lua_tonumber(L, 1));
  if (trade_index < 1 || trade_index > ::openwow::game::kTradeSlotCount) {
    return std::nullopt;
  }

  return static_cast<std::size_t>(trade_index - 1);
}

const ::openwow::game::TradeWindow *GetTraderTradeWindow(
    const ::openwow::game::TradeInteraction& trade) {
  const auto &window = trade.trader_window();
  return window ? &*window : nullptr;
}

const ::openwow::game::TradeWindow *GetOwnTradeWindow(
    const ::openwow::game::TradeInteraction& trade) {
  const auto &window = trade.own_window();
  return window ? &*window : nullptr;
}

const ::openwow::game::TradeSlotItem *GetTraderTradeSlot(
    const ::openwow::game::TradeInteraction& trade, std::size_t slot_index) {
  const auto *window = GetTraderTradeWindow(trade);
  if (!window || slot_index >= window->slots.size()) {
    return nullptr;
  }

  const auto &slot = window->slots[slot_index];
  return slot.item_id != 0 ? &slot : nullptr;
}

void PushEmptyTradePlayerItemInfo(lua_State *L) {
  lua_pushnil(L);
  lua_pushnil(L);
  lua_pushnumber(L, 0);
  lua_pushnumber(L, 0);
  lua_pushnil(L);
}

void PushEmptyTradeTargetItemInfo(lua_State *L) {
  lua_pushnil(L);
  lua_pushnil(L);
  lua_pushnumber(L, 0);
  lua_pushnumber(L, 0);
  lua_pushnil(L);
  lua_pushnil(L);
}

std::string ResolveTradeTexturePath(lua_State *L, std::uint32_t display_info_id) {
  return ::openwow::game::ResolveItemInventoryIconTexturePath(
      RequireTradeLuaAdapter(L).dbc(), display_info_id);
}

std::string ResolveTradeTargetDisplayName(
    lua_State *L, const ::openwow::game::ItemTemplate &item_template,
    std::int32_t random_property_id) {
  return ResolveTradeDisplayName(L, item_template.name, random_property_id);
}

bool LocalPlayerHasTradeTargetItemUseError(
    const TradeLuaAdapter *adapter,
    const ::openwow::game::ItemTemplate &item_template) {
  if (!adapter) {
    return true;
  }

  const auto *player = adapter->objects().GetLocalPlayerTyped();
  if (!player) {
    return true;
  }
  return !adapter->MeetsItemRequirements(
      *player, ::openwow::game::BuildItemUseRequirementView(item_template));
}

std::optional<std::string> ResolveTradeSlot7Text(
    const TradeLuaAdapter *adapter, std::uint32_t text_id) {
  if (!adapter || text_id == 0) {
    return std::nullopt;
  }

  const auto *npc_text = adapter->queries().GetNpcText(text_id);
  if (!npc_text) {
    return std::nullopt;
  }

  for (const auto &block : npc_text->blocks) {
    if (block.probability <= 0.0f) {
      continue;
    }
    if (!block.text_male.empty()) {
      return block.text_male;
    }
    if (!block.text_female.empty()) {
      return block.text_female;
    }
  }

  return std::nullopt;
}

}

int LuaGetTradePlayerItemInfo(lua_State *L) {
  const auto slot_index = ParseTradeLuaIndex(L, "Usage: GetTradePlayerItemInfo(index)");
  if (!slot_index) {
    PushEmptyTradePlayerItemInfo(L);
    return 5;
  }

  auto *adapter = &RequireTradeLuaAdapter(L);
  const auto *item = ResolveLocalTradeItem(L, static_cast<int>(*slot_index) + 1);
  if (!item || item->IsEmpty()) {
    PushEmptyTradePlayerItemInfo(L);
    return 5;
  }

  const auto *item_template = adapter->queries().GetItemTemplate(item->entry);
  const auto name = ResolveLocalTradePlayerItemName(L, adapter, *item, item_template);
  const auto texture_path =
      ResolveTradeTexturePath(L, ResolveLocalTradeItemDisplayId(L, *item, item_template));
  const auto quality =
      item_template ? static_cast<std::int32_t>(item_template->quality) : -1;

  lua_pushstring(L, name.c_str());
  lua_pushstring(L, texture_path.c_str());
  lua_pushnumber(L, static_cast<double>(item->count));
  lua_pushnumber(L, static_cast<lua_Number>(quality));

  if (*slot_index == ::openwow::game::kTradeWillNotBeTradedSlot) {
    if (const auto *window = GetOwnTradeWindow(RequireTradeLuaAdapter(L).trade())) {
      if (const auto slot7_text = ResolveTradeSlot7Text(adapter, window->slot7_text_id)) {
        lua_pushstring(L, slot7_text->c_str());
      } else {
        lua_pushnil(L);
      }
    } else {
      lua_pushnil(L);
    }
  } else {
    lua_pushnil(L);
  }

  return 5;
}

int LuaGetTradePlayerItemLink(lua_State *L) {
  const auto slot_index = ParseTradeLuaIndex(L, "Usage: GetTradePlayerItemLink(index)");
  if (!slot_index) {
    return 0;
  }

  auto *adapter = &RequireTradeLuaAdapter(L);

  const auto *item =
      ResolveLocalTradeItem(L, static_cast<int>(*slot_index) + 1);
  if (!item || item->IsEmpty()) {
    return 0;
  }

  const auto *item_template = adapter->queries().GetItemTemplate(item->entry);

  ::openwow::game::ItemLinkData link_data;
  link_data.itemId = item->entry;
  const bool is_gift_wrapped =
      (item->flags & ::openwow::game::ItemFlags::kGiftWrapped) != 0;
  if (!is_gift_wrapped) {
    link_data.enchantId = item->GetPermanentEnchant();
    link_data.gemIds[0] = item->GetSocketEnchant(0);
    link_data.gemIds[1] = item->GetSocketEnchant(1);
    link_data.gemIds[2] = item->GetSocketEnchant(2);
    link_data.randomPropertyId = item->random_property;
    link_data.suffixFactor = static_cast<std::int32_t>(item->random_suffix);
  }
  link_data.name =
      ResolveLocalTradePlayerItemName(L, adapter, *item, item_template);
  const auto quality = item_template
                           ? static_cast<std::uint32_t>(item_template->quality)
                           : 1u;
  link_data.quality = static_cast<std::uint8_t>(quality < 8 ? quality : 1u);

  if (const auto *player = adapter->objects().GetActivePlayer();
      player != nullptr) {
    link_data.linkLevel = player->State().GetLevel();
  }

  const auto link = ::openwow::game::ItemLinkParser::Generate(link_data);
  lua_pushstring(L, link.c_str());
  return 1;
}

int LuaGetTradeTargetItemInfo(lua_State *L) {
  const auto slot_index = ParseTradeLuaIndex(L, "Usage: GetTradeTargetItemInfo(index)");
  if (!slot_index) {
    PushEmptyTradeTargetItemInfo(L);
    return 6;
  }

  auto *adapter = &RequireTradeLuaAdapter(L);
  const auto& trade = RequireTradeLuaAdapter(L).trade();
  const auto *window = GetTraderTradeWindow(trade);
  const auto *slot = GetTraderTradeSlot(trade, *slot_index);
  if (!window || !slot) {
    PushEmptyTradeTargetItemInfo(L);
    return 6;
  }

  const auto *item_template =
      adapter ? adapter->queries().GetOrRequestItemTemplate(slot->item_id) : nullptr;
  if (!item_template) {
    PushEmptyTradeTargetItemInfo(L);
    return 6;
  }

  const auto display_name =
      ResolveTradeTargetDisplayName(L, *item_template, slot->random_property_id);
  const auto texture_path = ResolveTradeTexturePath(L, slot->display_info_id);

  lua_pushstring(L, display_name.c_str());
  lua_pushstring(L, texture_path.c_str());
  lua_pushnumber(L, static_cast<lua_Number>(slot->stack_count));
  lua_pushnumber(
      L, static_cast<lua_Number>(
             static_cast<std::uint32_t>(item_template->quality)));

  if (LocalPlayerHasTradeTargetItemUseError(adapter, *item_template)) {
    lua_pushnumber(L, 0.0);
  } else {
    lua_pushnil(L);
  }

  if (*slot_index == ::openwow::game::kTradeWillNotBeTradedSlot) {
    if (const auto slot7_text = ResolveTradeSlot7Text(adapter, window->slot7_text_id)) {
      lua_pushstring(L, slot7_text->c_str());
    } else {
      lua_pushnil(L);
    }
  } else {
    lua_pushnil(L);
  }

  return 6;
}

int LuaGetTradeTargetItemLink(lua_State *L) {
  const auto slot_index = ParseTradeLuaIndex(L, "Usage: GetTradeTargetItemLink(index)");
  if (!slot_index) {
    return 0;
  }

  auto *adapter = &RequireTradeLuaAdapter(L);

  const auto *slot = GetTraderTradeSlot(
      RequireTradeLuaAdapter(L).trade(), *slot_index);
  if (!slot) {
    return 0;
  }

  const auto *item_template = adapter->queries().GetItemTemplate(slot->item_id);
  if (!item_template) {
    return 0;
  }

  ::openwow::game::ItemLinkData link_data;
  link_data.itemId = slot->item_id;
  link_data.enchantId = slot->permanent_enchant;
  link_data.gemIds[0] = slot->socket_enchants[0];
  link_data.gemIds[1] = slot->socket_enchants[1];
  link_data.gemIds[2] = slot->socket_enchants[2];
  link_data.randomPropertyId = slot->random_property_id;
  link_data.suffixFactor = static_cast<std::int32_t>(slot->suffix_factor);
  link_data.name = ResolveTradeTargetDisplayName(L, *item_template, slot->random_property_id);
  const auto quality =
      static_cast<std::uint32_t>(item_template->quality);
  link_data.quality =
      static_cast<std::uint8_t>(quality < 8 ? quality : 1);

  if (const auto *player = adapter->objects().GetActivePlayer(); player != nullptr) {
    link_data.linkLevel = player->State().GetLevel();
  }

  const auto link = ::openwow::game::ItemLinkParser::Generate(link_data);
  lua_pushstring(L, link.c_str());
  return 1;
}

int LuaGetPlayerTradeMoney(lua_State *L) {
  const auto& trade = RequireTradeLuaAdapter(L).trade();

  const auto copper =
      trade.begin_trade_guid() != 0 ? trade.own_gold() : 0;
  lua_pushnumber(L, static_cast<double>(copper));
  return 1;
}

int LuaSetTradeMoney(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: SetTradeMoney(amount)");
  }

  const auto copper = static_cast<std::uint32_t>(
      ::openwow::ui::TruncateLuaNumberToI32(lua_tonumber(L, 1)));
  auto *adapter = &RequireTradeLuaAdapter(L);

  TrySetOwnTradeGold(*adapter, RequireTradeLuaAdapter(L).trade(), copper);
  return 0;
}

int LuaGetTargetTradeMoney(lua_State *L) {
  lua_pushnumber(
      L, static_cast<double>(GetTraderTradeGold(RequireTradeLuaAdapter(L).trade())));
  return 1;
}

int LuaAddTradeMoney(lua_State *L) {
  auto *adapter = &RequireTradeLuaAdapter(L);

  auto* cursor = adapter->cursor();
  const auto* money =
      cursor != nullptr
          ? cursor->get_if<
                ::openwow::game::actions::held_cursor::PlayerMoney>()
          : nullptr;
  if (money == nullptr) {
    return 0;
  }

  auto& trade = RequireTradeLuaAdapter(L).trade();
  const auto next_gold = GetOwnTradeGold(trade) + money->amount;
  if (!TrySetOwnTradeGold(*adapter, trade, next_gold)) {
    return 0;
  }

  cursor->Clear({
      .release_source_lease = true,
      .publish_money_owner_update = false,
  });
  return 0;
}

int LuaClickTradeButton(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    luaL_error(L, "Usage: ClickTradeButton(index)");
    return 0;
  }

  auto *adapter = &RequireTradeLuaAdapter(L);
  auto& trade = RequireTradeLuaAdapter(L).trade();

  const std::uint32_t slot_index = static_cast<std::uint32_t>(
      ::openwow::ui::TruncateLuaNumberToI32(lua_tonumber(L, 1))) - 1u;
  if (slot_index >= ::openwow::game::kTradeSlotCount) {
    return 0;
  }
  const auto trade_slot = static_cast<std::uint8_t>(slot_index);
  const int lua_index = static_cast<int>(slot_index + 1u);

  auto &inventory = RequireTradeLuaAdapter(L).inventory();
  auto* cursor = adapter->cursor();
  if (cursor == nullptr) {
    return 0;
  }
  if (lua_toboolean(L, 2) != 0) {
    const auto previous_slot = trade.GetLocalPlayerTradeSlot(trade_slot);
    const auto *previous_item = ResolveLocalTradeItem(L, lua_index);

    if (!previous_slot || previous_slot->item_guid == 0) {
      return 0;
    }

    if (!adapter->interaction().SendClearTradeItem(trade_slot)) {
      return 0;
    }
    if (!trade.ClearLocalPlayerTradeSlot(trade_slot)) {
      return 0;
    }
    adapter->Present(TradeLuaEvent::kPlayerItemChanged, lua_index);

    RequireTradeLuaAdapter(L).LeaveItemMouseover(previous_slot->item_guid);

    if (previous_item && !previous_item->IsEmpty()) {
      SetCursorToTradeItem(L, *previous_item, *previous_slot);
    }

    return 0;
  }
  if (const auto* money =
          cursor->get_if<
              ::openwow::game::actions::held_cursor::PlayerMoney>()) {
    const auto next_gold = GetOwnTradeGold(trade) + money->amount;
    if (TrySetOwnTradeGold(*adapter, trade, next_gold)) {
      cursor->Clear({
          .release_source_lease = true,
          .publish_money_owner_update = false,
      });
    }
    return 0;
  }
  const auto* held_item = cursor->live_item();
  const auto* cursor_item =
      held_item != nullptr ? &held_item->item : nullptr;
  if (slot_index < ::openwow::game::kTradeSlotTradedCount && cursor_item &&
      !cursor_item->IsEmpty()) {
    const auto *dbc = RequireTradeLuaAdapter(L).dbc();
    const bool has_binding_enchant = HasBindingEnchantForTrade(*cursor_item, dbc);

    if (IsBoundTradeExpiredForTrade(*cursor_item, adapter->CurrentPlayedTime(),
                                    has_binding_enchant)) {
      const auto *tmpl =
          GetCachedTradeItemTemplate(L, cursor_item->entry);
      if (tmpl && tmpl->bonding == 4) {
        RequireTradeLuaAdapter(L).ShowSystemMessage(kErrQuestItemBonding);
      } else {
        RequireTradeLuaAdapter(L).ShowSystemMessage(kErrSoulboundItem);
      }
      return 0;
    }

    if (has_binding_enchant) {
      RequireTradeLuaAdapter(L).ShowSystemMessage(kErrBindingEnchant);
      return 0;
    }

    if (BlockContainerOrEquippedBag(L, *cursor_item, inventory)) {
      return 0;
    }
  }
  const auto previous_slot = trade.GetLocalPlayerTradeSlot(trade_slot);
  const auto *previous_item = ResolveLocalTradeItem(L, lua_index);

  if (cursor_item && !cursor_item->IsEmpty() && previous_slot &&
      previous_slot->item_guid == cursor_item->guid) {
    cursor->Clear();
    return 0;
  }

  if (!cursor_item || cursor_item->IsEmpty()) {
    if (!previous_slot || !previous_item) {
      cursor->Clear();
      return 0;
    }

    if (!adapter->interaction().SendClearTradeItem(trade_slot)) {
      return 0;
    }
    if (!trade.ClearLocalPlayerTradeSlot(trade_slot)) {
      return 0;
    }
    SetCursorToTradeItem(L, *previous_item, *previous_slot);
    adapter->Present(TradeLuaEvent::kPlayerItemChanged, lua_index);
    return 0;
  }

  if (!adapter->interaction().SendSetTradeItem(
          trade_slot, held_item->source_bag, held_item->source_slot)) {
    return 0;
  }

  if (!trade.SetLocalPlayerTradeSlot(
          trade_slot, cursor_item->guid, held_item->source_bag,
          held_item->source_slot)) {
    return 0;
  }

  if (previous_slot && previous_item) {
    SetCursorToTradeItem(L, *previous_item, *previous_slot);
  } else {
    cursor->Clear({
        .release_source_lease = true,
        .publish_money_owner_update = false,
    });
  }
  adapter->Present(TradeLuaEvent::kPlayerItemChanged, lua_index);
  return 0;
}

}
