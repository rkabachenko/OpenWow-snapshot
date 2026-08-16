
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/game/commerce/merchants/adapters/lua/merchant_lua_adapter.h"
#include "openwow/game/actions/held_cursor/held_cursor.h"

#include "openwow/game/actions/held_cursor/adapters/platform/cursor_surface.h"
#include "openwow/game/gossip_manager.h"
#include "openwow/game/hyperlink.h"
#include "openwow/game/interaction_sender.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/items/adapters/retail/item_display_name_formatter.h"
#include "openwow/game/inventory/items/item_interactions.h"
#include "openwow/game/inventory/items/item_use_requirements.h"
#include "openwow/game/inventory/items/item_trade_eligibility.h"
#include "openwow/game/localization.h"
#include "openwow/game/commerce/merchants/merchant_requirements.h"
#include "openwow/game/inventory/loot/loot_state.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/ui/game/api/game_lua_api_container_common.h"
#include "openwow/ui/game/merchant_repair_cost.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/surfaces/game/runtime/npc_interaction_controller.h"

extern "C" {
#include <lua.hpp>
}

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace openwow::ui::game::detail {

static const ::openwow::game::VendorList* GetVendor(lua_State* L);

namespace {

constexpr std::uint32_t kRetailBuyCursorType = 3u;
constexpr std::uint32_t kRetailUnableBuyCursorType = 0x1du;

constexpr std::uint8_t kRetailMerchantBuyBag = 1u;
constexpr std::uint8_t kRetailDefaultMerchantBuyQuantity = 1u;

struct VisibleBuybackEntry {
  std::uint32_t raw_slot = 0;
  const ::openwow::game::ItemInstance* item = nullptr;
  std::uint32_t price = 0;
};

::openwow::game::CursorSurface* GetCursorMgr(lua_State* L) {
  return RequireMerchantLuaAdapter(L).cursor();
}

::openwow::game::CursorSurface* GetMerchantHoverCursorMgr(lua_State* L) {
  auto* mgr = GetCursorMgr(L);
  if (mgr == nullptr) {
    return nullptr;
  }

  if (const auto* session = GetWorldSession(L); session != nullptr) {
    const auto targeting = session->spells().GetTargeting().GetState();
    if (targeting.isActive && targeting.spellId != 0u) {
      return nullptr;
    }
  }

  if (mgr->GetBaseCursorType() != ::openwow::game::CursorType::kDefault) {
    return nullptr;
  }

  return mgr;
}

std::uint32_t ResolveItemSellPrice(const MerchantLuaAdapter& adapter,
                                   const std::uint32_t entry) {
  if (entry == 0) {
    return 0;
  }

  if (const auto* item_template = adapter.query_cache().GetItemTemplate(entry)) {
    return item_template->sell_price;
  }

  return 0;
}

bool ItemCanShowSellCursor(const MerchantLuaAdapter& adapter,
                           const ::openwow::game::ItemInstance& item) {
  return !item.IsEmpty() && ResolveItemSellPrice(adapter, item.entry) != 0;
}

std::string ResolveDisplayNameWithRandomProperty(lua_State* L,
                                                 std::string display_name,
                                                 std::int32_t random_property_id) {
  return ::openwow::game::FormatItemDisplayNameWithRandomProperty(
      RequireMerchantLuaAdapter(L).localization(),
      RequireMerchantLuaAdapter(L).dbc(), display_name, random_property_id);
}

bool LocalPlayerCanUseItemTemplate(const MerchantLuaAdapter& adapter,
                                   const ::openwow::game::ItemTemplate& item_template) {
  return adapter.CanUseItem(item_template);
}

std::vector<VisibleBuybackEntry> BuildVisibleBuybackEntries(lua_State* L) {
  auto& adapter = RequireMerchantLuaAdapter(L);
  const auto* player = adapter.objects().GetActivePlayer();
  if (player == nullptr) {
    return {};
  }

  auto& inventory = adapter.inventory();
  std::vector<VisibleBuybackEntry> entries;
  entries.reserve(::openwow::game::PlayerInventoryReplica::kBuybackSlots);

  for (std::uint8_t slot = 0; slot < ::openwow::game::PlayerInventoryReplica::kBuybackSlots; ++slot) {

    const auto player_item_guid = player->GetGuidField(
        static_cast<std::uint16_t>(
            ::openwow::game::PLAYER_FIELD_VENDORBUYBACK_SLOT_1 + slot * 2u));
    if (player_item_guid.IsEmpty()) {
      continue;
    }

    const auto price = player->GetUInt32(
        static_cast<std::uint16_t>(::openwow::game::PLAYER_FIELD_BUYBACK_PRICE_1 + slot));
    if (price == 0) {
      continue;
    }

    const auto* item = inventory.GetBuybackSlot(slot);
    entries.push_back(
        {static_cast<std::uint32_t>(::openwow::game::InventorySlots::kBuybackStart + slot),
         item,
         price});
  }

  if (!entries.empty()) {
    std::qsort(
        entries.data(),
        entries.size(),
        sizeof(VisibleBuybackEntry),
        [](const void* lhs, const void* rhs) -> int {
          const auto* lhs_entry =
              static_cast<const VisibleBuybackEntry*>(lhs);
          const auto* rhs_entry =
              static_cast<const VisibleBuybackEntry*>(rhs);
          if (lhs_entry->price < rhs_entry->price) {
            return -1;
          }
          if (lhs_entry->price > rhs_entry->price) {
            return 1;
          }
          return 0;
        });
  }

  return entries;
}

std::uint32_t ReadVisibleBuybackOneBasedIndex(lua_State* L,
                                              const int argument_index,
                                              const char* usage_message) {
  if (lua_isnumber(L, argument_index) == 0) {
    luaL_error(L, usage_message);
  }

  return ::openwow::ui::SaturateLuaNumberToU32(
      lua_tonumber(L, argument_index));
}

std::int32_t ReadMerchantZeroBasedIndex(lua_State* L,
                                        const int argument_index,
                                        const char* usage_message) {
  if (lua_isnumber(L, argument_index) == 0) {
    luaL_error(L, usage_message);
  }

  return ::openwow::ui::SignedI32FromU32Bits(
      static_cast<std::uint32_t>(
          TruncateLuaNumberToSseI32(lua_tonumber(L, argument_index))) -
      1u);
}

void PushEmptyBuybackInfo(lua_State* L) {
  lua_pushnil(L);
  lua_pushnil(L);
  lua_pushnumber(L, 0);
  lua_pushnumber(L, 1.0);
  lua_pushnumber(L, 0);
  lua_pushnumber(L, 1.0);
}

::openwow::game::AsyncQueryChannel::CallbackKey MerchantUpdateCallbackKey() {
  static const char tag = 0;
  return ::openwow::game::AsyncQueryChannel::CallbackKey(
      reinterpret_cast<std::uintptr_t>(&tag), 0);
}

void FireMerchantUpdateEvent(MerchantLuaAdapter& adapter) {
  adapter.PresentMerchantUpdated();
}

const ::openwow::game::ItemTemplate* GetOrRequestTemplateAndFireMerchantUpdate(
    lua_State* L, const std::uint32_t item_entry) {
  auto& adapter = RequireMerchantLuaAdapter(L);
  if (item_entry == 0) {
    return nullptr;
  }

  if (const auto* item_template = adapter.query_cache().GetItemTemplate(item_entry);
      item_template != nullptr) {
    return item_template;
  }

  return adapter.query_cache().GetOrRequestItemTemplate(
      item_entry,
      ::openwow::game::QueryCache::QueryRequestOptions{
          .callback_key = MerchantUpdateCallbackKey(),
          .dedupe_callbacks = true,
          .callback = [&adapter](const bool ) {
            FireMerchantUpdateEvent(adapter);
          }});
}

const ::openwow::game::ItemTemplate*
GetOrRequestMerchantItemInfoTemplate(lua_State* L,
                                     const std::uint32_t item_entry) {
  auto& adapter = RequireMerchantLuaAdapter(L);
  if (item_entry == 0) {
    return nullptr;
  }

  if (const auto* item_template = adapter.query_cache().GetItemTemplate(item_entry);
      item_template != nullptr) {
    return item_template;
  }

  if (!adapter.gossip().merchant().BeginItemInfoRefresh(item_entry)) {
    return nullptr;
  }

  const auto* item_template = adapter.query_cache().GetOrRequestItemTemplate(
      item_entry,
      ::openwow::game::QueryCache::QueryRequestOptions{
          .callback_key =
              ::openwow::game::MerchantInteraction::ItemInfoRefreshCallbackKey(),
          .dedupe_callbacks = true,
          .callback = [&adapter](const bool ) {
            if (adapter.gossip().merchant().FinishItemInfoRefresh()) {
              FireMerchantUpdateEvent(adapter);
            }
          }});

  if (item_template != nullptr) {
    (void)adapter.gossip().merchant().FinishItemInfoRefresh();
  }

  return item_template;
}

std::int32_t ReadMerchantItemInfoZeroBasedIndex(lua_State* L) {
  return ReadMerchantZeroBasedIndex(
      L, 1, "Usage: GetMerchantItemInfo(index)");
}

}

static const ::openwow::game::VendorList* GetVendor(lua_State* L) {
  auto& merchant = RequireMerchantLuaAdapter(L).gossip().merchant();
  if (!merchant.active()) return nullptr;
  return &merchant.snapshot();
}

static const ::openwow::game::VendorItem* GetVendorItem(lua_State* L,
                                                        const int index_arg,
                                                        const char* usage_message) {
  const auto index =
      ReadMerchantZeroBasedIndex(L, index_arg, usage_message);
  const auto* vl = GetVendor(L);
  if (!vl) return nullptr;
  if (index < 0 || index >= static_cast<std::int32_t>(vl->items.size())) {
    return nullptr;
  }
  return &vl->items[static_cast<std::size_t>(index)];
}

static const ::openwow::game::VendorItem* GetVendorItemByZeroBasedIndex(
    const ::openwow::game::VendorList* vendor,
    const std::int32_t zero_based_index) {
  if (vendor == nullptr || zero_based_index < 0 ||
      zero_based_index >= static_cast<std::int32_t>(vendor->items.size())) {
    return nullptr;
  }

  return &vendor->items[static_cast<std::size_t>(zero_based_index)];
}

static const ::openwow::game::VendorItem* FindVendorItemByItemId(
    const ::openwow::game::VendorList& vendor,
    const std::uint32_t item_id) {
  for (const auto& vendor_item : vendor.items) {
    if (vendor_item.item_id == item_id) {
      return &vendor_item;
    }
  }

  return nullptr;
}

std::int32_t ReadMerchantTooltipIndex(lua_State* L,
                                      const int argument,
                                      const char* error_message) {
  return ReadMerchantZeroBasedIndex(L, argument, error_message);
}

const ::openwow::data::dbc::ItemExtendedCostEntry* LookupExtendedCostEntry(
    lua_State* L,
    const ::openwow::game::VendorItem& vendor_item) {
  if (vendor_item.extended_cost == 0) {
    return nullptr;
  }

  const auto* dbc = RequireMerchantLuaAdapter(L).dbc();
  if (dbc == nullptr) {
    return nullptr;
  }

  return dbc->item_extended_cost().LookupEntry(vendor_item.extended_cost);
}

bool PlayerMeetsMerchantCursorRequirements(
    lua_State* L, const MerchantLuaAdapter& adapter,
    const ::openwow::game::CGPlayer_C& player,
    const ::openwow::game::VendorItem& vendor_item) {
  if (player.GetUInt32(::openwow::game::PLAYER_FIELD_COINAGE) <
      vendor_item.price) {
    return false;
  }

  if (const auto* item_template =
          adapter.query_cache().GetItemTemplate(vendor_item.item_id);
      item_template != nullptr && item_template->required_skill != 0u) {
    if (item_template->required_skill >
            std::numeric_limits<std::uint16_t>::max() ||
        player.GetSkillValueWithStepModifier(
            static_cast<std::uint16_t>(item_template->required_skill)) <
            item_template->required_skill_rank) {
      return false;
    }
  }

  const auto* extended_cost = LookupExtendedCostEntry(L, vendor_item);
  if (extended_cost == nullptr) {
    return true;
  }

  if (player.GetHonorPoints() < extended_cost->honor_points ||
      player.GetArenaPoints() < extended_cost->arena_points) {
    return false;
  }

  const auto& inventory = adapter.inventory();
  for (std::size_t index = 0; index < extended_cost->item_id.size(); ++index) {
    if (extended_cost->item_id[index] != 0u &&
        extended_cost->item_count[index] != 0u &&
        inventory.CountDefaultPlayerItemsOfEntry(
            extended_cost->item_id[index]) < extended_cost->item_count[index]) {
      return false;
    }
  }

  return extended_cost->personal_arena_rating == 0u ||
         extended_cost->arena_slot >= 3u ||
         ::openwow::game::MeetsMerchantArenaRatingRequirement(
             *extended_cost, adapter.arena_team_query());
}

std::uint32_t ResolveExtendedCostTooltipItemId(
    const ::openwow::data::dbc::ItemExtendedCostEntry& extended_cost,
    const std::int32_t visible_cost_index) {
  if (visible_cost_index < 0) {
    return 0;
  }

  std::int32_t current_visible_index = 0;
  for (std::size_t real_index = 0; real_index < extended_cost.item_id.size();
       ++real_index) {
    if (extended_cost.item_id[real_index] == 0) {
      continue;
    }

    if (current_visible_index == visible_cost_index) {
      return extended_cost.item_id[real_index];
    }

    ++current_visible_index;
  }

  return 0;
}

struct RefundItemSelection {
  const ::openwow::game::ItemInstance* item = nullptr;
  ::openwow::game::RefundQuote refund_info{};
  std::uint32_t remaining_seconds = 0;
};

const ::openwow::game::ItemInstance* ResolveRefundItemSelection(
    const MerchantLuaAdapter& adapter,
    bool is_equipped, int bag,
    int slot) {
  auto& inventory = adapter.inventory();
  if (slot < 1) {
    return nullptr;
  }

  if (is_equipped) {
    if (adapter.objects().GetActivePlayer() == nullptr ||
        slot > static_cast<int>(::openwow::game::PlayerInventoryReplica::kMaxEquipSlots)) {
      return nullptr;
    }

    return inventory.GetEquipSlot(static_cast<std::uint8_t>(slot - 1));
  }

  return detail::ResolveLuaContainerItem(
      inventory, adapter.objects().GetActivePlayer() != nullptr,
      adapter.world_session().bank_npc_guid() != 0, bag, slot - 1);
}

bool ItemHasRefundBlockingModification(
    const ::openwow::game::ItemInstance& item) {
  return ::openwow::game::ItemHasRefundBlockingEnchantmentState(item);
}

bool RefundItemMatchesActiveLootSource(
    const MerchantLuaAdapter& adapter,
    const ::openwow::game::ItemInstance& item) {
  const auto& loot = adapter.loot();
  return loot.is_looting() &&
         loot.loot_window().source_guid.GetRawValue() == item.guid;
}

std::optional<std::uint32_t> ResolveRefundRemainingSeconds(
    const MerchantLuaAdapter& adapter,
    const ::openwow::game::ItemInstance& item) {
  return ::openwow::game::ResolveBoundTradeWindowRemainingSeconds(
      item.create_played_time, adapter.total_played_time());
}

bool HasRefundSnapshot(lua_State* L, const std::uint64_t item_guid) {
  return item_guid != 0 &&
         RequireMerchantLuaAdapter(L)
             .item_interactions()
             .refund_quote(::openwow::game::ObjectGuid(item_guid)) != nullptr;
}

std::optional<RefundItemSelection> ResolveRefundItemContext(
    lua_State* L, bool is_equipped, int bag, int slot) {

  auto* adapter_ptr = TryMerchantLuaAdapter(L);
  if (adapter_ptr == nullptr) {
    return std::nullopt;
  }
  auto& adapter = *adapter_ptr;

  const auto* item =
      ResolveRefundItemSelection(adapter, is_equipped, bag, slot);
  if (item == nullptr || item->guid == 0 ||
      RefundItemMatchesActiveLootSource(adapter, *item)) {
    return std::nullopt;
  }

  if (ItemHasRefundBlockingModification(*item)) {
    return std::nullopt;
  }

  const auto* refund_info =
      adapter.item_interactions().refund_quote(
          ::openwow::game::ObjectGuid(item->guid));
  const auto remaining_seconds = ResolveRefundRemainingSeconds(adapter, *item);
  if (refund_info == nullptr || !remaining_seconds.has_value()) {
    return std::nullopt;
  }

  return RefundItemSelection{item, *refund_info,
                             std::min(*remaining_seconds,
                                      refund_info->time_left)};
}

std::size_t CountRequiredRefundItems(
    const ::openwow::game::RefundQuote& refund_info) {
  std::size_t count = 0;
  for (const auto& required_item : refund_info.required_items) {
    if (required_item.item_id != 0) {
      ++count;
    }
  }
  return count;
}

const ::openwow::game::RefundCost* FindRequiredRefundItem(
    const ::openwow::game::RefundQuote& refund_info,
    int one_based_item_index) {
  if (one_based_item_index <= 0) {
    return nullptr;
  }

  int current_index = 0;
  for (const auto& required_item : refund_info.required_items) {
    if (required_item.item_id == 0) {
      continue;
    }

    ++current_index;
    if (current_index == one_based_item_index) {
      return &required_item;
    }
  }

  return nullptr;
}

std::optional<std::string> BuildRefundItemLink(lua_State* L,
                                               std::uint32_t item_entry) {
  auto& adapter = RequireMerchantLuaAdapter(L);

  const auto* item_template = adapter.query_cache().GetItemTemplate(item_entry);
  if (item_template == nullptr) {
    return std::nullopt;
  }

  const auto color_info =
      ::openwow::game::ItemTemplate::GetQualityColorInfo(
          static_cast<std::uint32_t>(item_template->quality));
  std::string link = std::string(color_info.hyperlink_color) + "|Hitem:" +
                     std::to_string(item_entry) +
                     ":0:0:0:0:0:0:0|h[" + item_template->name + "]|h|r";
  return link;
}

int LuaGetMerchantNumItems(lua_State* L) {
  const auto* vl = GetVendor(L);
  lua_pushnumber(L, vl ? static_cast<lua_Integer>(vl->items.size()) : 0);
  return 1;
}

int LuaGetMerchantItemInfo(lua_State* L) {
  const auto index = ReadMerchantItemInfoZeroBasedIndex(L);
  const auto* item = GetVendorItemByZeroBasedIndex(GetVendor(L), index);
  if (!item) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 1.0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 1.0);
    lua_pushnil(L);
    return 7;
  }

  auto& adapter = RequireMerchantLuaAdapter(L);
  const auto* item_template = GetOrRequestMerchantItemInfoTemplate(L, item->item_id);
  const auto* extended_cost_entry = LookupExtendedCostEntry(L, *item);
  if (item_template != nullptr) {
    lua_pushstring(L, item_template->name.c_str());
  } else {
    lua_pushnil(L);
  }

  const auto texture =
      ResolveItemDisplayIdIconTexturePathOrFallback(L, item->display_info_id);
  lua_pushstring(L, texture.c_str());
  lua_pushnumber(L, static_cast<lua_Integer>(item->price));
  lua_pushnumber(L, static_cast<lua_Integer>(item->buy_count));
  lua_pushnumber(L, static_cast<lua_Integer>(item->max_count));
  lua_pushwowbool(
      L,
      (extended_cost_entry == nullptr ||
       ::openwow::game::MeetsMerchantArenaRatingRequirement(
           *extended_cost_entry, adapter.arena_team_query())) &&
          (item_template == nullptr ||
           LocalPlayerCanUseItemTemplate(adapter, *item_template)));
  lua_pushwowbool(L, extended_cost_entry != nullptr);
  return 7;
}

int LuaGetMerchantItemLink(lua_State* L) {
  const auto* item =
      GetVendorItem(L, 1, "Usage: GetMerchantItemLink(index)");
  if (!item) {
    return 0;
  }

  auto& adapter = RequireMerchantLuaAdapter(L);

  const auto* item_template = adapter.query_cache().GetItemTemplate(item->item_id);
  if (item_template == nullptr) {
    (void)adapter.query_cache().GetOrRequestItemTemplate(item->item_id);
    return 0;
  }

  const auto link = ::openwow::game::HyperlinkParser::BuildItemLink(
      item->item_id, item_template->name,
      static_cast<std::uint32_t>(item_template->quality));
  lua_pushstring(L, link.c_str());
  return 1;
}

int LuaGetMerchantItemMaxStack(lua_State* L) {
  const auto* item =
      GetVendorItem(L, 1, "Usage: GetMerchantItemMaxStack(index)");
  if (item != nullptr && item->buy_count <= 1) {
    auto& adapter = RequireMerchantLuaAdapter(L);
    if (item->item_id != 0) {
      if (const auto* item_template =
              adapter.query_cache().GetItemTemplate(item->item_id);
          item_template != nullptr) {
        lua_pushnumber(L, static_cast<lua_Integer>(item_template->stackable));
        return 1;
      }

      (void)adapter.query_cache().GetOrRequestItemTemplate(item->item_id);
    }
  }
  lua_pushnumber(L, 1);
  return 1;
}

int LuaGetMerchantItemCostInfo(lua_State* L) {
  const auto* item =
      GetVendorItem(L, 1, "Usage: GetMerchantItemCostInfo(index)");
  const auto* extended_cost =
      item != nullptr ? LookupExtendedCostEntry(L, *item) : nullptr;

  std::uint32_t item_cost_count = 0;
  if (extended_cost != nullptr) {
    item_cost_count = static_cast<std::uint32_t>(std::count_if(
        extended_cost->item_id.begin(), extended_cost->item_id.end(),
        [](const std::uint32_t item_id) { return item_id != 0; }));
    lua_pushnumber(L, static_cast<lua_Integer>(extended_cost->honor_points));
    lua_pushnumber(L, static_cast<lua_Integer>(extended_cost->arena_points));
  } else {
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
  }

  lua_pushnumber(L, static_cast<lua_Integer>(item_cost_count));
  return 3;
}

int LuaGetMerchantItemCostItem(lua_State* L) {
  const auto* item = GetVendorItem(
      L, 1, "Usage: GetMerchantItemCostItem(index, itemIndex)");
  const auto cost_index = ReadMerchantTooltipIndex(
      L, 2, "Usage: GetMerchantItemCostItem(index, itemIndex)");
  const auto* extended_cost =
      item != nullptr ? LookupExtendedCostEntry(L, *item) : nullptr;
  if (extended_cost == nullptr) {
    lua_pushnil(L);
    lua_pushnumber(L, 0);
    return 2;
  }

  const auto cost_item_id =
      ResolveExtendedCostTooltipItemId(*extended_cost, cost_index);
  if (cost_item_id == 0) {
    lua_pushnil(L);
    lua_pushnumber(L, 0);
    return 2;
  }

  const auto item_it = std::find(extended_cost->item_id.begin(),
                                 extended_cost->item_id.end(), cost_item_id);
  const auto item_offset =
      static_cast<std::size_t>(item_it - extended_cost->item_id.begin());
  const auto cost_count = item_offset < extended_cost->item_count.size()
                              ? extended_cost->item_count[item_offset]
                              : 0u;

  const auto* item_template =
      GetOrRequestTemplateAndFireMerchantUpdate(L, cost_item_id);
  if (item_template == nullptr) {
    lua_pushnil(L);
    lua_pushnumber(L, static_cast<lua_Integer>(cost_count));
    return 2;
  }

  const auto texture =
      ResolveItemEntryIconTexturePathOrFallback(L, cost_item_id);
  const auto link = ::openwow::game::HyperlinkParser::BuildItemLink(
      cost_item_id, item_template->name,
      static_cast<std::uint32_t>(item_template->quality));
  lua_pushstring(L, texture.c_str());
  lua_pushnumber(L, static_cast<lua_Integer>(cost_count));
  lua_pushstring(L, link.c_str());
  return 3;
}

int LuaSetMerchantItem(lua_State* L) {
  luaL_checktype(L, 1, LUA_TTABLE);

  const auto* vendor = GetVendor(L);
  const auto slot_index =
      ReadMerchantTooltipIndex(L, 2, "Invalid merchant slot in SetMerchantItem");
  const auto* vendor_item = GetVendorItemByZeroBasedIndex(vendor, slot_index);
  if (vendor_item == nullptr || vendor_item->item_id == 0) {
    return 0;
  }

  const auto* tooltip_item = FindVendorItemByItemId(*vendor, vendor_item->item_id);
  if (tooltip_item == nullptr) {
    return 0;
  }

  RequireMerchantLuaAdapter(L).ShowItemTooltip(tooltip_item->item_id);
  return 0;
}

int LuaSetMerchantCostItem(lua_State* L) {
  luaL_checktype(L, 1, LUA_TTABLE);

  const auto* vendor = GetVendor(L);
  const auto slot_index = ReadMerchantTooltipIndex(
      L, 2, "Invalid merchant slot in SetMerchantCostItem");
  const auto cost_index = ReadMerchantTooltipIndex(
      L, 3, "Invalid merchant slot in SetMerchantCostItem");
  const auto* vendor_item = GetVendorItemByZeroBasedIndex(vendor, slot_index);
  if (vendor_item == nullptr || vendor_item->item_id == 0) {
    return 0;
  }

  const auto* extended_cost = LookupExtendedCostEntry(L, *vendor_item);
  if (extended_cost == nullptr) {
    return 0;
  }

  const auto cost_item_id =
      ResolveExtendedCostTooltipItemId(*extended_cost, cost_index);
  if (cost_item_id == 0) {
    return 0;
  }

  RequireMerchantLuaAdapter(L).ShowItemTooltip(cost_item_id);
  return 0;
}

int LuaBuyMerchantItem(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: BuyMerchantItem(index)");
  }

  auto& adapter = RequireMerchantLuaAdapter(L);
  if (adapter.objects().GetActivePlayer() == nullptr) {
    return 0;
  }

  if (adapter.held_cursor() != nullptr) {
    adapter.held_cursor()->Clear();
  }

  const auto zero_based_index = ReadMerchantZeroBasedIndex(
      L, 1, "Usage: BuyMerchantItem(index)");
  std::uint8_t quantity = kRetailDefaultMerchantBuyQuantity;
  if (lua_isnumber(L, 2)) {
    const auto requested_quantity =
        TruncateLuaNumberToSseI32(lua_tonumber(L, 2));
    if (requested_quantity >= kRetailDefaultMerchantBuyQuantity) {
      quantity = static_cast<std::uint8_t>(
          std::min(requested_quantity, static_cast<int>(std::numeric_limits<std::uint8_t>::max())));
    }
  }

  const auto* vl = GetVendor(L);
  if (vl == nullptr) {
    return 0;
  }
  const auto* item = GetActiveVendorItemByZeroBasedIndex(vl, zero_based_index);

  if (item == nullptr || item->slot == 0) {
    return 0;
  }

  adapter.interaction().SendBuyItem(
      vl->vendor_guid.GetRawValue(),
      item->item_id, item->slot,
      static_cast<std::uint32_t>(quantity), kRetailMerchantBuyBag);
  return 0;
}

int LuaGetBuybackItemInfo(lua_State* L) {
  const auto one_based_index =
      ReadVisibleBuybackOneBasedIndex(L, 1, "Usage: GetBuybackItemInfo(slot)");
  const auto entries = BuildVisibleBuybackEntries(L);
  if (one_based_index == 0 || one_based_index > entries.size()) {
    PushEmptyBuybackInfo(L);
    return 6;
  }

  auto& adapter = RequireMerchantLuaAdapter(L);
  const auto& entry = entries[one_based_index - 1];
  if (entry.item == nullptr) {
    PushEmptyBuybackInfo(L);
    return 6;
  }

  const auto* item_template =
      GetOrRequestTemplateAndFireMerchantUpdate(L, entry.item->entry);
  if (item_template == nullptr) {
    PushEmptyBuybackInfo(L);
    return 6;
  }

  const auto display_name = ResolveDisplayNameWithRandomProperty(
      L, item_template->name, entry.item->random_property);
  const auto texture = ResolveItemEntryIconTexturePathOrFallback(L, entry.item->entry);

  lua_pushstring(L, display_name.c_str());
  lua_pushstring(L, texture.c_str());
  lua_pushnumber(L, static_cast<lua_Number>(entry.price));
  lua_pushnumber(L, static_cast<lua_Number>(entry.item->count));
  lua_pushnumber(L, 0.0);
  lua_pushwowbool(L, LocalPlayerCanUseItemTemplate(adapter, *item_template));
  return 6;
}

int LuaGetBuybackItemLink(lua_State* L) {
  const auto one_based_index =
      ReadVisibleBuybackOneBasedIndex(L, 1, "Usage: GetBuybackItemLink(slot)");
  const auto entries = BuildVisibleBuybackEntries(L);
  if (one_based_index == 0 || one_based_index > entries.size()) {
    return 0;
  }

  auto& adapter = RequireMerchantLuaAdapter(L);
  const auto& entry = entries[one_based_index - 1];
  if (entry.item == nullptr) {
    return 0;
  }

  const auto* item_template =
      adapter.query_cache().GetItemTemplate(entry.item->entry);
  if (item_template == nullptr) {
    return 0;
  }

  const auto display_name = ResolveDisplayNameWithRandomProperty(
      L, item_template->name, entry.item->random_property);
  const auto quality =
      entry.item->quality != 0
          ? static_cast<std::uint32_t>(entry.item->quality)
          : static_cast<std::uint32_t>(item_template->quality);
  const auto link = ::openwow::game::HyperlinkParser::BuildItemLink(
      entry.item->entry,
      display_name,
      quality,
      static_cast<std::int32_t>(entry.item->GetPermanentEnchant()),
      static_cast<std::int32_t>(entry.item->GetSocketEnchant(0)),
      static_cast<std::int32_t>(entry.item->GetSocketEnchant(1)),
      static_cast<std::int32_t>(entry.item->GetSocketEnchant(2)),
      entry.item->random_property,
      static_cast<std::int32_t>(entry.item->random_suffix));
  lua_pushstring(L, link.c_str());
  return 1;
}

int LuaBuybackItem(lua_State* L) {
  const auto one_based_index =
      ReadVisibleBuybackOneBasedIndex(L, 1, "Usage: BuybackItem(slot)");
  auto& adapter = RequireMerchantLuaAdapter(L);
  const auto* vl = GetVendor(L);
  if (!vl) return 0;

  const auto entries = BuildVisibleBuybackEntries(L);
  if (one_based_index == 0 || one_based_index > entries.size()) return 0;

  adapter.interaction().SendBuybackItem(
      vl->vendor_guid.GetRawValue(),
      entries[one_based_index - 1].raw_slot);
  return 0;
}

int LuaRepairAllItems(lua_State* L) {
  auto& adapter = RequireMerchantLuaAdapter(L);
  const auto* player = adapter.objects().GetActivePlayer();
  if (player == nullptr) {
    return 0;
  }

  const auto repair = adapter.repair_quote();
  if (!repair.has_value()) {
    return 0;
  }

  const bool guild = lua_gettop(L) >= 1 && lua_toboolean(L, 1);
  if (!guild &&
      player->GetUInt32(::openwow::game::PLAYER_FIELD_COINAGE) <
          repair->total_cost) {
    DisplaySystemMessage(0x28);
    return 0;
  }

  adapter.interaction().SendRepairItem(repair->vendor_guid, 0, guild);
  return 0;
}

int LuaCanMerchantRepair(lua_State* L) {
  lua_pushwowbool(
      L, RequireMerchantLuaAdapter(L).repair_quote().has_value());
  return 1;
}

int LuaCloseMerchant(lua_State* L) {
  RequireMerchantLuaAdapter(L).CloseMerchant();
  return 0;
}

int LuaGetNumBuybackItems(lua_State* L) {
  if (GetVendor(L) == nullptr) {
    lua_pushnumber(L, 0.0);
    return 1;
  }
  lua_pushnumber(L, static_cast<lua_Number>(BuildVisibleBuybackEntries(L).size()));
  return 1;
}

int LuaGetContainerItemPurchaseInfo(lua_State* L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(
        L, "Usage: GetContainerItemPurchaseInfo(index, slot[, isEquipped])");
  }

  const int bag = static_cast<int>(lua_tonumber(L, 1));
  const int slot = static_cast<int>(lua_tonumber(L, 2));
  const bool is_equipped = lua_gettop(L) >= 3 && lua_toboolean(L, 3) != 0;

  const auto context = ResolveRefundItemContext(L, is_equipped, bag, slot);
  if (!context.has_value()) {
    return 0;
  }

  lua_pushnumber(L, static_cast<lua_Number>(context->refund_info.money));
  lua_pushnumber(L, static_cast<lua_Number>(context->refund_info.honor));
  lua_pushnumber(L, static_cast<lua_Number>(context->refund_info.arena));
  lua_pushnumber(L, static_cast<lua_Number>(
                        CountRequiredRefundItems(context->refund_info)));
  lua_pushnumber(L, static_cast<lua_Number>(context->remaining_seconds));
  return 5;
}

int LuaContainerItemPurchaseItem(lua_State* L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2) || !lua_isnumber(L, 3)) {
    return luaL_error(
        L, "Usage: ContainerItemPurchaseItem(bag, slot, itemIndex[, isEquipped])");
  }

  const int bag = static_cast<int>(lua_tonumber(L, 1));
  const int slot = static_cast<int>(lua_tonumber(L, 2));
  const int item_index = static_cast<int>(lua_tonumber(L, 3));
  const bool is_equipped = lua_gettop(L) >= 4 && lua_toboolean(L, 4) != 0;

  const auto context = ResolveRefundItemContext(L, is_equipped, bag, slot);
  if (!context.has_value()) {
    return 0;
  }

  const auto* required_item =
      FindRequiredRefundItem(context->refund_info, item_index);
  if (required_item == nullptr) {
    return 0;
  }

  if (const auto icon_path =
          TryResolveItemEntryIconTexturePath(L, required_item->item_id);
      icon_path.has_value()) {
    lua_pushstring(L, icon_path->c_str());
  } else {
    lua_pushnil(L);
  }
  lua_pushnumber(L, static_cast<lua_Number>(required_item->count));

  if (const auto item_link = BuildRefundItemLink(L, required_item->item_id);
      item_link.has_value()) {
    lua_pushstring(L, item_link->c_str());
  } else {
    lua_pushnil(L);
  }

  return 3;
}

int LuaContainerRefundItemPurchase(lua_State* L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(
        L, "Usage: ContainerRefundItemPurchase(index, slot[, isEquipped])");
  }

  auto& adapter = RequireMerchantLuaAdapter(L);
  const int bag = static_cast<int>(lua_tonumber(L, 1));
  const int slot = static_cast<int>(lua_tonumber(L, 2));
  const bool is_equipped = lua_gettop(L) >= 3 && lua_toboolean(L, 3) != 0;
  const auto* item =
      ResolveRefundItemSelection(adapter, is_equipped, bag, slot);
  if (item == nullptr || !HasRefundSnapshot(L, item->guid)) {
    DisplaySystemMessage(12);
    return 0;
  }

  (void)adapter.item_interactions().begin_refund_request(
      ::openwow::game::ObjectGuid(item->guid));
  if (!adapter.interaction().SendItemRefund(item->guid)) {
    DisplaySystemMessage(12);
  }
  return 0;
}

int LuaShowBuybackSellCursor(lua_State* L) {
  auto* mgr = GetMerchantHoverCursorMgr(L);
  if (mgr == nullptr) {
    return 0;
  }

  const auto one_based_index =
      ReadVisibleBuybackOneBasedIndex(L, 1, "Usage: ShowBuybackSellCursor(index)");
  const auto entries = BuildVisibleBuybackEntries(L);
  if (one_based_index == 0 || one_based_index > entries.size()) {
    return 0;
  }

  const auto* player =
      RequireMerchantLuaAdapter(L).objects().GetActivePlayer();
  if (player == nullptr) {
    return 0;
  }

  const auto& entry = entries[one_based_index - 1];
  if (player->GetUInt32(::openwow::game::PLAYER_FIELD_COINAGE) >= entry.price) {
    mgr->SetImmediateCursorType(kRetailBuyCursorType);
  } else {
    mgr->SetImmediateCursorType(kRetailUnableBuyCursorType);
  }
  return 0;
}

int LuaShowInventorySellCursor(lua_State* L) {
  auto* mgr = GetMerchantHoverCursorMgr(L);
  auto& adapter = RequireMerchantLuaAdapter(L);
  if (mgr == nullptr) {
    return 0;
  }

  if (adapter.objects().GetActivePlayer() == nullptr) {
    return 0;
  }

  int slot = 0;
  if (!ResolveInventorySlotArgument(L, 1, &slot) || slot == -1) {
    return 0;
  }

  const auto* item = adapter.inventory().GetItemInSlot(
      static_cast<std::uint8_t>(slot));
  if (item == nullptr || !ItemCanShowSellCursor(adapter, *item)) {
    return 0;
  }

  mgr->SetImmediateCursorType(kRetailBuyCursorType);
  return 0;
}

int LuaShowContainerSellCursor(lua_State* L) {
  auto* mgr = GetMerchantHoverCursorMgr(L);
  if (mgr == nullptr) {
    return 0;
  }

  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: ShowContainerSellCursor(index, slot)");
  }

  int bag = static_cast<int>(lua_tonumber(L, 1));
  int slot = static_cast<int>(lua_tonumber(L, 2)) - 1;

  auto& adapter = RequireMerchantLuaAdapter(L);
  auto& inv = adapter.inventory();
  const auto* item = inv.GetContainerSlot(
      static_cast<std::uint8_t>(bag), static_cast<std::uint8_t>(slot));
  if (item == nullptr || item->IsEmpty()) {
    return 0;
  }

  if (!ItemCanShowSellCursor(adapter, *item)) {
    return 0;
  }

  mgr->SetImmediateCursorType(kRetailBuyCursorType);
  return 0;
}

int LuaShowMerchantSellCursor(lua_State* L) {
  auto* mgr = GetMerchantHoverCursorMgr(L);
  if (mgr == nullptr) {
    return 0;
  }

  if (lua_isnumber(L, 1) == 0) {
    return luaL_error(L, "Usage: ShowMerchantSellCursor(index)");
  }

  const auto zero_based_index = ::openwow::ui::SignedI32FromU32Bits(
      static_cast<std::uint32_t>(
          TruncateLuaNumberToSseI32(lua_tonumber(L, 1))) -
      1u);
  const auto* vendor_item =
      GetVendorItemByZeroBasedIndex(GetVendor(L), zero_based_index);
  auto& adapter = RequireMerchantLuaAdapter(L);
  const auto* player = adapter.objects().GetActivePlayer();
  if (vendor_item == nullptr || player == nullptr) {
    return 0;
  }

  mgr->SetImmediateCursorType(PlayerMeetsMerchantCursorRequirements(
                                  L, adapter, *player, *vendor_item)
                                  ? kRetailBuyCursorType
                                  : kRetailUnableBuyCursorType);
  return 0;
}

}
