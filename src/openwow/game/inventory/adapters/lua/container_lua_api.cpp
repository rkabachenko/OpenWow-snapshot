#include "openwow/game/actions/held_cursor/held_cursor.h"
#include "openwow/ui/game/runtime/texture_render_state_source.h"
#include "openwow/ui/lua_c_api_convenience.h"

#include <algorithm>

#include "openwow/game/action_validation_utils.h"
#include "openwow/game/actions/held_cursor/adapters/platform/cursor_surface.h"
#include "openwow/game/commerce/auctions/adapters/lua/auction_lua_api.h"
#include "openwow/game/commerce/auctions/auction_state.h"
#include "openwow/game/commerce/mail/adapters/lua/mail_compose_attachment_lua.h"
#include "openwow/game/commerce/trade/trade_interaction.h"
#include "openwow/game/guild_system.h"
#include "openwow/game/inventory/adapters/lua/container_lua_api.h"
#include "openwow/game/inventory/adapters/protocol/inventory_messages.h"
#include "openwow/game/inventory/adapters/ui/item_cursor_pickup_controller.h"
#include "openwow/game/inventory/adapters/ui/item_spell_target_controller.h"
#include "openwow/game/inventory/adapters/ui/item_target_cursor_presenter.h"
#include "openwow/game/inventory/items/adapters/lua/item_lua_adapter.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/items/item_link_parser.h"
#include "openwow/game/inventory/operations/inventory_commands.h"
#include "openwow/game/inventory/operations/player_item_packet_location.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/localization.h"
#include "openwow/game/objects/cgitem.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/spell_cast_runtime.h"
#include "openwow/game/spell_missile_runtime.h"
#include "openwow/game/tutorial_system.h"
#include "openwow/game/update_fields.h"
#include "openwow/render/models/characters/portrait_icon_texture.h"
#include "openwow/ui/game/api/game_lua_api_container_common.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/api/held_cursor_lua_api.h"
#include "openwow/ui/game/event_dispatcher.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/game_ui_core.h"
#include "openwow/ui/game/guild_bank_cursor_utils.h"
#include "openwow/ui/game/loot_tooltip_support.h"
#include "openwow/ui/game/merchant_repair_cost.h"
#include "openwow/ui/game/runtime/lua/held_cursor_lua_binding.h"
#include "openwow/ui/game/trade_cursor_utils.h"
#include "openwow/ui/lua_numeric.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"

namespace openwow::ui::game::detail {

namespace item_cursor = ::openwow::game::inventory::ui;
namespace item_targeting = ::openwow::game::inventory::ui;

namespace {

constexpr int kBagPortraitLuaSlotCount = 4;

constexpr int kFirstCarriedContainerId = 1;
constexpr int kLastCarriedContainerId = 4;
constexpr int kFirstBankContainerId = 5;
constexpr int kLastBankContainerId = 11;
constexpr int kFirstCarriedContainerInventorySlot = 20;
constexpr int kFirstBankContainerInventorySlot = 68;

std::uint32_t LookupContainerBagFamilyMask(lua_State* L,
                                           ItemLuaAdapter& adapter,
                                           std::uint32_t entry) {
  if (entry == 0) {
    return 0;
  }

  if (const auto* item_template = adapter.queries().GetItemTemplate(entry);
      item_template != nullptr) {
    return item_template->bag_family;
  }

  if (const auto* cached_item = RequireItemLuaAdapter(L).items().GetItem(entry);
      cached_item != nullptr) {
    return cached_item->bag_family;
  }

  return 0;
}

const char* GetItemQualityHyperlinkPrefix(std::uint32_t quality) {
  return ::openwow::game::ItemTemplate::GetQualityColorInfo(quality)
      .hyperlink_color;
}

bool TextureMatchesObjectType(const char* type_name) {
  return openwow::text::EqualsIgnoreCaseAscii(type_name, "Texture") ||
         openwow::text::EqualsIgnoreCaseAscii(type_name, "Region") ||
         openwow::text::EqualsIgnoreCaseAscii(type_name, "Object");
}

inline constexpr std::array<int, 12> kInventoryAlertTrackedSlots{
    0, 2, 4, 5, 6, 7, 8, 9, 15, 16, 17, -1,
};
inline constexpr int kInventoryAlertLowDurabilityThreshold = 5;
inline constexpr std::uint32_t kQuestItemBonding = 4;

[[nodiscard]] bool IsWrappedGiftItem(
    const ::openwow::game::ItemInstance& item,
    const ::openwow::game::ItemTemplate& item_template) {
  return (item_template.flags & ::openwow::game::ItemFlags::kWrapped) != 0 &&
         (item.flags & ::openwow::game::ItemFlags::kGiftWrapped) != 0;
}

[[nodiscard]] bool IsWrappingPaperItem(
    const ::openwow::game::ItemInstance& item,
    const ::openwow::game::ItemTemplate& item_template) {
  return (item_template.flags & ::openwow::game::ItemFlags::kWrapped) != 0 &&
         (item.flags & ::openwow::game::ItemFlags::kGiftWrapped) == 0;
}

void FireInventoryAlertEvents(ItemLuaAdapter& adapter, const bool changed) {
  auto& dispatch = adapter.events();
  if (changed) {
    dispatch.FireEvent(events::UPDATE_INVENTORY_ALERTS);
  }
  dispatch.FireEvent(events::UPDATE_INVENTORY_DURABILITY);
}

const ::openwow::game::CGItem_C* ResolveInventoryAlertEquippedItem(
    ItemLuaAdapter& adapter, const ::openwow::game::CGPlayer_C& player,
    const std::uint8_t slot) {
  const auto item_guid = player.GetEquippedItem(slot);
  if (item_guid.IsEmpty()) {
    return nullptr;
  }

  const auto* object = adapter.objects().Get(item_guid);
  if (object == nullptr || !object->IsItem()) {
    return nullptr;
  }

  return static_cast<const ::openwow::game::CGItem_C*>(object);
}

int ComputeEquipmentInventoryAlert(const ::openwow::game::CGItem_C* item) {
  if (item == nullptr) {
    return 0;
  }

  const auto item_flags = item->GetItemFlags();
  if ((item_flags & ::openwow::game::kItemFieldFlagBroken) != 0) {
    return 2;
  }
  if ((item_flags & ::openwow::game::kItemFieldFlagWrapped) != 0) {
    return 0;
  }

  const auto max_durability = item->GetMaxDurability();
  if (max_durability == 0) {
    return 0;
  }

  constexpr std::uint32_t kTutorialEquipmentBroken = 0x24u;
  constexpr std::uint32_t kTutorialEquipmentDamaged = 0x23u;

  const auto current_durability = item->GetDurability();
  if (current_durability == 0) {
    ::openwow::game::TutorialSystem::Instance().TriggerTutorial(
        kTutorialEquipmentBroken);
    return 2;
  }

  if (current_durability <= kInventoryAlertLowDurabilityThreshold) {
    ::openwow::game::TutorialSystem::Instance().TriggerTutorial(
        kTutorialEquipmentDamaged);
    return 1;
  }

  return 0;
}

int ComputeAmmoInventoryAlert(ItemLuaAdapter& adapter,
                              const ::openwow::game::CGPlayer_C& player) {
  const auto ammo_item_id = player.GetUInt32(::openwow::game::PLAYER_AMMO_ID);
  if (ammo_item_id == 0) {
    return 0;
  }

  return ::openwow::game::CountCarriedItemsOfEntry(adapter.inventory(),
                                                   ammo_item_id) <= 0x14u
             ? 1
             : 0;
}

std::array<int, kInventoryAlertTrackedSlots.size()>
ComputeInventoryAlertStatuses(ItemLuaAdapter& adapter,
                              const ::openwow::game::CGPlayer_C& player) {
  std::array<int, kInventoryAlertTrackedSlots.size()> statuses{};

  for (std::size_t index = 0; index < kInventoryAlertTrackedSlots.size();
       ++index) {
    const int slot = kInventoryAlertTrackedSlots[index];
    if (slot < 0) {
      statuses[index] = ComputeAmmoInventoryAlert(adapter, player);
      continue;
    }

    statuses[index] =
        ComputeEquipmentInventoryAlert(ResolveInventoryAlertEquippedItem(
            adapter, player, static_cast<std::uint8_t>(slot)));
  }

  return statuses;
}

void EnsureInventoryAlertProjectionInitialized(ItemLuaAdapter& adapter) {
  auto& cache = adapter.inventory_alerts();
  const auto* player = adapter.objects().GetActivePlayer();
  if (player == nullptr) {
    cache = {};
    cache.initialized = true;
    return;
  }

  const auto active_player_guid = player->GetGuid().GetRawValue();
  if (cache.initialized && cache.player == active_player_guid) {
    return;
  }

  cache = {};
  cache.player = active_player_guid;
  cache.statuses = ComputeInventoryAlertStatuses(adapter, *player);
  cache.initialized = true;
}

void RefreshInventoryAlertState(ItemLuaAdapter& adapter) {
  const auto* player = adapter.objects().GetActivePlayer();
  if (player == nullptr) {
    return;
  }

  EnsureInventoryAlertProjectionInitialized(adapter);

  auto& cache = adapter.inventory_alerts();
  const auto updated_statuses = ComputeInventoryAlertStatuses(adapter, *player);
  const bool changed = updated_statuses != cache.statuses;
  cache.statuses = updated_statuses;
  FireInventoryAlertEvents(adapter, changed);
}

bool TryDropHeldGuildBankItemToContainer(lua_State* L, const int bag_id,
                                         const int slot) {
  auto& adapter = RequireItemLuaAdapter(L);
  const auto* player = adapter.objects().GetActivePlayer();
  if (player == nullptr ||
      !guild_bank_cursor::HasActiveGuildBankInteraction(L)) {
    return false;
  }

  auto* cursor = ::openwow::ui::game::lua::FindHeldCursor(*L);
  const auto* held_item =
      cursor != nullptr
          ? cursor
                ->get_if<::openwow::game::actions::held_cursor::GuildBankItem>()
          : nullptr;
  if (held_item == nullptr) {
    return false;
  }
  const guild_bank_cursor::GuildBankHeldItemView held_state{
      .item_entry = held_item->item_entry,
      .linear_slot = held_item->linear_slot,
      .split_count = held_item->split_count,
  };

  auto& inventory = adapter.inventory();
  const auto target =
      guild_bank_cursor::ResolveContainerDropTarget(inventory, bag_id, slot);
  if (!target.has_value()) {
    return true;
  }

  const auto source_tab = static_cast<std::uint8_t>(
      held_state.linear_slot /
      ::openwow::game::GuildSystem::kGuildBankSlotsPerTab);
  const auto source_slot = static_cast<std::uint8_t>(
      held_state.linear_slot %
      ::openwow::game::GuildSystem::kGuildBankSlotsPerTab);
  if (adapter.guild().GetControlBankTabWithdrawLimit(player->GetGuildRank(),
                                                     source_tab) == 0) {
    DisplaySystemMessage(guild_bank_cursor::kGuildPermissionsMessage);
    cursor->Clear();
    return true;
  }

  const auto* item_template =
      adapter.queries().GetItemTemplate(held_state.item_entry);
  if (guild_bank_cursor::ShouldCancelHeldGuildBankDropLocally(
          held_state, item_template, target->item)) {
    cursor->Clear();

    guild_bank_cursor::ClearGuildBankItemLockAtLinearSlotAndNotify(
        held_state.item_entry, held_state.linear_slot);
    return true;
  }

  adapter.interaction().SendGuildBankSwapItemsBankToPlayer(
      adapter.guild().GetBankerGuid(), source_tab, source_slot,
      held_state.item_entry, target->player_bag, target->player_slot,
      guild_bank_cursor::ComputeGuildBankHeldItemMoveCount(
          held_state, item_template, target->item));
  cursor->Clear();
  return true;
}

void ValidateTextureArgument(lua_State* L) {
  if (lua_type(L, 1) != LUA_TTABLE) {
    luaL_error(L,
               "Attempt to find 'this' in non-table object (used '.' instead "
               "of ':' ?)");
  }

  if (!HasLuaScriptObjectThis(L, 1)) {
    luaL_error(L, "Attempt to find 'this' in non-framescript object");
  }

  const char* type_name =
      openwow::ui::BorrowRawLuaStringField(L, 1, "__ow_type");
  if (type_name == nullptr || *type_name == '\0') {
    luaL_error(L, "Attempt to find 'this' in non-framescript object");
  }
  if (!TextureMatchesObjectType(type_name)) {
    luaL_error(L, "Wrong object type for member function");
  }
}

void SetTextureOverrideField(lua_State* L, int index,
                             const std::string* normalized_path) {
  index = lua_absindex(L, index);

  ::openwow::ui::game::runtime::SetTextureRenderStateString(
      L, index, ::openwow::ui::game::runtime::TextureRenderStateField::kTexture,
      normalized_path != nullptr && !normalized_path->empty()
          ? std::optional<std::string_view>(*normalized_path)
          : std::optional<std::string_view>{});
}

const ::openwow::game::BagInfo* ResolveLuaBagInfo(
    const ::openwow::game::PlayerInventoryReplica& inventory, const int bag_id,
    const bool bank_open) {
  if (bag_id >= 1 &&
      bag_id <= ::openwow::game::PlayerInventoryReplica::kMaxBags) {
    return inventory.GetBag(static_cast<std::uint8_t>(bag_id));
  }

  if (!bank_open || bag_id < 5 || bag_id > kContainerFrameLuaBagSlotCount) {
    return nullptr;
  }

  return inventory.GetBankBag(static_cast<std::uint8_t>(bag_id - 5));
}

const ::openwow::game::BagInfo* ResolveBagPortraitBagInfo(lua_State* L,
                                                          int zero_based_slot) {
  auto& adapter = RequireItemLuaAdapter(L);
  auto& inventory = adapter.inventory();
  const bool bank_open = adapter.world_session().bank_npc_guid() != 0;
  if (zero_based_slot < 0) {
    return nullptr;
  }

  return ResolveLuaBagInfo(inventory, zero_based_slot + 1, bank_open);
}

bool ResolveInventoryPortraitSlotArgument(lua_State* L, const int index,
                                          int* out_slot) {
  if (out_slot == nullptr) {
    return false;
  }

  int slot = 0;
  if (lua_isnumber(L, index) != 0) {
    slot = static_cast<int>(lua_tonumber(L, index)) - 1;
  } else if (lua_isstring(L, index) != 0) {
    const char* slot_name = lua_tostring(L, index);
    if (const auto* info =
            FindLuaInventorySlotInfo(L, slot_name != nullptr ? slot_name : "");
        info != nullptr) {
      slot = static_cast<int>(info->slot_number);
    }
  }

  *out_slot = slot;
  return IsValidResolvedInventorySlot(slot);
}

const ::openwow::game::ItemInstance* ResolveLuaContainerItem(
    const int bag_id, const int zero_based_slot, lua_State* L) {
  auto& adapter = RequireItemLuaAdapter(L);
  const bool bank_open = adapter.world_session().bank_npc_guid() != 0;
  return detail::ResolveLuaContainerItem(
      adapter.inventory(), adapter.objects().GetActivePlayer() != nullptr,
      bank_open, bag_id, zero_based_slot);
}

bool ActivePlayerHasQuestLogEntry(ItemLuaAdapter& adapter,
                                  const std::uint32_t quest_id) {
  if (quest_id == 0) {
    return false;
  }

  const auto* player = adapter.objects().GetActivePlayer();
  if (player == nullptr) {
    return false;
  }

  for (std::uint8_t slot = 0; slot < ::openwow::game::kMaxQuestLogEntries;
       ++slot) {
    if (player->GetQuestLog(slot).quest_id == quest_id) {
      return true;
    }
  }

  return false;
}

struct ContainerUseContext {
  const ::openwow::game::ItemInstance* item = nullptr;
  std::uint64_t container_guid = 0;
  std::uint8_t server_bag = 0;
  std::uint8_t server_slot = 0;
  bool source_is_bank = false;
  bool source_is_player_bag = false;
};

[[nodiscard]] std::uint32_t ResolveContainerItemInventoryType(
    lua_State* L, ItemLuaAdapter& adapter,
    const ::openwow::game::ItemInstance& item,
    const ::openwow::game::ItemTemplate* item_template) {

  const auto* dbc = adapter.dbc();
  if (dbc != nullptr) {
    if (const auto* entry = dbc->item().LookupEntry(item.entry);
        entry != nullptr) {
      return entry->inventory_type;
    }
  }

  if (item_template != nullptr) {
    return static_cast<std::uint32_t>(item_template->inventory_type);
  }
  if (const auto* cached_item =
          RequireItemLuaAdapter(L).items().GetItem(item.entry);
      cached_item != nullptr) {
    return static_cast<std::uint32_t>(cached_item->inventory_type);
  }
  return 0;
}

std::optional<ContainerUseContext> ResolveContainerUseContext(
    lua_State* L, ItemLuaAdapter& adapter, const int bag_id,
    const int zero_based_slot) {
  if (zero_based_slot < 0 || adapter.objects().GetActivePlayer() == nullptr) {
    return std::nullopt;
  }

  const auto* item = ResolveLuaContainerItem(bag_id, zero_based_slot, L);
  if (item == nullptr || item->IsEmpty()) {
    return std::nullopt;
  }

  const auto& inventory = adapter.inventory();
  const bool bank_open = adapter.world_session().bank_npc_guid() != 0;

  ContainerUseContext context;
  context.item = item;
  context.source_is_bank =
      bag_id == -1 || (bag_id >= 5 && bag_id <= kContainerFrameLuaBagSlotCount);
  context.source_is_player_bag =
      bag_id == 0 ||
      (bag_id >= 1 &&
       bag_id <= ::openwow::game::PlayerInventoryReplica::kMaxBags);

  switch (bag_id) {
    case -2:
      context.container_guid =
          adapter.objects().GetActivePlayerGuid().GetRawValue();
      context.server_bag = ::openwow::game::InventorySlots::kMainBag;
      context.server_slot = static_cast<std::uint8_t>(
          ::openwow::game::InventorySlots::kKeyringStart + zero_based_slot);
      return context;
    case -1:
      if (!bank_open) {
        return std::nullopt;
      }
      context.container_guid =
          adapter.objects().GetActivePlayerGuid().GetRawValue();
      context.server_bag = ::openwow::game::InventorySlots::kMainBag;
      context.server_slot = static_cast<std::uint8_t>(
          ::openwow::game::InventorySlots::kBankStart + zero_based_slot);
      return context;
    case 0:
      context.container_guid =
          adapter.objects().GetActivePlayerGuid().GetRawValue();
      context.server_bag = ::openwow::game::InventorySlots::kMainBag;
      context.server_slot = static_cast<std::uint8_t>(
          ::openwow::game::InventorySlots::kBackpackStart + zero_based_slot);
      return context;
    default:
      break;
  }

  if (bag_id >= 1 &&
      bag_id <= ::openwow::game::PlayerInventoryReplica::kMaxBags) {
    context.container_guid =
        GetContainerGuidForLuaBagSlot(inventory, bag_id, bank_open);
    context.server_bag = static_cast<std::uint8_t>(
        ::openwow::game::InventorySlots::kBagSlotsStart + (bag_id - 1));
    context.server_slot = static_cast<std::uint8_t>(zero_based_slot);
    return context;
  }

  if (bag_id >= 5 && bag_id <= kContainerFrameLuaBagSlotCount && bank_open) {
    context.container_guid =
        GetContainerGuidForLuaBagSlot(inventory, bag_id, true);
    context.server_bag = static_cast<std::uint8_t>(
        ::openwow::game::InventorySlots::kBankBagStart + (bag_id - 5));
    context.server_slot = static_cast<std::uint8_t>(zero_based_slot);
    return context;
  }

  return std::nullopt;
}

const ::openwow::game::ItemTemplate* TryResolveItemTemplate(
    lua_State* L, const std::uint32_t item_id) {
  if (item_id == 0) {
    return nullptr;
  }

  auto& adapter = RequireItemLuaAdapter(L);
  if (const auto* item = adapter.queries().GetItemTemplate(item_id);
      item != nullptr) {
    return item;
  }

  (void)adapter.queries().GetOrRequestItemTemplate(item_id);
  return nullptr;
}

std::uint32_t GetActivePlayerLevelOrZero(lua_State* L) {
  if (const auto* player = RequireItemLuaAdapter(L).objects().GetActivePlayer();
      player != nullptr) {
    return player->State().GetLevel();
  }

  return 0;
}

std::string BuildItemHyperlinkString(
    const std::uint32_t item_id, const std::uint32_t quality,
    const std::uint32_t enchant_id, const std::array<std::uint32_t, 3>& gem_ids,
    const std::int32_t random_property_id, const std::int32_t suffix_factor,
    const std::uint32_t player_level, const std::string& display_name) {
  char link[1024];
  std::snprintf(
      link, sizeof(link), "%s|Hitem:%u:%u:%u:%u:%u:%u:%d:%d:%u|h[%s]|h|r",
      ::openwow::game::ItemTemplate::GetQualityColorInfo(quality)
          .hyperlink_color,
      item_id, enchant_id, gem_ids[0], gem_ids[1], gem_ids[2], 0u,
      random_property_id, suffix_factor, player_level, display_name.c_str());
  return link;
}

std::string LookupItemClassName(lua_State* L, const std::uint32_t class_id) {
  const auto* dbc = GetDbcLoader(L);
  if (dbc == nullptr) {
    return {};
  }

  const auto* entry = dbc->item_class().LookupEntry(class_id);
  if (entry == nullptr || entry->name.empty()) {
    return {};
  }

  return std::string(entry->name);
}

std::string LookupItemSubClassName(lua_State* L, const std::uint32_t class_id,
                                   const std::uint32_t sub_class_id) {
  const auto* dbc = GetDbcLoader(L);
  if (dbc == nullptr) {
    return {};
  }

  const auto* entry =
      dbc->item_sub_class().LookupEntry(class_id * 256u + sub_class_id);
  if (entry == nullptr) {
    return {};
  }

  if (!entry->verbose_name.empty()) {
    return std::string(entry->verbose_name);
  }
  if (!entry->display_name.empty()) {
    return std::string(entry->display_name);
  }

  return {};
}

const char* LookupInventoryTypeToken(const std::uint32_t inv_type) {
  switch (inv_type) {
    case 1:
      return "INVTYPE_HEAD";
    case 2:
      return "INVTYPE_NECK";
    case 3:
      return "INVTYPE_SHOULDER";
    case 4:
      return "INVTYPE_BODY";
    case 5:
      return "INVTYPE_CHEST";
    case 6:
      return "INVTYPE_WAIST";
    case 7:
      return "INVTYPE_LEGS";
    case 8:
      return "INVTYPE_FEET";
    case 9:
      return "INVTYPE_WRIST";
    case 10:
      return "INVTYPE_HAND";
    case 11:
      return "INVTYPE_FINGER";
    case 12:
      return "INVTYPE_TRINKET";
    case 13:
      return "INVTYPE_WEAPON";
    case 14:
      return "INVTYPE_SHIELD";
    case 15:
      return "INVTYPE_RANGED";
    case 16:
      return "INVTYPE_CLOAK";
    case 17:
      return "INVTYPE_2HWEAPON";
    case 18:
      return "INVTYPE_BAG";
    case 19:
      return "INVTYPE_TABARD";
    case 20:
      return "INVTYPE_ROBE";
    case 21:
      return "INVTYPE_WEAPONMAINHAND";
    case 22:
      return "INVTYPE_WEAPONOFFHAND";
    case 23:
      return "INVTYPE_HOLDABLE";
    case 24:
      return "INVTYPE_AMMO";
    case 25:
      return "INVTYPE_THROWN";
    case 26:
      return "INVTYPE_RANGEDRIGHT";
    case 27:
      return "INVTYPE_QUIVER";
    case 28:
      return "INVTYPE_RELIC";
    default:
      return "";
  }
}

std::string BuildContainerItemLink(lua_State* L, ItemLuaAdapter& adapter,
                                   const ::openwow::game::ItemInstance& item) {
  const auto* item_template =
      adapter.queries().GetOrRequestItemTemplate(item.entry);
  const auto raw_quality =
      item_template != nullptr
          ? static_cast<std::uint32_t>(item_template->quality)
          : 1u;
  const std::uint32_t quality = raw_quality < 8u ? raw_quality : 1u;

  std::string display_name;
  if (item_template != nullptr && !item_template->name.empty()) {
    display_name = ResolveLootItemDisplayName(
        GetDbcLoader(L), item_template->name, item.random_property);
  }

  return BuildItemHyperlinkString(
      item.entry, quality, item.GetPermanentEnchant(),
      {item.GetSocketEnchant(0), item.GetSocketEnchant(1),
       item.GetSocketEnchant(2)},
      item.random_property, static_cast<std::int32_t>(item.random_suffix),
      GetActivePlayerLevelOrZero(L), display_name);
}

struct AutoTradeContainerPlacement {
  std::uint8_t trade_slot = 0;
  std::uint8_t source_bag = ::openwow::game::InventorySlots::kMainBag;
  std::uint8_t source_slot = 0;
};

std::optional<AutoTradeContainerPlacement> ResolveUseContainerTradePlacement(
    lua_State* L, ItemLuaAdapter& adapter, const int bag_id,
    const int zero_based_slot, const ::openwow::game::ItemInstance& item,
    const ::openwow::game::TradeInteraction& trade) {
  static_cast<void>(L);
  const bool use_will_not_be_traded_slot = item.IsSoulbound();
  std::optional<std::uint8_t> trade_slot;

  if (use_will_not_be_traded_slot) {
    if (const auto* item_template =
            adapter.queries().GetItemTemplate(item.entry);
        item_template != nullptr && item_template->bonding == 4) {
      adapter.ShowSystemMessage(44);
      return std::nullopt;
    }

    if (trade
            .GetLocalPlayerTradeSlot(::openwow::game::kTradeWillNotBeTradedSlot)
            .has_value()) {
      return std::nullopt;
    }

    trade_slot = ::openwow::game::kTradeWillNotBeTradedSlot;
  } else {
    for (std::uint8_t slot_index = 0;
         slot_index < ::openwow::game::kTradeSlotTradedCount; ++slot_index) {
      if (!trade.GetLocalPlayerTradeSlot(slot_index).has_value()) {
        trade_slot = slot_index;
        break;
      }
    }

    if (!trade_slot.has_value()) {
      return std::nullopt;
    }
  }

  if (const auto* container = adapter.objects().GetContainer(
          ::openwow::game::ObjectGuid(item.guid));
      container != nullptr &&
      container->GetNumFreeSlots() != container->GetNumSlots()) {
    adapter.ShowSystemMessage(47);
    return std::nullopt;
  }

  const auto absolute_slot = adapter.inventory().FindSlotByGuid(item.guid);
  if (absolute_slot >= ::openwow::game::InventorySlots::kBagSlotsStart &&
      absolute_slot < ::openwow::game::InventorySlots::kBagSlotsEnd) {
    adapter.ShowSystemMessage(16);
    return std::nullopt;
  }

  return AutoTradeContainerPlacement{
      .trade_slot = *trade_slot,
      .source_bag = bag_id == 0
                        ? ::openwow::game::InventorySlots::kMainBag
                        : static_cast<std::uint8_t>(
                              ::openwow::game::InventorySlots::kBagSlotsStart +
                              bag_id - 1),
      .source_slot = bag_id == 0
                         ? static_cast<std::uint8_t>(
                               ::openwow::game::InventorySlots::kBackpackStart +
                               zero_based_slot)
                         : static_cast<std::uint8_t>(zero_based_slot),
  };
}

}

int LuaGetContainerNumSlots(lua_State* L) {
  if (!lua_isnumber(L, 1))
    return luaL_error(L, "Usage: GetContainerNumSlots(index)");

  auto& adapter = RequireItemLuaAdapter(L);
  if (adapter.objects().GetActivePlayer() == nullptr) {
    lua_pushnumber(L, 0.0);
    return 1;
  }

  const auto bag_id = static_cast<std::int32_t>(lua_tointeger(L, 1));
  const auto slot_count = adapter.inventory().GetLuaContainerNumSlots(
      bag_id, adapter.world_session().bank_npc_guid() != 0);
  lua_pushnumber(L, static_cast<lua_Number>(slot_count));
  return 1;
}

int LuaGetContainerItemDurability(lua_State* L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: GetContainerItemDurability(index, slot)");
  }

  const int bag_id = static_cast<int>(lua_tonumber(L, 1));
  const int zero_based_slot = static_cast<int>(lua_tonumber(L, 2)) - 1;
  const auto* item = ResolveLuaContainerItem(bag_id, zero_based_slot, L);
  if (item == nullptr || item->max_durability == 0) {
    return 0;
  }

  lua_pushnumber(L, static_cast<lua_Number>(item->durability));
  lua_pushnumber(L, static_cast<lua_Number>(item->max_durability));
  return 2;
}

int LuaGetContainerItemInfo(lua_State* L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2))
    return luaL_error(L, "Usage: GetContainerItemInfo(index, slot)");

  auto& adapter = RequireItemLuaAdapter(L);
  if (adapter.objects().GetActivePlayer() == nullptr) {
    return 0;
  }

  const int bag_id = static_cast<int>(lua_tonumber(L, 1));
  const int slot =
      static_cast<int>(lua_tonumber(L, 2)) - 1;
  const auto* item = ResolveLuaContainerItem(bag_id, slot, L);

  if (!item || item->IsEmpty()) {
    return 0;
  }

  std::string icon_path =
      ResolveItemEntryIconTexturePathOrFallback(L, item->entry);
  const char* item_name = "Item";
  uint32_t quality = item->quality;
  const char* quality_color = GetItemQualityHyperlinkPrefix(1);

  const ::openwow::game::ItemTemplate* item_template = nullptr;
  item_template = adapter.queries().GetItemTemplate(item->entry);
  if (item_template) {
    item_name = item_template->name.c_str();
    quality = static_cast<std::uint32_t>(item_template->quality);
    quality_color = GetItemQualityHyperlinkPrefix(quality);
  }

  {
    const auto* cached = RequireItemLuaAdapter(L).items().GetItem(item->entry);
    if (cached && !cached->name.empty()) {
      item_name = cached->name.c_str();
    }
  }

  lua_pushstring(L, icon_path.c_str());

  lua_pushnumber(L, static_cast<lua_Number>(item->count));

  lua_pushwowbool(L, adapter.world_session().item_locks().IsItemLocked(*item));

  lua_pushnumber(L, static_cast<lua_Number>(quality));

  lua_pushwowbool(
      L, item->IsReadable(item_template != nullptr ? item_template->page_text
                                                   : 0));

  lua_pushwowbool(L, (item->flags & ::openwow::game::ItemFlags::kLootable));

  char link[256];
  std::snprintf(link, sizeof(link), "%s|Hitem:%u:0:0:0:0:0:0:0|h[%s]|h|r",
                quality_color, item->entry, item_name);
  lua_pushstring(L, link);
  return 7;
}

int LuaGetContainerItemLink(lua_State* L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2))
    return luaL_error(L, "Usage: GetContainerItemLink(index, slot)");

  auto& adapter = RequireItemLuaAdapter(L);
  if (adapter.objects().GetActivePlayer() == nullptr) {
    return 0;
  }

  const int bag_id = static_cast<int>(lua_tonumber(L, 1));
  const int slot = static_cast<int>(lua_tonumber(L, 2)) - 1;
  const auto* item = ResolveLuaContainerItem(bag_id, slot, L);

  if (!item || item->IsEmpty()) {
    return 0;
  }

  const auto link = BuildContainerItemLink(L, adapter, *item);
  lua_pushstring(L, link.c_str());
  return 1;
}

int LuaGetContainerNumFreeSlots(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetContainerFreeSlots(index)");
  }

  auto& adapter = RequireItemLuaAdapter(L);
  if (adapter.objects().GetActivePlayer() == nullptr) {
    lua_pushnumber(L, 0.0);
    return 1;
  }

  const int bag_id = static_cast<int>(lua_tonumber(L, 1));
  const bool bank_frame_open = adapter.world_session().bank_npc_guid() != 0;
  auto& inventory = adapter.inventory();

  if (bag_id == 0) {
    lua_pushnumber(L,
                   static_cast<lua_Number>(CountFreeBackpackSlots(inventory)));
    lua_pushnumber(L, 0.0);
    return 2;
  }

  if (bag_id == -1) {
    if (!bank_frame_open) {
      lua_pushnumber(L, 0.0);
      return 1;
    }

    lua_pushnumber(L, static_cast<lua_Number>(CountFreeBankSlots(inventory)));
    lua_pushnumber(L, 0.0);
    return 2;
  }

  const std::uint64_t bag_guid =
      GetContainerGuidForLuaBagSlot(inventory, bag_id, bank_frame_open);
  if (bag_guid == 0) {
    lua_pushnumber(L, 0.0);
    return 1;
  }

  const auto* container =
      adapter.objects().GetContainer(::openwow::game::ObjectGuid(bag_guid));
  if (container == nullptr) {
    lua_pushnumber(L, 0.0);
    return 1;
  }

  lua_pushnumber(L, static_cast<lua_Number>(container->GetNumFreeSlots()));
  lua_pushnumber(L, static_cast<lua_Number>(LookupContainerBagFamilyMask(
                        L, adapter, container->GetEntry())));
  return 2;
}

int LuaGetContainerItemID(lua_State* L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2))
    return luaL_error(L, "Usage: GetContainerItemID(index, slot)");

  auto& adapter = RequireItemLuaAdapter(L);
  if (adapter.objects().GetActivePlayer() == nullptr) {
    return 0;
  }

  const int bag_id = static_cast<int>(lua_tonumber(L, 1));
  const int slot =
      static_cast<int>(lua_tonumber(L, 2)) - 1;
  const auto* item = ResolveLuaContainerItem(bag_id, slot, L);

  if (!item || item->IsEmpty()) {
    return 0;
  }
  lua_pushnumber(L, static_cast<lua_Number>(item->entry));
  return 1;
}

int LuaGetContainerItemCooldown(lua_State* L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2))
    return luaL_error(L, "Usage: GetContainerItemCooldown(index, slot)");

  std::uint32_t duration_ms = 0;
  std::uint32_t start_time_ms = 0;
  bool enabled = false;

  auto& adapter = RequireItemLuaAdapter(L);
  const auto bag_id = static_cast<int>(lua_tonumber(L, 1));
  const auto slot = static_cast<int>(lua_tonumber(L, 2)) - 1;
  const auto* item = ResolveLuaContainerItem(bag_id, slot, L);
  if (item != nullptr && !item->IsEmpty()) {
    const auto* live_item =
        adapter.objects().GetItem(::openwow::game::ObjectGuid(item->guid));
    if (live_item != nullptr) {
      (void)::openwow::game::FillItemCooldownByInventoryItem(
          *live_item, &duration_ms, &start_time_ms, &enabled);
    } else {
      (void)::openwow::game::FillItemCooldownByEntry(adapter.world_session(),
                                                     item->entry, &duration_ms,
                                                     &start_time_ms, &enabled);
    }
  }

  lua_pushnumber(L, static_cast<lua_Number>(start_time_ms) / 1000.0);
  lua_pushnumber(L, static_cast<lua_Number>(duration_ms) / 1000.0);
  lua_pushnumber(L, enabled ? 1.0 : 0.0);
  return 3;
}

int LuaPickupContainerItem(lua_State* L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: PickupContainerItem(index, slot)");
  }

  int bag_id = static_cast<int>(lua_tonumber(L, 1));
  int slot = static_cast<int>(lua_tonumber(L, 2)) - 1;
  auto& adapter = RequireItemLuaAdapter(L);
  auto& inv = adapter.inventory();
  const bool has_active_player = adapter.objects().GetActivePlayer() != nullptr;
  const bool bank_open = adapter.world_session().bank_npc_guid() != 0;

  std::uint64_t target_container_guid = 0;
  std::uint8_t target_server_bag = 0;
  std::uint8_t target_server_slot = 0;
  const auto resolve_target = [&]() -> bool {
    switch (bag_id) {
      case -2:
        if (adapter.objects().GetActivePlayer() == nullptr) {
          return false;
        }
        target_container_guid =
            adapter.objects().GetActivePlayerGuid().GetRawValue();
        target_server_bag = ::openwow::game::InventorySlots::kMainBag;
        target_server_slot = static_cast<std::uint8_t>(
            ::openwow::game::InventorySlots::kKeyringStart + slot);
        return true;
      case -1:
        if (!bank_open || adapter.objects().GetActivePlayer() == nullptr) {
          return false;
        }
        target_container_guid =
            adapter.objects().GetActivePlayerGuid().GetRawValue();
        target_server_bag = ::openwow::game::InventorySlots::kMainBag;
        target_server_slot = static_cast<std::uint8_t>(
            ::openwow::game::InventorySlots::kBankStart + slot);
        return true;
      case 0:
        if (adapter.objects().GetActivePlayer() == nullptr) {
          return false;
        }
        target_container_guid =
            adapter.objects().GetActivePlayerGuid().GetRawValue();
        target_server_bag = ::openwow::game::InventorySlots::kMainBag;
        target_server_slot = static_cast<std::uint8_t>(
            ::openwow::game::InventorySlots::kBackpackStart + slot);
        return true;
      default:
        break;
    }

    if (bag_id >= 1 &&
        bag_id <= ::openwow::game::PlayerInventoryReplica::kMaxBags) {
      target_container_guid =
          GetContainerGuidForLuaBagSlot(inv, bag_id, bank_open);
      target_server_bag = static_cast<std::uint8_t>(
          ::openwow::game::InventorySlots::kBagSlotsStart + (bag_id - 1));
      target_server_slot = static_cast<std::uint8_t>(slot);
      return target_container_guid != 0;
    }

    if (bag_id >= 5 && bag_id <= kContainerFrameLuaBagSlotCount && bank_open) {
      target_container_guid = GetContainerGuidForLuaBagSlot(inv, bag_id, true);
      target_server_bag = static_cast<std::uint8_t>(
          ::openwow::game::InventorySlots::kBankBagStart + (bag_id - 5));
      target_server_slot = static_cast<std::uint8_t>(slot);
      return target_container_guid != 0;
    }

    return false;
  };

  if (TryDropHeldGuildBankItemToContainer(L, bag_id, slot)) {
    return 0;
  }

  const auto* item = has_active_player
                         ? ResolveLuaContainerItem(bag_id, slot, L)
                         : inv.GetContainerSlot(static_cast<uint8_t>(bag_id),
                                                static_cast<uint8_t>(slot));

  if (item != nullptr && !item->IsEmpty()) {
    auto* cursor = ::openwow::ui::game::lua::FindHeldCursor(*L);
    const auto* cursor_item = cursor != nullptr ? cursor->live_item() : nullptr;
    if (cursor_item != nullptr && cursor_item->item.guid == item->guid) {
      cursor->Clear();
      return 0;
    }
  }

  auto& item_targeting = adapter.spells().GetTargeting();
  if (::openwow::game::inventory::ui::HasActiveItemTargetCursor(
          item_targeting)) {
    const auto source_guid =
        ::openwow::game::inventory::ui::GetItemTargetCursorSource(
            item_targeting);
    const auto* source_item = inv.FindItemByGuid(source_guid.GetRawValue());
    const auto* source_template =
        source_item != nullptr
            ? adapter.queries().GetOrRequestItemTemplate(source_item->entry)
            : nullptr;
    if (source_item != nullptr && source_template != nullptr &&
        IsWrappingPaperItem(*source_item, *source_template)) {
      if (item == nullptr || item->IsEmpty() ||
          adapter.world_session().item_locks().IsItemLocked(*item) ||
          !resolve_target()) {
        return 0;
      }

      const auto source_location =
          ::openwow::game::ResolvePlayerItemPacketLocationByGuid(
              inv, source_guid.GetRawValue());
      if (!source_location.has_value()) {
        return 0;
      }

      (void)adapter.interaction().SendWrapItem(
          source_location->packet_bag, source_location->packet_slot,
          target_server_bag, target_server_slot);
      ::openwow::game::inventory::ui::ClearItemTargetCursor(item_targeting);
      return 0;
    }
  }

  auto* held_cursor = ::openwow::ui::game::lua::FindHeldCursor(*L);

  if (held_cursor != nullptr) {
    if (const auto* held_merchant =
            held_cursor->get_if<
                ::openwow::game::actions::held_cursor::MerchantItem>();
        held_merchant != nullptr) {

      if (!resolve_target()) {
        return 0;
      }

      const auto merchant_slot = held_merchant->zero_based_slot + 1;
      const auto quantity =
          held_merchant->auxiliary_value != 0 ? held_merchant->auxiliary_value
                                               : 1u;
      if (SendCursorMerchantItemToTarget(
              adapter.world_session(), merchant_slot, target_container_guid,
              target_server_slot, quantity)) {
        held_cursor->Clear();
      }
      return 0;
    }
  }

  if (held_cursor != nullptr && held_cursor->live_item() != nullptr) {
    std::uint8_t src_bag = 0;
    std::uint8_t src_slot = 0;
    if (!ResolveHeldCursorServerCoords(*held_cursor, &src_bag, &src_slot)) {
      return 0;
    }

    if (!resolve_target()) {
      return 0;
    }

    const auto split_count = held_cursor->live_item()->auxiliary_value;
    if (split_count != 0) {
      adapter.interaction().SendSplitItem(src_bag, src_slot, target_server_bag,
                                          target_server_slot, split_count);
    } else {
      adapter.interaction().SendSwapItem(target_server_bag, target_server_slot,
                                         src_bag, src_slot);
    }

    held_cursor->Clear();
    if (item != nullptr && !item->IsEmpty()) {
      GameUI_OnMouseoverUnitEnter(item->guid);
    }
    return 0;
  }

  if (held_cursor == nullptr || held_cursor->live_item() == nullptr) {
    if (!item || item->IsEmpty()) {
      return 0;
    }

    if (adapter.trade().IsLocalPlayerTradeItemGuid(item->guid)) {
      return 0;
    }

    const auto container_guid = resolve_target() ? target_container_guid : 0;
    (void)item_cursor::PickupItemCursor(
        *held_cursor, adapter.inventory(), adapter.items(), adapter.dbc(),
        adapter.objects().GetActivePlayerGuid(),
        ::openwow::game::ObjectGuid(item->guid),
        {
            .source =
                {
                    .container = ::openwow::game::ObjectGuid(container_guid),
                    .slot = slot,
                },
        });
  }

  return 0;
}

int LuaUseContainerItem(lua_State* L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: UseContainerItem(index, slot[, target])");
  }

  const int bag_id = static_cast<int>(lua_tonumber(L, 1));
  const int slot =
      static_cast<int>(lua_tonumber(L, 2)) - 1;

  auto& item_adapter = RequireItemLuaAdapter(L);

  std::uint64_t target_guid = 0;
  if (lua_gettop(L) >= 3 && !lua_isnil(L, 3)) {
    target_guid = item_adapter.ResolveUnit(SafeLuaString(L, 3)).GetRawValue();
    if (target_guid == 0) {
      return 0;
    }
  }

  const auto context =
      ResolveContainerUseContext(L, item_adapter, bag_id, slot);
  if (!context.has_value() || context->item == nullptr ||
      context->item->IsEmpty() ||
      item_adapter.world_session().item_locks().IsItemLocked(*context->item)) {
    return 0;
  }
  const auto* item = context->item;

  if (auto* session = GetWorldSession(L); session != nullptr) {
    auto& targeting = session->spells().GetTargeting();

    constexpr auto kItemTargets = ::openwow::game::SpellTargetFlag::kItem |
                                  ::openwow::game::SpellTargetFlag::kGoItem;
    const bool pending_targets_item =
        targeting.GetSpellId() != 0 &&
        ::openwow::game::HasFlag(
            static_cast<::openwow::game::SpellTargetFlag>(
                targeting.GetTargetMask()),
            kItemTargets);
    if (targeting.GetSpellId() != 0 && !pending_targets_item) {
      targeting.CancelTargeting();
    }
    if (targeting.GetSpellId() != 0) {
      item_adapter.PromptItemTarget(::openwow::game::ObjectGuid(item->guid));
      return 0;
    }
  }

  if (item_adapter.RepairItem(*item)) {
    return 0;
  }

  if (context->source_is_bank) {
    item_adapter.interaction().SendAutoStoreBankItem(context->server_bag,
                                                     context->server_slot);
    GameUI_OnMouseoverUnitEnter(item->guid);
    return 0;
  }

  if (item_adapter.world_session().bank_npc_guid() != 0 &&
      context->source_is_player_bag) {
    item_adapter.interaction().SendAutoBankItem(context->server_bag,
                                                context->server_slot);
    GameUI_OnMouseoverUnitEnter(item->guid);
    return 0;
  }

  if (const auto vendor = item_adapter.MerchantVendor(); vendor != 0) {
    item_adapter.interaction().SendSellItem(vendor, item->guid, 0);
    GameUI_OnMouseoverUnitEnter(item->guid);
    return 0;
  }

  if (TryAttachSendMailContainerItem(L, *item, context->server_bag,
                                     context->server_slot)) {
    return 0;
  }

  auto& auction_state = RequireAuctionInteraction(L).state();
  if (context->source_is_player_bag && auction_state.IsAtAH() &&
      auction_state.auctions_tab_showing()) {
    AuctionTrySelectSellItem(L, *item, static_cast<std::uint8_t>(bag_id),
                             static_cast<std::uint8_t>(slot), true);
    return 0;
  }

  auto& trade = item_adapter.trade();
  if (context->source_is_player_bag && trade.begin_trade_guid() != 0) {
    const auto placement = ResolveUseContainerTradePlacement(
        L, item_adapter, bag_id, slot, *item, trade);
    if (!placement.has_value()) {
      return 0;
    }

    if (!item_adapter.interaction().SendSetTradeItem(placement->trade_slot,
                                                     placement->source_bag,
                                                     placement->source_slot)) {
      return 0;
    }

    if (!trade.SetLocalPlayerTradeSlot(placement->trade_slot, item->guid,
                                       placement->source_bag,
                                       placement->source_slot)) {
      return 0;
    }
    item_adapter.PresentTradeItemChanged(
        static_cast<int>(placement->trade_slot) + 1);
    GameUI_OnMouseoverUnitEnter(item->guid);
    return 0;
  }

  if (context->source_is_player_bag &&
      item_adapter.DepositGuildBank(L, bag_id, slot)) {
    return 0;
  }

  const auto* item_template =
      item_adapter.queries().GetOrRequestItemTemplate(item->entry);
  if (item_template != nullptr) {
    if (IsWrappingPaperItem(*item, *item_template)) {
      ::openwow::game::inventory::ui::BeginItemTargetCursor(
          item_adapter.spells().GetTargeting(),
          ::openwow::game::ObjectGuid(item->guid));
      return 0;
    }
    if (IsWrappedGiftItem(*item, *item_template)) {
      (void)item_adapter.interaction().SendOpenItem(context->server_bag,
                                                    context->server_slot, true);
      return 0;
    }
  }

  if (item_template != nullptr && item_template->start_quest != 0) {
    item_adapter.interaction().SendQuestGiverQueryQuest(
        item->guid, item_template->start_quest);
    return 0;
  }

  const auto inventory_type =
      ResolveContainerItemInventoryType(L, item_adapter, *item, item_template);
  if (inventory_type != 0) {

    item_adapter.world_session().inventory_commands().RequestAutoEquip({
        .item_guid = item->guid,
        .source_container_guid = context->container_guid,
        .source_slot = static_cast<std::uint32_t>(context->server_slot),
        .item_entry = item->entry,
    });
    return 0;
  }

  if (item_adapter.interaction().TryQueueBindOnUseConfirmation(
          item->guid, item->entry, item->flags, target_guid)) {
    return 0;
  }

  if (target_guid == 0) {

    if (item_template != nullptr &&
        item_template->item_class == ::openwow::game::ItemClass::Glyph) {
      item_adapter.events().FireEvent(events::USE_GLYPH);
    }

    if (item_template != nullptr &&
        item_adapter.StartItemTargeting(*item, *item_template)) {
      return 0;
    }
  }

  item_adapter.interaction().SendUseItem(context->server_bag,
                                         context->server_slot, 0, target_guid);

  return 0;
}

int LuaSplitContainerItem(lua_State* L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2) || !lua_isnumber(L, 3)) {
    return luaL_error(L, "Usage: SplitContainerItem(index, slot, amount)");
  }

  const int bag_id = static_cast<int>(lua_tonumber(L, 1));
  const int slot =
      static_cast<int>(lua_tonumber(L, 2)) - 1;
  const int count = static_cast<int>(lua_tonumber(L, 3));
  if (count <= 0) {
    return 0;
  }

  auto& adapter = RequireItemLuaAdapter(L);
  if (adapter.held_cursor() == nullptr) {
    return 0;
  }

  const auto context = ResolveContainerUseContext(L, adapter, bag_id, slot);
  if (!context.has_value() || context->item == nullptr ||
      adapter.world_session().item_locks().IsItemLocked(*context->item) ||
      static_cast<std::uint32_t>(count) > context->item->count) {
    return 0;
  }

  ::openwow::game::ItemInstance cursor_item = *context->item;
  cursor_item.count = static_cast<uint32_t>(count);
  const int split_count =
      static_cast<std::uint32_t>(count) == context->item->count ? 0 : count;
  auto& cursor = *adapter.held_cursor();
  cursor.Clear();
  namespace held_cursor = ::openwow::game::actions::held_cursor;
  cursor.HoldLiveItem(
      held_cursor::LiveItem{
          .item = cursor_item,
          .source_container_guid = context->container_guid,
          .source_bag = context->server_bag,
          .source_slot = context->server_slot,
          .auxiliary_value = static_cast<std::uint32_t>(split_count),
      },
      held_cursor::Presentation{
          .texture_path = ::openwow::ui::game::detail::cursor_texture::
              ResolveItemTexturePath(L, cursor_item.entry),
          .texture_mode = held_cursor::TextureMode::HeldTexture,
          .sound = held_cursor::Sound::CursorGrabObject,

          .grid = ::openwow::game::inventory::ui::ResolveItemCursorGrid(
              adapter.items(), cursor_item.entry),
      });

  return 0;
}

int LuaGetBagName(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetBagName(index)");
  }

  auto& adapter = RequireItemLuaAdapter(L);
  if (adapter.objects().GetActivePlayer() == nullptr) {
    lua_pushnil(L);
    return 1;
  }

  const int bag_id = static_cast<int>(lua_tonumber(L, 1));
  if (bag_id == 0) {

    const auto backpack_name =
        ::openwow::game::ResolveLocalizedGlobalString(L, "BACKPACK_TOOLTIP");
    lua_pushlstring(L, backpack_name.c_str(), backpack_name.size());
    return 1;
  }

  const auto& inventory = adapter.inventory();
  const bool bank_open = adapter.world_session().bank_npc_guid() != 0;
  const auto* bag_info = ResolveLuaBagInfo(inventory, bag_id, bank_open);
  if (bag_info != nullptr && !bag_info->IsEmpty()) {
    const auto* item_template =
        adapter.queries().GetOrRequestItemTemplate(bag_info->entry);
    if (item_template != nullptr) {
      lua_pushlstring(L, item_template->name.c_str(),
                      item_template->name.size());
      return 1;
    }
  }

  lua_pushnil(L);
  return 1;
}

int LuaGetItemInfo(lua_State* L) {
  int item_id = 0;
  std::optional<::openwow::game::ItemLinkData> parsed_link;
  if (lua_isnumber(L, 1)) {
    item_id = static_cast<int>(lua_tonumber(L, 1));
  } else if (lua_isstring(L, 1)) {
    const char* s = lua_tostring(L, 1);
    if (s != nullptr) {
      parsed_link = ::openwow::game::ItemLinkParser::Parse(s);
      item_id = parsed_link.has_value()
                    ? static_cast<int>(parsed_link->itemId)
                    : static_cast<int>(ResolveItemIdArg(L, 1));
    }
  } else {
    return luaL_error(L, "Usage: GetItemInfo(itemID|\"name\"|\"itemlink\")");
  }

  if (item_id <= 0) {
    return 0;
  }

  const auto entry = static_cast<std::uint32_t>(item_id);
  const auto* item = TryResolveItemTemplate(L, entry);
  if (item == nullptr) {
    return 0;
  }

  const std::uint32_t enchant_id =
      parsed_link.has_value() ? parsed_link->enchantId : 0u;
  const std::array<std::uint32_t, 3> gem_ids =
      parsed_link.has_value() ? parsed_link->gemIds
                              : std::array<std::uint32_t, 3>{};
  const std::int32_t random_property_id =
      parsed_link.has_value() ? parsed_link->randomPropertyId : 0;
  const std::int32_t suffix_factor =
      parsed_link.has_value() ? parsed_link->suffixFactor : 0;
  const auto display_name = ResolveLootItemDisplayName(
      GetDbcLoader(L), item->name, random_property_id);
  const auto hyperlink = BuildItemHyperlinkString(
      entry, static_cast<std::uint32_t>(item->quality), enchant_id, gem_ids,
      random_property_id, suffix_factor, GetActivePlayerLevelOrZero(L),
      display_name);

  lua_pushstring(L, display_name.c_str());
  lua_pushstring(L, hyperlink.c_str());
  lua_pushnumber(
      L, static_cast<lua_Number>(static_cast<std::uint32_t>(item->quality)));
  lua_pushnumber(L, static_cast<lua_Number>(item->item_level));
  lua_pushnumber(L, static_cast<lua_Number>(item->required_level));

  const auto item_class = static_cast<std::uint32_t>(item->item_class);
  const auto class_name = LookupItemClassName(L, item_class);
  lua_pushstring(L, class_name.c_str());

  const auto sub_class_name =
      LookupItemSubClassName(L, item_class, item->subclass);
  lua_pushstring(L, sub_class_name.c_str());

  lua_pushnumber(L, static_cast<lua_Number>(std::max(item->stackable, 1u)));
  lua_pushstring(L, LookupInventoryTypeToken(
                        static_cast<std::uint32_t>(item->inventory_type)));
  const auto texture =
      ResolveItemDisplayIdIconTexturePathOrFallback(L, item->display_id);
  lua_pushstring(L, texture.c_str());
  lua_pushnumber(L, static_cast<lua_Number>(item->sell_price));
  return 11;
}

int LuaGetItemCount(lua_State* L) {
  const auto item_id = static_cast<int>(ResolveItemIdArg(L, 1));
  if (item_id <= 0) {
    lua_pushnumber(L, 0);
    return 1;
  }

  const bool include_bank = lua_gettop(L) >= 2 && lua_toboolean(L, 2) != 0;
  const bool include_uses = lua_gettop(L) >= 3 && lua_toboolean(L, 3) != 0;

  auto& inv = RequireItemLuaAdapter(L).inventory();
  auto count =
      inv.GetItemCount(static_cast<std::uint32_t>(item_id), include_bank);

  if (include_uses && count != 0) {
    const auto* item_tmpl = RequireItemLuaAdapter(L).items().GetItem(
        static_cast<std::uint32_t>(item_id));
    if (item_tmpl != nullptr) {
      const auto* dbc = RequireItemLuaAdapter(L).dbc();
      count = static_cast<std::uint32_t>(
          std::max(0, ::openwow::game::ComputeDisplayedInventoryItemCount(
                          inv, static_cast<std::uint32_t>(item_id), count,
                          *item_tmpl, dbc)));
    }
  }

  lua_pushnumber(L, static_cast<lua_Number>(count));
  return 1;
}

int LuaGetItemQualityColor(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetItemQualityColor(index)");
  }

  const int quality = static_cast<int>(lua_tonumber(L, 1));
  const auto quality_index =
      quality < 0 ? 8u : static_cast<std::uint32_t>(quality);
  const auto color =
      ::openwow::game::ItemTemplate::GetQualityColorInfo(quality_index);

  lua_pushnumber(L, static_cast<lua_Number>(color.red) / 255.0);
  lua_pushnumber(L, static_cast<lua_Number>(color.green) / 255.0);
  lua_pushnumber(L, static_cast<lua_Number>(color.blue) / 255.0);
  lua_pushstring(L, color.hyperlink_color);
  return 4;
}

int LuaGetItemIcon(lua_State* L) {

  const auto item_id = ResolveItemIdArg(L, 1);
  if (item_id == 0) return 0;
  const auto icon_path = TryResolveItemEntryIconTexturePath(L, item_id);
  if (!icon_path.has_value()) return 0;
  lua_pushstring(L, icon_path->c_str());
  return 1;
}

int LuaGetMoney(lua_State* L) {
  const auto* player = RequireItemLuaAdapter(L).objects().GetLocalPlayer();
  if (!player) {
    lua_pushnumber(L, 0);
    return 1;
  }

  uint32_t copper = player->GetUInt32(::openwow::game::PLAYER_FIELD_COINAGE);
  lua_pushnumber(L, static_cast<lua_Number>(copper));
  return 1;
}

int LuaContainerIDToInventoryID(lua_State* L) {
  if (lua_isnumber(L, 1) == 0) {
    return luaL_error(L, "Usage: ContainerIDToInventoryID(containerID)");
  }

  const int container_id = openwow::ui::TruncateLuaNumberToI32(lua_tonumber(L, 1));
  if (container_id >= kFirstCarriedContainerId &&
      container_id <= kLastCarriedContainerId) {
    lua_pushnumber(
        L, static_cast<lua_Number>(kFirstCarriedContainerInventorySlot +
                                   container_id - kFirstCarriedContainerId));
    return 1;
  }
  if (container_id >= kFirstBankContainerId &&
      container_id <= kLastBankContainerId) {
    lua_pushnumber(L, static_cast<lua_Number>(kFirstBankContainerInventorySlot +
                                               container_id -
                                               kFirstBankContainerId));
    return 1;
  }
  return luaL_error(L, "ContainerIDToInventoryID(): invalid container ID");
}

int LuaGetContainerItemQuestInfo(lua_State* L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: GetContainerItemQuestInfo(index, slot)");
  }

  auto& adapter = RequireItemLuaAdapter(L);
  const int bag_id = static_cast<int>(lua_tonumber(L, 1));
  const int zero_based_slot = static_cast<int>(lua_tonumber(L, 2)) - 1;
  const auto* item = ResolveLuaContainerItem(bag_id, zero_based_slot, L);
  if (item == nullptr || item->IsEmpty()) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    return 3;
  }

  const auto* item_template =
      adapter.queries().GetOrRequestItemTemplate(item->entry);
  if (item_template == nullptr) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    return 3;
  }

  if (item_template->bonding == kQuestItemBonding) {
    lua_pushboolean(L, 1);
  } else {
    lua_pushnil(L);
  }

  if (item_template->start_quest != 0) {
    lua_pushnumber(L, static_cast<lua_Number>(item_template->start_quest));
    lua_pushboolean(
        L, ActivePlayerHasQuestLogEntry(adapter, item_template->start_quest)
               ? 1
               : 0);
  } else {
    lua_pushnil(L);
    lua_pushnil(L);
  }

  return 3;
}

int LuaGetInventoryAlertStatus(lua_State* L) {
  if (!lua_isnumber(L, 1))
    return luaL_error(L, "Usage: GetInventoryAlertStatus(index)");

  EnsureInventoryAlertProjectionInitialized(RequireItemLuaAdapter(L));

  const auto zero_based_index =
      openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1)) - 1u;
  const auto& cache = RequireItemLuaAdapter(L).inventory_alerts();
  const int status = zero_based_index < cache.statuses.size()
                         ? cache.statuses[zero_based_index]
                         : 0;
  lua_pushnumber(L, static_cast<lua_Number>(status));
  return 1;
}

int LuaUpdateInventoryAlertStatus(lua_State* L) {
  RefreshInventoryAlertState(RequireItemLuaAdapter(L));
  return 0;
}

int LuaSetBagPortraitTexture(lua_State* L) {
  ValidateTextureArgument(L);

  if (!lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: SetBagPortraitTexture(texture, slot)");
  }

  const int zero_based_slot = static_cast<int>(lua_tonumber(L, 2)) - 1;
  if (zero_based_slot < 0) {
    return 0;
  }
  if (zero_based_slot >= kBagPortraitLuaSlotCount) {
    return luaL_error(L, "Invalid slot in SetBagPortraitTexture");
  }

  SetTextureOverrideField(L, 1, nullptr);

  const auto* bag_info = ResolveBagPortraitBagInfo(L, zero_based_slot);
  if (bag_info == nullptr || bag_info->IsEmpty()) {
    return 0;
  }

  const std::string icon_path =
      ResolveItemEntryIconTexturePathOrFallback(L, bag_info->entry);
  const std::string portrait_texture_key =
      openwow::render::BuildPortraitIconTextureKey(icon_path);
  SetTextureOverrideField(L, 1, &portrait_texture_key);
  return 0;
}

int LuaSetInventoryPortraitTexture(lua_State* L) {
  ValidateTextureArgument(L);
  SetTextureOverrideField(L, 1, nullptr);

  if (lua_isstring(L, 2) == 0) {
    return luaL_error(
        L, "Usage: SetInventoryPortraitTexture(texture, unit, slot)");
  }

  int slot = 0;
  if (!ResolveInventoryPortraitSlotArgument(L, 3, &slot)) {
    return 0;
  }

  auto& adapter = RequireItemLuaAdapter(L);
  const auto guid = adapter.ResolveUnit(SafeLuaString(L, 2));
  const auto* player = adapter.objects().GetPlayer(guid);
  if (player == nullptr) {
    return 0;
  }

  std::uint32_t item_entry = 0;
  if (guid == adapter.objects().GetActivePlayerGuid()) {
    if (const auto* item = GetLocalInventoryItemByAbsoluteSlot(L, slot);
        item != nullptr) {
      item_entry = item->GetEntry();
    }
  } else if (guid.GetRawValue() == adapter.InspectTarget() && slot >= 0 &&
             slot <= 18) {
    item_entry = player->GetVisibleItemEntry(static_cast<std::uint8_t>(slot));
  }
  if (item_entry == 0) {
    return 0;
  }

  const std::string icon_path =
      ResolveItemEntryIconTexturePathOrFallback(L, item_entry);
  const std::string portrait_texture_key =
      openwow::render::BuildPortraitIconTextureKey(icon_path);
  SetTextureOverrideField(L, 1, &portrait_texture_key);
  return 0;
}

int LuaUseInventoryItem(lua_State* L) {
  int slot = 0;
  if (!ResolveInventorySlotArgument(L, 1, &slot) || slot == -1) {
    return 0;
  }

  auto& adapter = RequireItemLuaAdapter(L);
  auto& inventory = adapter.inventory();
  const auto* item = inventory.GetEquipSlot(static_cast<uint8_t>(slot));
  if (!item || item->IsEmpty()) {
    return 0;
  }

  if (auto* cursor = ::openwow::ui::game::lua::FindHeldCursor(*L)) {
    cursor->Clear();
  }

  if (const auto* session = GetWorldSession(L);
      session != nullptr &&
      session->spells().GetTargeting().GetSpellId() != 0) {
    adapter.PromptItemTarget(::openwow::game::ObjectGuid(item->guid));
    return 0;
  }

  if (adapter.RepairItem(*item)) {
    return 0;
  }

  std::uint64_t target_guid = 0;
  if (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) {
    target_guid = adapter.ResolveUnit(SafeLuaString(L, 2)).GetRawValue();
    if (target_guid == 0) {
      return 0;
    }
  }

  if (adapter.interaction().TryQueueBindOnUseConfirmation(
          item->guid, item->entry, item->flags, target_guid)) {
    return 0;
  }

  if (target_guid == 0) {
    if (const auto* item_template =
            adapter.queries().GetOrRequestItemTemplate(item->entry);
        item_template != nullptr &&
        adapter.StartItemTargeting(*item, *item_template)) {
      return 0;
    }
  }

  adapter.interaction().SendUseItem(255, static_cast<uint8_t>(slot), 0,
                                    target_guid);
  return 0;
}

}
