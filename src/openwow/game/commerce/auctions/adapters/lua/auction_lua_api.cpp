
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/runtime/security/protected_action_gate.h"
#include "openwow/game/actions/held_cursor/held_cursor.h"
#include "openwow/game/commerce/auctions/adapters/lua/auction_lua_adapter.h"
#include "openwow/game/commerce/auctions/adapters/lua/auction_lua_api.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/core/storm_string.h"
#include "openwow/game/action_validation_utils.h"
#include "openwow/game/commerce/auctions/auction_interaction.h"
#include "openwow/game/commerce/auctions/auction_pricing.h"
#include "openwow/game/commerce/auctions/auction_state.h"
#include "openwow/game/game_misc_utils.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/items/item_use_requirements.h"
#include "openwow/game/inventory/operations/player_item_packet_location.h"
#include "openwow/game/query_cache.h"
#include "openwow/ui/game/cursor_texture_resolver.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/game_ui_core.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/game/loot_tooltip_support.h"
#include "openwow/ui/lua_numeric.h"
#include "openwow/ui/lua_result_capacity.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace openwow::ui::game::detail {

namespace {

openwow::game::AuctionState& RequireAuctionState(lua_State* L) {
  return RequireAuctionLuaAdapter(L).auction().state();
}

struct AuctionItemClassDescriptor {
  std::uint32_t class_id;
  bool has_subclass_filters;
};

struct AuctionInvTypeDescriptor {
  std::uint32_t inventory_type;
  const char* token;
  bool allow_for_first_armor_subclass;
  bool deny_for_other_armor_subclasses;
};

struct AuctionCollectedSource {
  std::uint64_t item_guid = 0;
  std::uint32_t available_count = 0;
};

constexpr std::size_t kAuctionMultiSellMaxTrackedSources = 200;

constexpr std::array<AuctionItemClassDescriptor, 12> kAuctionItemClasses = {{
    {2, true},
    {4, true},
    {1, true},
    {0, true},
    {16, true},
    {7, true},
    {6, true},
    {11, true},
    {9, true},
    {3, true},
    {15, true},
    {12, false},
}};

constexpr std::array<AuctionInvTypeDescriptor, 14> kAuctionInvTypes = {{
    {1, "INVTYPE_HEAD", true, false},
    {2, "INVTYPE_NECK", true, true},
    {3, "INVTYPE_SHOULDER", false, false},
    {4, "INVTYPE_BODY", true, true},
    {5, "INVTYPE_CHEST", false, false},
    {6, "INVTYPE_WAIST", false, false},
    {7, "INVTYPE_LEGS", false, false},
    {8, "INVTYPE_FEET", false, false},
    {9, "INVTYPE_WRIST", false, false},
    {10, "INVTYPE_HAND", false, false},
    {11, "INVTYPE_FINGER", true, true},
    {12, "INVTYPE_TRINKET", true, true},
    {16, "INVTYPE_CLOAK", false, true},
    {23, "INVTYPE_HOLDABLE", true, true},
}};

const openwow::data::dbc::DbcLoader* GetAuctionDbcLoader(lua_State* L) {
  return RequireAuctionLuaAdapter(L).dbc();
}

const AuctionItemClassDescriptor* GetAuctionItemClassDescriptorByIndex(
    int class_index) {
  if (class_index < 1 ||
      class_index > static_cast<int>(kAuctionItemClasses.size())) {
    return nullptr;
  }
  return &kAuctionItemClasses[static_cast<std::size_t>(class_index - 1)];
}

const AuctionItemClassDescriptor* GetAuctionSubClassFilterDescriptor(
    int class_index) {
  const auto* descriptor = GetAuctionItemClassDescriptorByIndex(class_index);
  if (!descriptor || !descriptor->has_subclass_filters) {
    return nullptr;
  }
  return descriptor;
}

const openwow::data::dbc::ItemSubClassEntry* FindAuctionSubClassByOrdinal(
    const openwow::data::dbc::DbcLoader& dbc,
    std::uint32_t class_id,
    int zero_based_subclass_index) {
  int remaining = zero_based_subclass_index;
  for (const auto& entry : dbc.item_sub_class().entries()) {
    if (entry.class_id != class_id || (entry.display_flags & 0x2u) != 0) {
      continue;
    }
    if (remaining == 0) {
      return &entry;
    }
    --remaining;
  }
  return nullptr;
}

const char* GetAuctionSubClassName(
    const openwow::data::dbc::ItemSubClassEntry& entry) {
  if (!entry.verbose_name.empty()) {
    return entry.verbose_name.data();
  }
  if (!entry.display_name.empty()) {
    return entry.display_name.data();
  }
  return "";
}

std::optional<::openwow::game::AuctionItem> GetAuctionItemSnapshotByRow(
    const ::openwow::game::AuctionState& system,
    const char* list_type,
    std::size_t row) {
  if (list_type == nullptr ||
      ::openwow::core::SStrCmpNoCase(list_type, "list", 0x7FFFFFFFu) == 0) {
    return system.GetResult(row);
  }
  if (::openwow::core::SStrCmpNoCase(list_type, "owner", 0x7FFFFFFFu) == 0) {
    return system.GetOwnAuction(row);
  }
  if (::openwow::core::SStrCmpNoCase(list_type, "bidder", 0x7FFFFFFFu) == 0) {
    return system.GetBid(row);
  }
  return std::nullopt;
}

std::size_t ReadAuctionSaturatedOneBasedRow(lua_State* L, int index) {
  return static_cast<std::size_t>(
      openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, index)) - 1u);
}

std::optional<::openwow::game::AuctionSelectionList> ParseSelectionListType(
    const char* list_type) {
  if (!list_type) {
    return std::nullopt;
  }
  if (::openwow::core::SStrCmpNoCase(list_type, "owner", 0x7FFFFFFFu) == 0) {
    return ::openwow::game::AuctionSelectionList::kOwner;
  }
  if (::openwow::core::SStrCmpNoCase(list_type, "bidder", 0x7FFFFFFFu) == 0) {
    return ::openwow::game::AuctionSelectionList::kBidder;
  }
  if (::openwow::core::SStrCmpNoCase(list_type, "list", 0x7FFFFFFFu) == 0) {
    return ::openwow::game::AuctionSelectionList::kList;
  }
  return std::nullopt;
}

std::uint32_t ParseAuctionQueryType(const char* list_type) {
  if (!list_type ||
      ::openwow::core::SStrCmpNoCase(list_type, "list", 0x7FFFFFFFu) == 0) {
    return 0;
  }
  if (::openwow::core::SStrCmpNoCase(list_type, "bidder", 0x7FFFFFFFu) == 0) {
    return 1;
  }
  if (::openwow::core::SStrCmpNoCase(list_type, "owner", 0x7FFFFFFFu) == 0) {
    return 2;
  }

  return 0;
}

std::optional<std::uint32_t> ReadAuctionSignedNumberArgBits(lua_State* L,
                                                           int index) {
  if (!lua_isnumber(L, index)) {
    return std::nullopt;
  }
  return static_cast<std::uint32_t>(
      TruncateLuaNumberToSseI32(lua_tonumber(L, index)));
}

std::uint8_t ReadAuctionByteNumberArgOrZero(lua_State* L, int index) {
  if (!lua_isnumber(L, index)) {
    return 0;
  }
  return static_cast<std::uint8_t>(
      TruncateLuaNumberToWrappedLowU32(lua_tonumber(L, index)));
}

bool ReadAuctionBooleanArg(lua_State* L, int index) {
  return lua_type(L, index) == LUA_TBOOLEAN && lua_toboolean(L, index) != 0;
}

std::size_t ReadAuctionListRowArgument(lua_State* L, int index) {
  const auto one_based = TruncateLuaNumberToSseI32(lua_tonumber(L, index));
  return static_cast<std::size_t>(static_cast<std::uint32_t>(one_based) - 1u);
}

std::size_t ReadAuctionSelectionRowArgument(lua_State* L, int index) {
  return ReadAuctionSaturatedOneBasedRow(L, index);
}

std::uint32_t ResolveAuctionInvTypeFilter(
    std::optional<std::uint32_t> inv_type_index) {
  if (!inv_type_index.has_value() || *inv_type_index == 0 ||
      *inv_type_index > kAuctionInvTypes.size()) {
    return 0xFFFFFFFFu;
  }

  return kAuctionInvTypes[static_cast<std::size_t>(*inv_type_index - 1)]
      .inventory_type;
}

std::uint32_t ResolveAuctionSubClassFilter(
    lua_State* L,
    const AuctionItemClassDescriptor* class_descriptor,
    std::optional<std::uint32_t> subclass_index) {
  if (!class_descriptor || !class_descriptor->has_subclass_filters ||
      !subclass_index.has_value() || *subclass_index == 0) {
    return 0xFFFFFFFFu;
  }

  const auto* dbc = GetAuctionDbcLoader(L);
  if (!dbc) {
    return 0xFFFFFFFFu;
  }

  const auto* subclass = FindAuctionSubClassByOrdinal(
      *dbc, class_descriptor->class_id,
      static_cast<int>(*subclass_index - 1));
  if (!subclass) {
    return 0xFFFFFFFFu;
  }

  return subclass->subclass_id;
}

std::vector<openwow::net::wotlk::AuctionSearchParams::SortEntry>
CollectAuctionSortEntries(
    const ::openwow::game::AuctionState& auction_state,
    const ::openwow::game::AuctionSelectionList list) {
  std::vector<openwow::net::wotlk::AuctionSearchParams::SortEntry>
      sort_entries;
  sort_entries.reserve(
      ::openwow::game::AuctionState::kMaxAuctionSortEntries);
  for (std::uint32_t index = 0;
       index < ::openwow::game::AuctionState::kMaxAuctionSortEntries;
       ++index) {
    const auto entry = auction_state.GetSortEntry(list, index);
    if (!entry.has_value() || entry->active == 0) {
      continue;
    }
    sort_entries.push_back({
        .column = static_cast<std::uint8_t>(entry->column),
        .reversed = static_cast<std::uint8_t>(entry->reversed),
    });
  }

  return sort_entries;
}

bool IsAuctionHouseOpenForLua(lua_State* L) {
  const auto& auction = RequireAuctionLuaAdapter(L).auction();

  return auction.auctioneer_guid() != 0;
}

bool CanCancelOwnerAuction([[maybe_unused]] lua_State* L, std::size_t index) {
  return RequireAuctionState(L).CanCancelOwnAuction(index);
}

constexpr int kAuctionCancelNotEnoughMoneyMessage = 40;
constexpr float kAuctionCancelBidCutRate = 0.050000001f;

std::uint32_t CalculateAuctionCancelBidCut(std::uint32_t current_bid) {
  return static_cast<std::uint32_t>(
      static_cast<float>(current_bid) * kAuctionCancelBidCutRate);
}

bool CanPayAuctionCancelBidCut(const openwow::game::ObjectManager& objects,
                               const ::openwow::game::AuctionItem& item) {
  if (item.current_bid == 0) {
    return true;
  }

  const auto* player = objects.GetActivePlayer();
  if (player == nullptr) {
    return false;
  }

  return player->GetMoney() >= CalculateAuctionCancelBidCut(item.current_bid);
}

std::optional<std::uint32_t> ParseAuctionSortColumn(const char* sort_name) {
  if (!sort_name) {
    return std::nullopt;
  }

  ::openwow::game::AuctionSortColumnId column =
      ::openwow::game::AuctionSortColumnId::kQuality;
  if (!::openwow::game::ParseAuctionSortColumnName(sort_name, column)) {
    return std::nullopt;
  }

  return static_cast<std::uint32_t>(column);
}

void FireAuctionSortUpdate(
    lua_State* L, const ::openwow::game::AuctionSelectionList list) {
  RequireAuctionLuaAdapter(L).PresentListChanged(list);
}

std::optional<std::string> ResolveAuctionParticipantName(
    AuctionLuaAdapter& adapter,
    const std::uint64_t raw_guid,
    const ::openwow::game::AuctionSelectionList list) {
  if (raw_guid == 0) {
    return std::nullopt;
  }

  if (const auto* cached_name = adapter.query_cache().GetPlayerName(raw_guid);
      cached_name != nullptr && !cached_name->name.empty()) {
    return cached_name->name;
  }

  adapter.RequestNameRefresh(raw_guid, list);
  return std::nullopt;
}

std::optional<lua_Number> ResolveAuctionItemTimeLeftValue(
    AuctionLuaAdapter& adapter,
    openwow::game::AuctionState& auction_state,
    const openwow::game::AuctionItem& item) {
  if (item.expiration_tick_ms == 0) {
    return 0.0;
  }

  const auto now = openwow::core::GameClock::GetTickCount32();
  if (static_cast<std::int32_t>(now - item.expiration_tick_ms) >= 0) {
    if (item.sale_status == 1 || now - item.expiration_tick_ms > 10000u) {
      const auto removed = auction_state.RemoveAuction(item.auction_id);
      if ((static_cast<std::uint8_t>(removed) &
           static_cast<std::uint8_t>(
               openwow::game::AuctionRemovalMask::kList)) != 0) {
        adapter.PresentListChanged(
            openwow::game::AuctionSelectionList::kList);
      }
      if ((static_cast<std::uint8_t>(removed) &
           static_cast<std::uint8_t>(
               openwow::game::AuctionRemovalMask::kOwner)) != 0) {
        adapter.PresentListChanged(
            openwow::game::AuctionSelectionList::kOwner);
      }
      if ((static_cast<std::uint8_t>(removed) &
           static_cast<std::uint8_t>(
               openwow::game::AuctionRemovalMask::kBidder)) != 0) {
        adapter.PresentListChanged(
            openwow::game::AuctionSelectionList::kBidder);
      }
    }
    return 1.0;
  }

  const auto remaining_ms = item.expiration_tick_ms - now;
  if (item.sale_status == 1) {
    return static_cast<lua_Number>(remaining_ms / 1000u);
  }
  if (remaining_ms < 1800000u) {
    return 1.0;
  }
  if (remaining_ms < 7200000u) {
    return 2.0;
  }
  if (remaining_ms < 43200000u) {
    return 3.0;
  }
  return 4.0;
}

constexpr int kAuctionErrorMissingTemplate = 25;
constexpr int kAuctionErrorQuestBoundItem = 388;
constexpr int kAuctionErrorBoundItem = 389;
constexpr int kAuctionErrorConjuredItem = 390;
constexpr int kAuctionErrorTemporaryItem = 391;
constexpr int kAuctionErrorWrappedItem = 392;
constexpr int kAuctionErrorLootableItem = 393;
constexpr int kAuctionErrorNonEmptyBag = 394;
constexpr int kAuctionErrorDamagedItem = 400;
constexpr int kAuctionErrorUsedChargesItem = 401;
constexpr int kAuctionErrorNotEnoughStacks = 724;

struct AuctionTemplateView {
  std::string name;
  std::uint32_t quality = 0;
  std::uint32_t sell_price = 0;
  std::int32_t stackable = 1;
  std::uint32_t display_id = 0;
  std::uint32_t bonding = 0;
  std::uint32_t area = 0;
  std::uint32_t map = 0;
  std::uint32_t duration = 0;
  std::uint32_t holiday_id = 0;
  openwow::game::ItemUseRequirementView requirements;
  std::uint32_t container_slots = 0;
  std::array<openwow::game::ItemSpellData, 5> spells{};
};

struct AuctionInventoryEntry {
  openwow::game::ItemInstance item;
  std::uint8_t source_bag = 0;
  std::uint8_t source_slot = 0;
};

const openwow::game::CGPlayer_C* GetAuctionActivePlayer(
    const AuctionLuaAdapter& adapter) {
  return adapter.objects().GetActivePlayer();
}

std::optional<AuctionTemplateView> ResolveAuctionTemplate(
    AuctionLuaAdapter& adapter,
    std::uint32_t entry) {
  if (entry == 0) {
    return std::nullopt;
  }

  if (const auto* tmpl = adapter.query_cache().GetItemTemplate(entry)) {
      AuctionTemplateView view;
      view.name = tmpl->name;
      view.quality = static_cast<std::uint32_t>(tmpl->quality);
      view.sell_price = tmpl->sell_price;
      view.stackable = tmpl->stackable;
      view.display_id = tmpl->display_id;
      view.bonding = tmpl->bonding;
      view.area = tmpl->area;
      view.map = tmpl->map;
      view.duration = tmpl->duration;
      view.holiday_id = tmpl->holiday_id;
      view.requirements =
          ::openwow::game::BuildItemUseRequirementView(*tmpl);
      view.container_slots = tmpl->container_slots;
      for (std::size_t i = 0; i < view.spells.size(); ++i) {
        const auto& spell = tmpl->spells[i];
        view.spells[i].spell_id = spell.spell_id;
        view.spells[i].trigger = spell.trigger;
        view.spells[i].charges = spell.charges;
        view.spells[i].cooldown = static_cast<std::int32_t>(spell.cooldown);
        view.spells[i].category = spell.category;
        view.spells[i].category_cooldown =
            static_cast<std::int32_t>(spell.category_cooldown);
      }
    return view;
  }

  return std::nullopt;
}

std::optional<AuctionInventoryEntry> ResolveAuctionItemByGuid(
    const openwow::game::PlayerInventoryReplica& inventory,
    std::uint64_t guid) {
  if (guid == 0) {
    return std::nullopt;
  }

  for (std::uint8_t slot = 0; slot < openwow::game::PlayerInventoryReplica::kBackpackSize;
       ++slot) {
    if (const auto* item = inventory.GetBackpackSlot(slot);
        item != nullptr && !item->IsEmpty() && item->guid == guid) {
      return AuctionInventoryEntry{.item = *item, .source_bag = 0, .source_slot = slot};
    }
  }

  for (std::uint8_t bag = 1; bag <= openwow::game::PlayerInventoryReplica::kMaxBags; ++bag) {
    const auto* bag_info = inventory.GetBag(bag);
    if (bag_info == nullptr) {
      continue;
    }

    for (std::uint8_t slot = 0; slot < bag_info->num_slots; ++slot) {
      if (const auto* item = inventory.GetBagSlot(bag, slot);
          item != nullptr && !item->IsEmpty() && item->guid == guid) {
        return AuctionInventoryEntry{.item = *item, .source_bag = bag, .source_slot = slot};
      }
    }
  }

  for (std::uint8_t slot = 0; slot < openwow::game::PlayerInventoryReplica::kMaxEquipSlots;
       ++slot) {
    if (const auto* item = inventory.GetEquipSlot(slot);
        item != nullptr && !item->IsEmpty() && item->guid == guid) {
      return AuctionInventoryEntry{
          .item = *item,
          .source_bag = openwow::game::InventorySlots::kMainBag,
          .source_slot = slot,
      };
    }
  }

  return std::nullopt;
}

const openwow::game::ItemTemplate* ResolveAuctionAcquisitionTemplate(
    AuctionLuaAdapter& adapter,
    const std::uint32_t entry) {
  if (entry == 0) {
    return nullptr;
  }

  return adapter.query_cache().GetItemTemplate(entry);
}

bool CanPlaceAuctionBidForItem(AuctionLuaAdapter& adapter,
                               const openwow::game::AuctionItem& item) {
  const auto item_template =
      ResolveAuctionAcquisitionTemplate(adapter, item.item_entry);
  if (item_template == nullptr) {
    return true;
  }

  return adapter.CanAcquireItem(*item_template, item.count);
}

std::uint64_t ResolveAuctionContainerGuid(
    const openwow::game::PlayerInventoryReplica& inventory,
    std::uint8_t source_bag) {
  if (source_bag == 0 ||
      source_bag == openwow::game::InventorySlots::kMainBag) {
    return 0;
  }

  if (source_bag >= openwow::game::InventorySlots::kBagSlotsStart &&
      source_bag < openwow::game::InventorySlots::kBagSlotsEnd) {
    const auto bag_index = static_cast<std::uint8_t>(
        source_bag - openwow::game::InventorySlots::kBagSlotsStart + 1);
    if (const auto* bag_info = inventory.GetBag(bag_index);
        bag_info != nullptr) {
      return bag_info->guid;
    }
  }

  return 0;
}

bool IsAuctionHouseBagSelectionNonEmpty(
    const openwow::game::PlayerInventoryReplica& inventory,
    std::uint64_t bag_guid) {
  for (std::uint8_t bag = 1; bag <= openwow::game::PlayerInventoryReplica::kMaxBags; ++bag) {
    const auto* bag_info = inventory.GetBag(bag);
    if (bag_info == nullptr || bag_info->guid != bag_guid) {
      continue;
    }

    for (const auto& slot : bag_info->slots) {
      if (!slot.IsEmpty()) {
        return true;
      }
    }
    return false;
  }

  return false;
}

std::uint32_t ResolveAuctionRuntimeMinutes(lua_State* L, int arg_index) {
  const std::uint32_t runtime_index = openwow::ui::SaturateLuaNumberToU32(
      lua_tonumber(L, arg_index) - 1.0);
  if (runtime_index < ::openwow::game::kAuctionRuntimeMinutes.size()) {
    return ::openwow::game::kAuctionRuntimeMinutes[runtime_index];
  }
  return 1440u;
}

bool ShouldRejectUsedCharges(const openwow::game::ItemInstance& item,
                             const AuctionTemplateView& tmpl) {
  const auto& first_spell = tmpl.spells[0];
  return first_spell.spell_id != 0 && item.charges[0] != first_spell.charges;
}

std::optional<int> ValidateAuctionSellItem(
    const openwow::game::ItemInstance& item,
    const AuctionTemplateView& tmpl,
    const openwow::game::PlayerInventoryReplica& inventory) {
  if (item.max_durability != 0 && item.durability < item.max_durability) {
    return kAuctionErrorDamagedItem;
  }

  if (ShouldRejectUsedCharges(item, tmpl)) {
    return kAuctionErrorUsedChargesItem;
  }

  if (item.IsSoulbound()) {
    return tmpl.bonding == 4 ? kAuctionErrorQuestBoundItem
                             : kAuctionErrorBoundItem;
  }

  if (item.IsConjured() ||
      tmpl.area != 0 || tmpl.map != 0) {
    return kAuctionErrorConjuredItem;
  }

  if (item.duration != 0 || tmpl.duration != 0 || tmpl.holiday_id != 0) {
    return kAuctionErrorTemporaryItem;
  }

  if (IsAuctionHouseBagSelectionNonEmpty(inventory, item.guid)) {
    return kAuctionErrorNonEmptyBag;
  }

  if ((item.flags & openwow::game::ItemFlags::kWrapped) != 0) {
    return kAuctionErrorWrappedItem;
  }

  if ((item.flags & openwow::game::ItemFlags::kLootable) != 0) {
    return kAuctionErrorLootableItem;
  }

  return std::nullopt;
}

bool CanAuctionSellItem(AuctionLuaAdapter& adapter,
                        const openwow::game::ItemInstance& item,
                        bool display_error) {
  const auto tmpl = ResolveAuctionTemplate(adapter, item.entry);
  if (!tmpl.has_value()) {
    if (display_error) {
      DisplaySystemMessage(kAuctionErrorMissingTemplate);
    }
    return false;
  }

  const auto error =
      ValidateAuctionSellItem(item, *tmpl, adapter.inventory());
  if (!error.has_value()) {
    return true;
  }

  if (display_error) {
    DisplaySystemMessage(*error);
  }
  return false;
}

std::vector<AuctionInventoryEntry> CollectAuctionInventoryItems(lua_State* L) {
  std::vector<AuctionInventoryEntry> entries;
  auto& inventory = RequireAuctionLuaAdapter(L).inventory();

  for (std::uint8_t slot = 0; slot < openwow::game::PlayerInventoryReplica::kBackpackSize;
       ++slot) {
    if (const auto* item = inventory.GetBackpackSlot(slot);
        item != nullptr && !item->IsEmpty()) {
      entries.push_back({.item = *item, .source_bag = 0, .source_slot = slot});
    }
  }

  for (std::uint8_t bag = 1; bag <= openwow::game::PlayerInventoryReplica::kMaxBags; ++bag) {
    const auto* bag_info = inventory.GetBag(bag);
    if (bag_info == nullptr) {
      continue;
    }

    for (std::uint8_t slot = 0; slot < bag_info->num_slots; ++slot) {
      if (const auto* item = inventory.GetBagSlot(bag, slot);
          item != nullptr && !item->IsEmpty()) {
        entries.push_back({.item = *item, .source_bag = bag, .source_slot = slot});
      }
    }
  }

  return entries;
}

template <typename Callback>
void ForEachAuctionSellableInventoryItem(AuctionLuaAdapter& adapter,
                                         lua_State* L,
                                         std::uint32_t entry,
                                         Callback&& callback) {
  if (entry == 0 || GetAuctionActivePlayer(adapter) == nullptr) {
    return;
  }

  for (const auto& candidate : CollectAuctionInventoryItems(L)) {
    if (candidate.item.entry != entry ||
        !CanAuctionSellItem(adapter, candidate.item, false)) {
      continue;
    }

    if (!callback(candidate)) {
      return;
    }
  }
}

std::uint32_t CountAuctionSellableQuantity(lua_State* L,
                                           AuctionLuaAdapter& adapter,
                                           std::uint32_t entry) {
  std::uint32_t total = 0;
  ForEachAuctionSellableInventoryItem(
      adapter, L, entry, [&total](const AuctionInventoryEntry& candidate) {
        total += candidate.item.count;
        return true;
      });
  return total;
}

std::vector<AuctionCollectedSource> CollectAuctionMultiSellSources(
    lua_State* L,
    AuctionLuaAdapter& adapter,
    std::uint32_t entry,
    std::uint32_t required_quantity) {
  std::vector<AuctionCollectedSource> sources;
  if (required_quantity == 0) {
    return sources;
  }

  sources.reserve(std::min<std::size_t>(
      required_quantity, kAuctionMultiSellMaxTrackedSources));
  std::uint32_t remaining = required_quantity;
  bool exceeded_source_limit = false;
  ForEachAuctionSellableInventoryItem(
      adapter, L, entry, [&](const AuctionInventoryEntry& candidate) {
        if (sources.size() >= kAuctionMultiSellMaxTrackedSources) {
          exceeded_source_limit = true;
          return false;
        }

        const auto available = candidate.item.count;
        sources.push_back({
            .item_guid = candidate.item.guid,
            .available_count = available,
        });
        remaining -= std::min(remaining, available);
        return remaining != 0;
      });

  if (exceeded_source_limit) {
    sources.clear();
    return sources;
  }

  if (remaining != 0) {
    sources.clear();
  }
  return sources;
}

std::vector<std::pair<std::uint64_t, std::uint32_t>>
BuildAuctionSellRequestItemsFromSources(
    std::vector<AuctionCollectedSource> sources,
    const std::uint32_t stack_size) {
  std::vector<std::pair<std::uint64_t, std::uint32_t>> items;
  if (sources.empty() || stack_size == 0) {
    return items;
  }

  auto remaining = stack_size;
  auto boundary = sources.size() - 1;
  while (remaining > sources[boundary].available_count) {
    remaining -= sources[boundary].available_count;
    if (boundary == 0) {
      return {};
    }
    --boundary;
  }

  items.reserve(sources.size() - boundary);
  for (std::size_t index = sources.size(); index-- > boundary + 1;) {
    items.emplace_back(
        sources[index].item_guid, sources[index].available_count);
  }
  items.emplace_back(sources[boundary].item_guid, remaining);
  return items;
}

void FireNewAuctionUpdate(lua_State* L) {
  RequireAuctionLuaAdapter(L).PresentSellSelectionChanged();
}

void ReleaseAuctionSellItems(
    lua_State* L, const std::vector<std::uint64_t>& item_guids,
    const bool failed) {
  for (const auto guid : item_guids) {
    GameUI_OnMouseoverUnitLeave(guid);
  }
  if (!item_guids.empty()) {
    FireNewAuctionUpdate(L);
  }
  if (failed) {
    RequireAuctionLuaAdapter(L).PresentMultiSellFailed();
  }
}

void ClearAuctionSellItemSelection(lua_State* L) {
  const auto selected = RequireAuctionState(L).TakeSellItemSelection();
  if (selected.has_value()) {
    GameUI_OnMouseoverUnitLeave(*selected);
    FireNewAuctionUpdate(L);
  }
}

bool SelectAuctionSellItem(lua_State* L,
                           const ::openwow::game::ItemInstance& item,
                           std::uint8_t source_bag,
                           std::uint8_t source_slot,
                           const bool display_error,
                           const bool notify) {
  auto& adapter = RequireAuctionLuaAdapter(L);
  auto& inventory = adapter.inventory();
  if (!CanAuctionSellItem(adapter, item, display_error)) {
    return false;
  }

  if (item.guid != 0) {
    if (const auto location =
            ::openwow::game::ResolvePlayerItemPacketLocationByGuid(
                inventory, item.guid);
        location.has_value()) {
      source_bag = location->packet_bag;
      source_slot = location->packet_slot;
    }
  }

  RequireAuctionState(L).SetSellItemSelection({
      .item_guid = item.guid,
      .container_guid = ResolveAuctionContainerGuid(inventory, source_bag),
      .slot_id = source_slot,
  });
  if (notify) {
    FireNewAuctionUpdate(L);
  }
  return true;
}

}

bool AuctionTrySelectSellItem(lua_State* L,
                              const ::openwow::game::ItemInstance& item,
                              std::uint8_t source_bag,
                              std::uint8_t source_slot,
  bool display_error) {
  return SelectAuctionSellItem(L, item, source_bag, source_slot,
                               display_error, true);
}

int LuaGetOwnerAuctionItems(lua_State* L) {
  RequireAuctionLuaAdapter(L).RequestOwnerRefresh();
  return 0;
}

int LuaGetBidderAuctionItems(lua_State* L) {
  auto& adapter = RequireAuctionLuaAdapter(L);
  auto& auction = adapter.auction();
  if (auction.auctioneer_guid() == 0) {
    return 0;
  }

  std::uint32_t bidder_page_bits = 0;
  if (lua_isnumber(L, 1)) {
    bidder_page_bits = static_cast<std::uint32_t>(
        TruncateLuaNumberToSseI32(lua_tonumber(L, 1)));
  }
  const auto list_from = bidder_page_bits * 50u;
  auto& auction_state = RequireAuctionState(L);
  if (!auction.bidder_list_request_enabled() ||
      !auction_state.CanSendAuctionQuery(1)) {
    return 0;
  }

  adapter.interaction().SendAuctionListBidderItems(
      auction.auctioneer_guid(),
      list_from,
      auction.CollectOutbidAuctionIdsForCurrentHouse());
  auction_state.MarkQuerySent(1);
  auction.MarkBidderListRequestSent(list_from);
  return 0;
}

int LuaGetAuctionItemInfo(lua_State* L) {
  if (!lua_isstring(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: GetAuctionItemInfo(\"type\", index)");
  }

  const char* list_type = lua_tostring(L, 1);
  const auto row = ReadAuctionSaturatedOneBasedRow(L, 2);
  auto& sys = RequireAuctionState(L);
  const auto item = GetAuctionItemSnapshotByRow(sys, list_type, row);
  const auto list = ParseSelectionListType(list_type);
  if (!item.has_value() || !list.has_value()) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnumber(L, 1.0);
    lua_pushnumber(L, -1.0);
    lua_pushnil(L);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnumber(L, 0);
    return 13;
  }
  auto& adapter = RequireAuctionLuaAdapter(L);
  const auto tmpl = ResolveAuctionTemplate(adapter, item->item_entry);
  if (!tmpl.has_value()) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnumber(L, 1.0);
    lua_pushnumber(L, -1.0);
    lua_pushnil(L);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnumber(L, 0);
    return 13;
  }

  const auto display_name = ResolveLootItemDisplayName(
      GetAuctionDbcLoader(L), tmpl->name, item->random_property);
  lua_pushstring(L, display_name.c_str());
  const auto texture_path =
      ResolveItemEntryIconTexturePathOrFallback(L, item->item_entry);
  lua_pushstring(L, texture_path.c_str());
  lua_pushnumber(L, static_cast<lua_Number>(item->count));
  lua_pushnumber(
      L, static_cast<lua_Number>(
             static_cast<std::uint32_t>(tmpl->quality)));

  if (const auto* player = adapter.objects().GetActivePlayer();
      player == nullptr ||
      adapter.MeetsItemRequirements(*player, tmpl->requirements)) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }

  lua_pushnumber(
      L, static_cast<lua_Number>(
             std::max(tmpl->requirements.required_level, 1u)));
  lua_pushnumber(L, static_cast<lua_Number>(item->start_bid));
  lua_pushnumber(L, static_cast<lua_Number>(item->minimum_increment));
  lua_pushnumber(L, static_cast<lua_Number>(item->buyout));
  lua_pushnumber(L, static_cast<lua_Number>(item->current_bid));

  const auto active_player_guid =
      ::openwow::game::CGObject_C::GetActivePlayerGuid().GetRawValue();
  if (::openwow::core::SStrCmpNoCase(list_type, "owner", 0x7FFFFFFFu) == 0) {
    if (item->bidder_guid != 0 && item->owner_guid == active_player_guid) {
      const auto bidder_name =
          ResolveAuctionParticipantName(adapter, item->bidder_guid, *list);
      if (bidder_name.has_value()) {
        lua_pushstring(L, bidder_name->c_str());
      } else {
        lua_pushnil(L);
      }
    } else {
      lua_pushnil(L);
    }
  } else {
    lua_pushwowbool(L, item->bidder_guid != 0 &&
                           item->bidder_guid == active_player_guid);
  }

  const auto owner_name =
      ResolveAuctionParticipantName(adapter, item->owner_guid, *list);
  if (owner_name.has_value()) {
    lua_pushstring(L, owner_name->c_str());
  } else {
    lua_pushnil(L);
  }
  lua_pushnumber(L, static_cast<lua_Number>(item->sale_status));
  return 13;
}

int LuaGetAuctionItemLink(lua_State* L) {
  if (!lua_isstring(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: GetAuctionItemLink(\"type\", index)");
  }

  const char* list_type = lua_tostring(L, 1);
  const auto row = ReadAuctionSaturatedOneBasedRow(L, 2);
  auto& sys = RequireAuctionState(L);
  const auto item = GetAuctionItemSnapshotByRow(sys, list_type, row);
  if (!item.has_value()) {
    return 0;
  }
  auto& adapter = RequireAuctionLuaAdapter(L);
  const auto tmpl = ResolveAuctionTemplate(adapter, item->item_entry);
  if (!tmpl.has_value()) {
    return 0;
  }
  const auto item_name = ResolveLootItemDisplayName(
      GetAuctionDbcLoader(L), tmpl->name, item->random_property);
  const auto raw_quality = static_cast<std::uint32_t>(tmpl->quality);
  const auto quality = raw_quality < 8u ? raw_quality : 1u;
  static constexpr std::array<const char*, 8> kQualityColors = {
      "|cff9d9d9d", "|cffffffff", "|cff1eff00", "|cff0070dd",
      "|cffa335ee", "|cffff8000", "|cffe6cc80", "|cff00ccff",
  };

  std::uint32_t player_level = 0;
  if (const auto* player = adapter.objects().GetActivePlayer();
      player != nullptr) {
    player_level = player->State().GetLevel();
  }

  char buf[1024];
  std::snprintf(
      buf, sizeof(buf),
      "%s|Hitem:%d:%d:%d:%d:%d:0:%d:%d:%d|h[%s]|h|r",
      kQualityColors[quality],
      ::openwow::ui::SignedI32FromU32Bits(item->item_entry),
      ::openwow::ui::SignedI32FromU32Bits(item->enchant_id),
      ::openwow::ui::SignedI32FromU32Bits(item->gem_enchant_ids[0]),
      ::openwow::ui::SignedI32FromU32Bits(item->gem_enchant_ids[1]),
      ::openwow::ui::SignedI32FromU32Bits(item->gem_enchant_ids[2]),
      item->random_property,
      ::openwow::ui::SignedI32FromU32Bits(item->random_suffix),
      ::openwow::ui::SignedI32FromU32Bits(player_level), item_name.c_str());
  lua_pushstring(L, buf);
  return 1;
}

int LuaGetAuctionItemSubClasses(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetAuctionItemSubClasses(index)");
  }

  const auto class_index_bits =
      openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1));
  if (class_index_bits == 0 || class_index_bits > kAuctionItemClasses.size()) {
    return 0;
  }
  const int class_index = static_cast<int>(class_index_bits);
  const auto* descriptor = GetAuctionSubClassFilterDescriptor(class_index);
  if (!descriptor) {
    return 0;
  }

  const auto* dbc = GetAuctionDbcLoader(L);
  if (!dbc) {
    return 0;
  }

  (void)openwow::ui::ReserveLuaResultCapacity(
      L, dbc->item_sub_class().entries().size(), "auction item subclasses");
  int count = 0;
  for (const auto& entry : dbc->item_sub_class().entries()) {
    if (entry.class_id != descriptor->class_id ||
        (entry.display_flags & 0x2u) != 0) {
      continue;
    }
    lua_pushstring(L, GetAuctionSubClassName(entry));
    ++count;
  }

  return count;
}

int LuaGetAuctionInvTypes(lua_State* L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: GetAuctionInvTypes(classIndex, subClassIndex)");
  }

  const auto class_index_bits =
      openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1));
  const auto subclass_index_bits =
      openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 2));
  if (class_index_bits == 0 || class_index_bits > kAuctionItemClasses.size()) {
    return 0;
  }
  const int class_index = static_cast<int>(class_index_bits);
  const std::uint32_t zero_based_subclass_index = subclass_index_bits - 1u;
  const auto* descriptor = GetAuctionSubClassFilterDescriptor(class_index);
  if (!descriptor) {
    return 0;
  }

  if (const auto* dbc = GetAuctionDbcLoader(L);
      dbc != nullptr && !dbc->item_sub_class().empty()) {
    const auto* subclass = FindAuctionSubClassByOrdinal(
        *dbc, descriptor->class_id,
        ::openwow::ui::SignedI32FromU32Bits(zero_based_subclass_index));

    if (subclass != nullptr && (subclass->flags & 0x200u) == 0) {
      return 0;
    }
  }

  (void)openwow::ui::ReserveLuaResultCapacity(
      L, kAuctionInvTypes.size() * 2u, "auction inventory type values");
  for (const auto& inv_type : kAuctionInvTypes) {
    lua_pushstring(L, inv_type.token);

    if (descriptor->class_id != 4) {
      lua_pushnumber(L, 1.0);
      continue;
    }

    if (zero_based_subclass_index == 0) {
      lua_pushwowbool(L, inv_type.allow_for_first_armor_subclass);
      continue;
    }

    const bool enabled =
        (zero_based_subclass_index == 1 && inv_type.inventory_type == 16) ||
        !inv_type.deny_for_other_armor_subclasses;
    lua_pushwowbool(L, enabled);
  }

  return static_cast<int>(kAuctionInvTypes.size() * 2);
}

int LuaGetAuctionItemClasses(lua_State* L) {
  const auto* dbc = GetAuctionDbcLoader(L);
  if (!dbc) {
    return 0;
  }

  (void)openwow::ui::ReserveLuaResultCapacity(
      L, kAuctionItemClasses.size(), "auction item classes");
  int count = 0;
  for (const auto& descriptor : kAuctionItemClasses) {
    const auto* entry = dbc->item_class().LookupEntry(descriptor.class_id);
    if (!entry) {
      continue;
    }

    lua_pushstring(L, entry->name.data());
    ++count;
  }

  return count;
}

int LuaGetNumAuctionItems(lua_State* L) {
  if (!lua_isstring(L, 1)) {
    return luaL_error(L, "Usage: GetNumAuctionItems(\"type\")");
  }

  auto& sys = RequireAuctionState(L);
  const char* list_type = lua_tostring(L, 1);
  if (::openwow::core::SStrCmpNoCase(list_type, "owner", 0x7FFFFFFFu) == 0) {
    const auto count = sys.GetNumOwnAuctions();
    lua_pushnumber(L, static_cast<lua_Number>(count));
    lua_pushnumber(L, static_cast<lua_Number>(count));
    return 2;
  }
  if (::openwow::core::SStrCmpNoCase(list_type, "bidder", 0x7FFFFFFFu) == 0) {
    lua_pushnumber(L, static_cast<lua_Number>(sys.GetNumBids()));
    lua_pushnumber(L, static_cast<lua_Number>(sys.GetTotalBidCount()));
    return 2;
  }
  if (::openwow::core::SStrCmpNoCase(list_type, "list", 0x7FFFFFFFu) == 0) {
    lua_pushnumber(L, static_cast<lua_Number>(sys.GetNumResults()));
    lua_pushnumber(L, static_cast<lua_Number>(sys.GetTotalResultCount()));
    return 2;
  }

  return 2;
}

int LuaSortAuctionItems(lua_State* L) {
  if (!lua_isstring(L, 1) || !lua_isstring(L, 2)) {
    return luaL_error(L, "Usage: SortAuctionItems(\"type\", \"sort\")");
  }

  const char* list_type = lua_tostring(L, 1);
  const char* sort_name = lua_tostring(L, 2);
  const auto list = ParseSelectionListType(list_type);
  const auto column = ParseAuctionSortColumn(sort_name);
  if (!list.has_value() || !column.has_value()) {
    return 0;
  }

  auto& auction_state = RequireAuctionState(L);
  bool reversed = false;
  if (const auto entry = auction_state.GetSortEntry(*list, 0);
      entry.has_value() && entry->active != 0 && entry->column == *column) {
    reversed = entry->reversed == 0;
  }

  auction_state.PromoteSortEntry(*list, *column, reversed);
  auction_state.ApplySort(*list);
  FireAuctionSortUpdate(L, *list);
  return 0;
}

int LuaSortAuctionClearSort(lua_State* L) {
  if (!lua_isstring(L, 1)) {
    return luaL_error(L, "Usage: SortAuctionClearSort(\"type\")");
  }

  if (const auto list = ParseSelectionListType(lua_tostring(L, 1));
      list.has_value()) {
    RequireAuctionState(L).ClearSortEntries(*list);
  }
  return 0;
}

int LuaQueryAuctionItems(lua_State* L) {
  auto& adapter = RequireAuctionLuaAdapter(L);
  auto& auction = adapter.auction();
  if (auction.auctioneer_guid() == 0) {
    return 0;
  }

  auto& auction_state = RequireAuctionState(L);
  const bool get_all_requested = ReadAuctionBooleanArg(L, 10);
  if (get_all_requested ? !auction_state.CanSendGetAllAuctionQuery(0)
                        : !auction_state.CanSendAuctionQuery(0)) {
    return 0;
  }

  const auto inv_type_index = lua_isnumber(L, 4)
                                  ? std::optional<std::uint32_t>(
                                        openwow::ui::SaturateLuaNumberToU32(
                                            lua_tonumber(L, 4)))
                                  : std::nullopt;
  const auto class_index = lua_isnumber(L, 5)
                               ? std::optional<std::uint32_t>(
                                     openwow::ui::SaturateLuaNumberToU32(
                                         lua_tonumber(L, 5)))
                               : std::nullopt;
  const auto subclass_index = lua_isnumber(L, 6)
                                  ? std::optional<std::uint32_t>(
                                        openwow::ui::SaturateLuaNumberToU32(
                                            lua_tonumber(L, 6)))
                                  : std::nullopt;
  const auto* class_descriptor = GetAuctionItemClassDescriptorByIndex(
      class_index.has_value() ? static_cast<int>(*class_index) : 0);

  ::openwow::net::wotlk::AuctionSearchParams params;
  params.auctioneer_guid =
      ::openwow::game::ObjectGuid(auction.auctioneer_guid());
  params.list_from = ReadAuctionSignedNumberArgBits(L, 7).value_or(0) * 50u;
  params.search_string = lua_isstring(L, 1) != 0
                             ? std::string(lua_tostring(L, 1))
                             : std::string();
  params.level_min = static_cast<std::uint8_t>(
      ReadAuctionSignedNumberArgBits(L, 2).value_or(0));
  params.level_max = static_cast<std::uint8_t>(
      ReadAuctionSignedNumberArgBits(L, 3).value_or(0));
  params.inventory_type = ResolveAuctionInvTypeFilter(inv_type_index);
  params.item_class =
      class_descriptor ? class_descriptor->class_id : 0xFFFFFFFFu;
  params.item_sub_class = ResolveAuctionSubClassFilter(
      L, class_descriptor, subclass_index);
  params.quality =
      ReadAuctionSignedNumberArgBits(L, 9).value_or(0xFFFFFFFFu);
  params.usable = ReadAuctionByteNumberArgOrZero(L, 8);
  params.get_all = get_all_requested ? 1u : 0u;
  params.sort_columns = CollectAuctionSortEntries(
      auction_state, ::openwow::game::AuctionSelectionList::kList);

  adapter.interaction().SendAuctionListItems(params);
  auction_state.MarkBrowseQuerySent(get_all_requested);
  return 0;
}

int LuaCanSendAuctionQuery(lua_State* L) {
  const char* query_type_name =
      lua_isstring(L, 1) ? lua_tostring(L, 1) : nullptr;
  const auto query_type = ParseAuctionQueryType(query_type_name);
  auto& auction_state = RequireAuctionState(L);
  const bool can_send = auction_state.CanSendAuctionQuery(query_type);
  const bool can_send_get_all =
      auction_state.CanSendGetAllAuctionQuery(query_type);
  lua_pushwowbool(L, can_send);
  lua_pushwowbool(L, can_send_get_all);
  return 2;
}

int LuaPlaceAuctionBid(lua_State* L) {
  if (!lua_isstring(L, 1) || !lua_isnumber(L, 2) || !lua_isnumber(L, 3)) {
    return luaL_error(L, "Usage: PlaceAuctionBid(\"type\", index, bid)");
  }

  auto& adapter = RequireAuctionLuaAdapter(L);

  if (!adapter.CanPerformProtectedAction(
          openwow::ui::game::protected_action_kind::kAuction)) {
    return 0;
  }

  const char* list_type = lua_tostring(L, 1);
  const auto row = ReadAuctionSaturatedOneBasedRow(L, 2);
  std::uint32_t bid = openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 3));

  auto& auction_state = RequireAuctionState(L);
  const auto item = GetAuctionItemSnapshotByRow(auction_state, list_type, row);
  if (!item.has_value()) {
    return 0;
  }

  auto& auction = adapter.auction();
  if (auction.auctioneer_guid() == 0) {
    return 0;
  }

  const auto* active_player = GetAuctionActivePlayer(adapter);
  if (active_player == nullptr) {
    return 0;
  }

  if (item->buyout != 0 && bid > item->buyout) {
    bid = item->buyout;
  }

  if (bid < item->start_bid) {
    DisplaySystemMessage(399);
    return 0;
  }

  if (bid <= item->current_bid) {
    DisplaySystemMessage(398);
    return 0;
  }

  const std::uint64_t next_bid_floor =
      static_cast<std::uint64_t>(item->current_bid) +
      static_cast<std::uint64_t>(item->minimum_increment);
  if (item->current_bid != 0 && bid < next_bid_floor &&
      (item->buyout == 0 || bid < item->buyout)) {
    DisplaySystemMessage(397);
    return 0;
  }

  if (bid > 0x77359400u &&
      static_cast<std::uint64_t>(bid) > next_bid_floor) {
    return 0;
  }

  std::uint32_t required_money = bid;
  if (item->bidder_guid == active_player->GetGuid().GetRawValue()) {
    required_money -= item->current_bid;
  }

  if (active_player->GetMoney() < required_money) {
    DisplaySystemMessage(40);
    return 0;
  }

  if (!CanPlaceAuctionBidForItem(adapter, *item)) {
    return 0;
  }

  adapter.interaction().SendAuctionPlaceBid(
      auction.auctioneer_guid(), item->auction_id, bid);
  return 0;
}

int LuaGetAuctionSellItemInfo(lua_State* L) {
  auto& adapter = RequireAuctionLuaAdapter(L);
  const auto selection = RequireAuctionState(L).GetSellItemSelection();
  const auto item = ResolveAuctionItemByGuid(adapter.inventory(),
                                             selection.item_guid);
  const auto tmpl = item.has_value()
                        ? ResolveAuctionTemplate(adapter, item->item.entry)
                        : std::nullopt;
  if (!item.has_value() || !tmpl.has_value()) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnumber(L, 1.0);
    lua_pushnumber(L, -1.0);
    lua_pushnil(L);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    return 9;
  }

  const auto item_count = item->item.count;
  const auto total_sell_price = tmpl->sell_price * item_count;
  const auto total_matching_count =
      CountAuctionSellableQuantity(L, adapter, item->item.entry);
  const auto texture_path =
      cursor_texture::ResolveItemTexturePath(L, item->item.entry);
  const auto display_name = ResolveLootItemDisplayName(
      GetAuctionDbcLoader(L), tmpl->name, item->item.random_property);

  lua_pushstring(L, display_name.empty() ? "Item" : display_name.c_str());
  lua_pushstring(L, texture_path.empty()
                        ? "Interface\\Icons\\INV_Misc_QuestionMark"
                        : texture_path.c_str());
  lua_pushnumber(L, static_cast<lua_Number>(item_count));
  lua_pushnumber(
      L, static_cast<lua_Number>(
             static_cast<std::uint32_t>(tmpl->quality)));

  if (const auto* player = GetAuctionActivePlayer(adapter);
      player == nullptr) {
    lua_pushnumber(L, 1.0);
  } else if (adapter.MeetsItemRequirements(*player, tmpl->requirements)) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }

  lua_pushnumber(L, static_cast<lua_Number>(total_sell_price));
  lua_pushnumber(L, static_cast<lua_Number>(tmpl->sell_price));
  lua_pushnumber(L, static_cast<lua_Number>(tmpl->stackable));
  lua_pushnumber(L, static_cast<lua_Number>(total_matching_count));
  return 9;
}

int LuaCancelSell([[maybe_unused]] lua_State* L) {
  auto& auction_state = RequireAuctionState(L);
  if (auction_state.HasActiveMultiSell()) {
    ReleaseAuctionSellItems(L, auction_state.AbortMultiSell(), true);
  }
  return 0;
}

int LuaClickAuctionSellItemButton(lua_State* L) {
  auto& adapter = RequireAuctionLuaAdapter(L);
  if (adapter.held_cursor() == nullptr) {
    return 0;
  }
  auto& held_cursor = *adapter.held_cursor();
  auto& auction_state = RequireAuctionState(L);

  const auto previous = auction_state.GetSellItemSelection();
  const auto* held_item = held_cursor.live_item();
  if (held_item == nullptr) {
    if (previous.IsEmpty()) {
      return 0;
    }

    auction_state.ClearSellItemSelection();
    adapter.ReturnSellItemToCursor(
        previous.item_guid, previous.container_guid, previous.slot_id);
    FireNewAuctionUpdate(L);
    return 0;
  }

  if (!previous.IsEmpty() && previous.item_guid == held_item->item.guid) {
    held_cursor.Clear();
    return 0;
  }

  if (!SelectAuctionSellItem(L, held_item->item, held_item->source_bag,
                             held_item->source_slot, true, false)) {
    return 0;
  }

  if (!previous.IsEmpty()) {
    adapter.ReturnSellItemToCursor(
        previous.item_guid, previous.container_guid, previous.slot_id);
  } else {
    held_cursor.Clear();
  }

  FireNewAuctionUpdate(L);
  return 0;
}

int LuaStartAuction(lua_State* L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2) || !lua_isnumber(L, 3)) {
    return luaL_error(
        L, "Usage: StartAuction(minBid, buyoutPrice, runTime, count)");
  }

  auto& adapter = RequireAuctionLuaAdapter(L);
  auto& auction = adapter.auction();
  if (auction.auctioneer_guid() == 0) {
    return 0;
  }

  const auto selection = RequireAuctionState(L).GetSellItemSelection();
  const auto item = ResolveAuctionItemByGuid(adapter.inventory(),
                                             selection.item_guid);
  const auto tmpl = item.has_value()
                        ? ResolveAuctionTemplate(adapter, item->item.entry)
                        : std::nullopt;
  if (!item.has_value() || !tmpl.has_value()) {
    return 0;
  }

  if (!CanAuctionSellItem(adapter, item->item, true)) {
    return 0;
  }

  auto min_bid = openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1));
  auto buyout = openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 2));
  if (buyout != 0 && buyout < min_bid) {
    buyout = min_bid;
  }

  const auto runtime_minutes = ResolveAuctionRuntimeMinutes(L, 3);
  const auto stack_size = lua_isnumber(L, 4)
                              ? openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 4))
                              : 0u;
  const auto num_stacks = lua_isnumber(L, 5)
                              ? openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 5))
                              : 1u;

  if (item->item.count < stack_size || num_stacks > 1) {
    const auto total_required = stack_size * num_stacks;
    const auto sources =
        CollectAuctionMultiSellSources(
            L, adapter, item->item.entry, total_required);
    if (sources.empty()) {
      DisplaySystemMessage(kAuctionErrorNotEnoughStacks);
      ClearAuctionSellItemSelection(L);
      return 0;
    }

    auto& auction_state = RequireAuctionState(L);
    if (num_stacks > 1) {
      std::vector<::openwow::game::AuctionMultiSellSource> multi_sell_sources;
      multi_sell_sources.reserve(sources.size());
      for (const auto& source : sources) {
        multi_sell_sources.push_back({
            .item_guid = source.item_guid,
            .remaining_count = source.available_count,
        });
      }

      auction_state.BeginMultiSell(
          auction.auctioneer_guid(), min_bid, buyout,
          runtime_minutes, stack_size, num_stacks,
          std::move(multi_sell_sources));
      adapter.PresentMultiSellStarted(num_stacks);
      for (const auto& source : sources) {
        GameUI_OnMouseoverUnitEnter(source.item_guid);
      }
      const auto request = auction_state.PrepareNextMultiSellRequest();
      if (!request.has_value()) {
        ReleaseAuctionSellItems(L, auction_state.AbortMultiSell(), true);
        return 0;
      }

      adapter.SellItems(
          request->auctioneer_guid, request->items, request->min_bid,
          request->buyout, request->duration_minutes);
      return 0;
    }

    const auto request_items =
        BuildAuctionSellRequestItemsFromSources(sources, stack_size);
    if (request_items.empty()) {
      ClearAuctionSellItemSelection(L);
      return 0;
    }

    adapter.SellItems(
        auction.auctioneer_guid(), request_items, min_bid, buyout,
        runtime_minutes);
    ClearAuctionSellItemSelection(L);
    return 0;
  }

  adapter.SellItems(
      auction.auctioneer_guid(), {{item->item.guid, stack_size}},
      min_bid, buyout, runtime_minutes);
  ClearAuctionSellItemSelection(L);
  return 0;
}

int LuaCalculateAuctionDeposit(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: CalculateAuctionDeposit(runTime)");
  }

  auto& adapter = RequireAuctionLuaAdapter(L);
  const auto selection = RequireAuctionState(L).GetSellItemSelection();
  const auto item = ResolveAuctionItemByGuid(adapter.inventory(),
                                             selection.item_guid);
  const auto tmpl = item.has_value()
                        ? ResolveAuctionTemplate(adapter, item->item.entry)
                        : std::nullopt;
  if (!item.has_value() || !tmpl.has_value()) {
    lua_pushnumber(L, 0.0);
    return 1;
  }

  const auto* dbc = adapter.dbc();
  const auto* auction_house =
      dbc != nullptr
          ? dbc->auction_house().LookupEntry(
                adapter.auction().auction_house_id())
          : nullptr;
  if (auction_house == nullptr) {
    lua_pushnumber(L, 0.0);
    return 1;
  }

  const auto runtime_minutes = ResolveAuctionRuntimeMinutes(L, 1);
  const auto stack_size = lua_isnumber(L, 2)
                              ? openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 2))
                              : item->item.count;
  const auto sell_price_total = tmpl->sell_price * stack_size;
  const auto deposit = ::openwow::game::CalculateRetailAuctionDeposit(
      auction_house->deposit_rate, sell_price_total, runtime_minutes);
  lua_pushnumber(L, static_cast<lua_Number>(deposit));
  return 1;
}

int LuaCancelAuction(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: SetSelectedAuctionItem(index)");
  }

  auto& adapter = RequireAuctionLuaAdapter(L);

  if (!adapter.CanPerformProtectedAction(
          openwow::ui::game::protected_action_kind::kAuction)) {
    return 0;
  }

  auto& auction = adapter.auction();
  if (auction.auctioneer_guid() == 0) {
    return 0;
  }

  const auto index = ReadAuctionSaturatedOneBasedRow(L, 1);

  auto& sys = RequireAuctionState(L);
  const auto item = sys.GetOwnAuction(index);
  if (!item.has_value() || item->sale_status != 0) {
    return 0;
  }

  if (!CanPayAuctionCancelBidCut(adapter.objects(), *item)) {
    DisplaySystemMessage(kAuctionCancelNotEnoughMoneyMessage);
    return 0;
  }

  adapter.interaction().SendAuctionRemoveItem(
      auction.auctioneer_guid(), item->auction_id);
  return 0;
}

int LuaSetSelectedAuctionItem(lua_State* L) {
  if (!lua_isstring(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: SetSelectedAuctionItem(\"type\", index)");
  }

  const char* list_type = lua_tostring(L, 1);
  const auto parsed_list = ParseSelectionListType(list_type);
  if (!parsed_list.has_value()) {
    return 0;
  }

  auto& sys = RequireAuctionState(L);
  sys.SetSelectedAuctionItem(
      *parsed_list, ReadAuctionSelectionRowArgument(L, 2));
  return 0;
}

int LuaGetSelectedAuctionItem(lua_State* L) {
  if (!lua_isstring(L, 1)) {
    return luaL_error(L, "Usage: GetSelectedAuctionItem(\"type\")");
  }

  const char* list_type = lua_tostring(L, 1);
  const auto parsed_list = ParseSelectionListType(list_type);
  if (!parsed_list.has_value()) {
    lua_pushnumber(L, 1.0);
    return 1;
  }

  const auto selected_index =
      RequireAuctionState(L).GetSelectedAuctionItem(
          *parsed_list);
  lua_pushnumber(L, static_cast<lua_Number>(selected_index));
  return 1;
}

int LuaCloseAuctionHouse(lua_State* L) {
  RequireAuctionLuaAdapter(L).CloseHouse();
  return 0;
}

int LuaGetAuctionHouseDepositRate(lua_State* L) {
  const auto deposit_rate =
      RequireAuctionLuaAdapter(L).auction().deposit_rate();
  lua_pushnumber(L, static_cast<lua_Number>(deposit_rate));
  return 1;
}

int LuaGetAuctionItemTimeLeft(lua_State* L) {
  if (!lua_isstring(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: GetAuctionItemTimeLeft(\"type\", index)");
  }

  const char* list_type = lua_tostring(L, 1);
  const auto row = ReadAuctionListRowArgument(L, 2);
  auto& sys = RequireAuctionState(L);
  const auto item = GetAuctionItemSnapshotByRow(sys, list_type, row);
  const auto time_left = item.has_value()
                             ? ResolveAuctionItemTimeLeftValue(
                                   RequireAuctionLuaAdapter(L), sys, *item)
                             : std::optional<lua_Number>(0.0);
  lua_pushnumber(L, time_left.value_or(0.0));
  return 1;
}

int LuaGetAuctionSort(lua_State* L) {
  if (!lua_isstring(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: GetAuctionSort(\"type\", \"index\")");
  }

  const auto list = ParseSelectionListType(lua_tostring(L, 1));
  const auto index = static_cast<std::uint32_t>(
                         TruncateLuaNumberToSseI32(lua_tonumber(L, 2))) -
                     1u;
  if (!list.has_value() || index >= 12u) {
    lua_pushnil(L);
    lua_pushnil(L);
    return 2;
  }

  const auto entry =
      RequireAuctionState(L).GetSortEntry(
          *list, index);
  if (!entry.has_value() || entry->active == 0) {
    lua_pushnil(L);
    lua_pushnil(L);
    return 2;
  }

  lua_pushstring(
      L, ::openwow::game::GetAuctionSortColumnName(
             static_cast<::openwow::game::AuctionSortColumnId>(entry->column)));
  if (entry->reversed != 0) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 2;
}

int LuaIsAuctionSortReversed(lua_State* L) {
  if (!lua_isstring(L, 1) || !lua_isstring(L, 2)) {
    return luaL_error(L, "Usage: IsAuctionSortReversed(\"type\", \"sort\")");
  }

  const auto list = ParseSelectionListType(lua_tostring(L, 1));
  const auto column = ParseAuctionSortColumn(lua_tostring(L, 2));
  if (!column.has_value()) {
    return 0;
  }

  if (!list.has_value()) {
    lua_pushnil(L);
    lua_pushnil(L);
    return 2;
  }

  auto& auction_state = RequireAuctionState(L);
  for (std::uint32_t index = 0;
       index < ::openwow::game::AuctionState::kMaxAuctionSortEntries;
       ++index) {
    const auto entry = auction_state.GetSortEntry(*list, index);
    if (!entry.has_value() || entry->active == 0 || entry->column != *column) {
      continue;
    }

    if (entry->reversed != 0) {
      lua_pushnumber(L, 1.0);
    } else {
      lua_pushnil(L);
    }
    lua_pushnumber(L, 1.0);
    return 2;
  }

  lua_pushnil(L);
  lua_pushnil(L);
  return 2;
}

int LuaSortAuctionApplySort(lua_State* L) {
  if (!lua_isstring(L, 1)) {
    return luaL_error(L, "Usage: SortAuctionApplySort(\"type\")");
  }

  if (const auto list = ParseSelectionListType(lua_tostring(L, 1));
      list.has_value()) {
    auto& auction_state = RequireAuctionState(L);
    auction_state.ApplySort(*list);
  FireAuctionSortUpdate(L, *list);
  }
  return 0;
}

int LuaSortAuctionAddSort(lua_State* L) {
  if (!lua_isstring(L, 1) || !lua_isstring(L, 2)) {
    return luaL_error(
        L, "Usage: SortAuctionAddSort(\"type\", \"sort\", \"reverse\")");
  }

  const auto list = ParseSelectionListType(lua_tostring(L, 1));
  const auto column = ParseAuctionSortColumn(lua_tostring(L, 2));
  const bool reversed =
      lua_type(L, 3) == LUA_TBOOLEAN && lua_toboolean(L, 3) != 0;
  if (list.has_value() && column.has_value()) {
    RequireAuctionState(L).PromoteSortEntry(*list, *column,
                                                           reversed);
  }
  return 0;
}

int LuaCanCancelAuction(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: CanCancelAuction(index)");
  }

  if (!IsAuctionHouseOpenForLua(L)) {
    return 0;
  }

  const auto index = ReadAuctionSaturatedOneBasedRow(L, 1);
  const bool can_cancel = CanCancelOwnerAuction(L, index);
  lua_pushwowbool(L, can_cancel);
  return 1;
}

int LuaSetAuctionsTabShowing(lua_State* L) {
  if (lua_type(L, 1) != LUA_TBOOLEAN) {
    return luaL_error(L, "Usage: SetAuctionsTabShowing(bool)");
  }

  RequireAuctionState(L).SetAuctionsTabShowing(
      lua_toboolean(L, 1) != 0);
  return 0;
}

}
