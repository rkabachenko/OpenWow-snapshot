
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/inventory/search/action_item_inventory_search.h"
#include "openwow/game/action_validation_utils.h"
#include "openwow/game/inventory/search/action_item_inventory_search.h"
#include "openwow/game/cooldown_tracker.h"
#include "openwow/game/hyperlink.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/items/adapters/lua/item_lua_adapter.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/spell_missile_runtime.h"
#include "openwow/ui/game/loot_tooltip_support.h"
#include "openwow/ui/game/merchant_repair_cost.h"

namespace openwow::ui::game::detail {

constexpr int kEquipmentSlotStart = 0;
constexpr int kEquipmentSlotEnd   = 18;

struct ItemCooldownResult {
  double start = 0.0;
  double duration = 0.0;
  double enabled = 1.0;

  [[nodiscard]] double ExpiresAt() const {
    return start + duration;
  }
};

static const openwow::game::ItemTemplate* ResolveItemCooldownTemplate(
    ItemLuaAdapter& adapter, std::uint32_t item_id) {
  return adapter.queries().GetItemTemplate(item_id);
}

static std::uint32_t ResolveInventoryItemUseSpellId(
    lua_State* L,
    const openwow::game::ItemInstance& item,
    const openwow::game::ItemTemplate& item_template) {
  const auto* dbc = GetDbcLoader(L);
  return openwow::game::ResolveItemInstanceUseSpellId(item, item_template, dbc);
}

static std::uint32_t ResolveItemCooldownSpellId(
    lua_State* L,
    ItemLuaAdapter& adapter,
    std::uint32_t item_id) {
  const auto* item_template = ResolveItemCooldownTemplate(adapter, item_id);
  if (item_template == nullptr) {
    return 0;
  }

  const auto* dbc = adapter.dbc();

  if (const auto* action_item =
          openwow::game::FindActionBarInventoryItemByEntry(
              adapter.inventory(), item_id, item_template, dbc);
      action_item != nullptr) {
    if (const auto spell_id =
            ResolveInventoryItemUseSpellId(L, *action_item, *item_template);
        spell_id != 0) {
      return spell_id;
    }
  }

  const auto item_slot = RequireItemLuaAdapter(L).inventory().FindItemByEntry(
      item_id);
  if (item_slot >= 0) {
    if (const auto* item = RequireItemLuaAdapter(L).inventory().GetItemInSlot(
            static_cast<std::uint8_t>(item_slot));
        item != nullptr) {
      if (const auto spell_id =
              ResolveInventoryItemUseSpellId(L, *item, *item_template);
          spell_id != 0) {
        return spell_id;
      }
    }
  }

  if (const auto* item_spell = FindItemOnUseSpell(*item_template);
      item_spell != nullptr) {
    return item_spell->spell_id;
  }

  return 0;
}

static std::optional<ItemCooldownResult> ResolveSpellbookItemCooldown(
    const openwow::game::SpellBook& spell_book, std::uint32_t spell_id) {
  const auto it = spell_book.cooldowns().find(spell_id);
  if (it == spell_book.cooldowns().end()) {
    return std::nullopt;
  }

  const auto duration_ms =
      std::max(it->second.cooldown_ms, it->second.category_cooldown_ms);
  if (duration_ms == 0) {
    return std::nullopt;
  }

  return ItemCooldownResult{
      .start = it->second.start_time_s,
      .duration = static_cast<double>(duration_ms) / 1000.0,
      .enabled = 1.0,
  };
}

static std::optional<ItemCooldownResult> ResolveTrackedItemCooldown(
    ItemLuaAdapter& adapter, const std::uint32_t item_id) {
  const auto* cooldown = adapter.cooldowns().GetItemCooldown(item_id);
  if (cooldown == nullptr || cooldown->duration == 0) {
    return std::nullopt;
  }

  const auto current_time_ms = adapter.MonotonicMilliseconds();
  if (cooldown->IsReady(current_time_ms)) {
    return std::nullopt;
  }

  return ItemCooldownResult{
      .start = static_cast<double>(cooldown->start_time) / 1000.0,
      .duration = static_cast<double>(cooldown->duration) / 1000.0,
      .enabled = 1.0,
  };
}

static int PushItemCooldownResult(lua_State* L,
                                  const ItemCooldownResult& cooldown) {
  lua_pushnumber(L, static_cast<lua_Number>(cooldown.start));
  lua_pushnumber(L, static_cast<lua_Number>(cooldown.duration));
  lua_pushnumber(L, static_cast<lua_Number>(cooldown.enabled));
  return 3;
}

static int PushUnavailableInventoryItemCooldown(lua_State* L) {
  return PushItemCooldownResult(
      L, ItemCooldownResult{.start = 0.0, .duration = 0.0, .enabled = 0.0});
}

static const openwow::game::CGItem_C* GetEquippedItem(
    ItemLuaAdapter& adapter,
    const openwow::game::CGPlayer_C& player,
    int slot) {
  if (slot < kEquipmentSlotStart || slot > kEquipmentSlotEnd)
    return nullptr;

  const std::uint16_t field_idx =
      static_cast<std::uint16_t>(PLAYER_FIELD_INV_SLOT_HEAD + slot * 2);
  const ObjectGuid item_guid = player.GetGuidField(field_idx);

  if (item_guid == ObjectGuid()) return nullptr;

  return adapter.objects().GetItem(item_guid);
}

struct InventoryUnitSlotArgument {
  std::string unit_token;

  int slot = 0;
};

static bool ParseInventoryUnitSlotArgument(
    lua_State* L, const char* function_name,
    InventoryUnitSlotArgument* out_argument) {
  if (out_argument == nullptr) {
    return false;
  }

  if (lua_isstring(L, 1) == 0) {
    luaL_error(L, "Usage: %s(unit, slot)", function_name);
    return false;
  }

  out_argument->unit_token = SafeLuaString(L, 1);
  return ResolveInventorySlotArgument(L, 2, &out_argument->slot);
}

static std::optional<InventoryQueryOwner> ResolveInventoryOwner(
    ItemLuaAdapter& adapter, const std::string_view unit_token) {
  const auto guid = adapter.ResolveUnit(unit_token);
  const auto* active = adapter.objects().GetLocalPlayerTyped();
  if (active == nullptr || guid.IsEmpty()) {
    return std::nullopt;
  }
  if (guid == active->GetGuid()) {
    return InventoryQueryOwner{active, true};
  }
  if (guid.GetRawValue() != adapter.InspectTarget()) {
    return std::nullopt;
  }
  const auto* inspected = adapter.objects().GetPlayer(guid);
  return inspected != nullptr
             ? std::optional<InventoryQueryOwner>{
                   InventoryQueryOwner{inspected, false}}
             : std::nullopt;
}

static const openwow::game::CGItem_C* ResolveInventoryQueryItemObject(
    lua_State* L, ItemLuaAdapter& adapter,
    const InventoryQueryOwner& owner, const int slot) {
  if (owner.player == nullptr || slot == -1) {
    return nullptr;
  }

  if (owner.is_active_player) {
    return GetLocalInventoryItemByAbsoluteSlot(L, slot);
  }

  return GetEquippedItem(adapter, *owner.player, slot);
}

static std::optional<std::uint32_t> ResolveInventoryItemEntry(
    lua_State* L, ItemLuaAdapter& adapter, const InventoryQueryOwner& owner,
    const int slot) {
  if (owner.player == nullptr) {
    return std::nullopt;
  }
  if (slot == -1) {

    if (!owner.is_active_player) {
      return std::nullopt;
    }
    const auto ammo = owner.player->GetUInt32(PLAYER_AMMO_ID);
    return ammo != 0 ? std::optional<std::uint32_t>{ammo} : std::nullopt;
  }
  if (owner.is_active_player) {
    const auto* item =
        ResolveInventoryQueryItemObject(L, adapter, owner, slot);
    return item != nullptr && item->GetEntry() != 0
               ? std::optional<std::uint32_t>{item->GetEntry()}
               : std::nullopt;
  }
  if (slot < kEquipmentSlotStart || slot > kEquipmentSlotEnd) {
    return std::nullopt;
  }
  const auto entry =
      owner.player->GetVisibleItemEntry(static_cast<std::uint8_t>(slot));
  return entry != 0 ? std::optional<std::uint32_t>{entry} : std::nullopt;
}

static std::optional<std::string> ResolvePlayerInventoryTexturePath(
    lua_State* L, ItemLuaAdapter& adapter,
    const InventoryQueryOwner& owner, const int slot) {
  if (owner.player == nullptr) {
    return std::nullopt;
  }

  const auto& player = *owner.player;
  if (slot == -1) {
    if (!owner.is_active_player) {
      return std::nullopt;
    }

    const auto ammo_item_id = player.GetUInt32(PLAYER_AMMO_ID);
    if (ammo_item_id == 0) {
      return std::nullopt;
    }

    const auto* item_template =
        adapter.queries().GetOrRequestItemTemplate(ammo_item_id);
    if (item_template == nullptr || item_template->display_id == 0) {
      return std::nullopt;
    }

    return ResolveItemDisplayIdIconTexturePathOrFallback(
        L, item_template->display_id);
  }

  if (slot >= kEquipmentSlotStart && slot <= kEquipmentSlotEnd &&
      player.GetVisibleItemEntry(static_cast<std::uint8_t>(slot)) != 0) {
    return adapter.VisibleItemIcon(
        L, player, static_cast<std::uint8_t>(slot));
  }

  if (!owner.is_active_player) {
    return std::nullopt;
  }

  const auto* item = GetLocalInventoryItemByAbsoluteSlot(L, slot);
  if (item == nullptr || item->IsPendingRemoval()) {
    return std::nullopt;
  }

  const auto entry = item->GetEntry();
  if (entry == 0) {
    return std::nullopt;
  }

  if (const auto* item_template =
          adapter.queries().GetOrRequestItemTemplate(entry);
      item_template != nullptr && item_template->display_id != 0) {
    return ResolveItemDisplayIdIconTexturePathOrFallback(
        L, item_template->display_id);
  }

  const auto* cached_item = RequireItemLuaAdapter(L).items().GetItem(entry);
  return cached_item != nullptr && cached_item->display_id != 0
             ? std::optional<std::string>{
                   ResolveItemDisplayIdIconTexturePathOrFallback(
                       L, cached_item->display_id)}
             : std::nullopt;
}

static std::optional<std::string> ResolveInventoryItemTexturePath(
    lua_State* L, ItemLuaAdapter& adapter,
    std::string_view unit_token, const int slot) {
  const auto owner = ResolveInventoryOwner(adapter, unit_token);
  if (!owner.has_value()) {
    return std::nullopt;
  }

  return ResolvePlayerInventoryTexturePath(L, adapter, *owner, slot);
}

int LuaGetInventoryItemDurability(lua_State* L) {
  if (!lua_isnumber(L, 1))
    return luaL_error(L, "Usage: GetInventoryItemDurability(slot)");

  int slot = 0;
  if (!ResolveInventorySlotArgument(L, 1, &slot)) {
    return 0;
  }

  if (slot == -1) {
    return 0;
  }

  const auto* item = GetLocalInventoryItemByAbsoluteSlot(L, slot);
  if (!item) {
    return 0;
  }

  const auto current = item->GetDurability();
  const auto max_dur = item->GetMaxDurability();

  if (max_dur == 0) {
    return 0;
  }

  lua_pushnumber(L, static_cast<double>(current));
  lua_pushnumber(L, static_cast<double>(max_dur));
  return 2;
}

int LuaGetInventoryItemTexture(lua_State* L) {
  InventoryUnitSlotArgument argument;
  if (!ParseInventoryUnitSlotArgument(
          L, "GetInventoryItemTexture", &argument)) {
    return 0;
  }

  auto& adapter = RequireItemLuaAdapter(L);
  const auto icon_path = ResolveInventoryItemTexturePath(
      L, adapter, argument.unit_token, argument.slot);
  if (!icon_path.has_value()) {
    lua_pushnil(L);
    return 1;
  }

  lua_pushstring(L, icon_path->c_str());
  return 1;
}

int LuaGetInventoryItemBroken(lua_State* L) {
  InventoryUnitSlotArgument argument;
  if (!ParseInventoryUnitSlotArgument(
          L, "GetInventoryItemBroken", &argument)) {
    return 0;
  }

  auto& adapter = RequireItemLuaAdapter(L);
  const auto owner = ResolveInventoryOwner(adapter, argument.unit_token);
  if (!owner.has_value()) {
    lua_pushnil(L);
    return 1;
  }

  const auto* item = ResolveInventoryQueryItemObject(
      L, adapter, *owner, argument.slot);
  if (!item) {
    lua_pushnil(L);
    return 1;
  }

  const auto current = item->GetDurability();
  const auto max_dur = item->GetMaxDurability();

  if (max_dur > 0 && current == 0) {
    lua_pushnumber(L, 1);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaGetRepairAllCost(lua_State* L) {
  const auto quote = RequireItemLuaAdapter(L).RepairQuote();
  if (!quote.has_value()) {
    lua_pushnumber(L, 0);
    return 1;
  }
  lua_pushnumber(L, static_cast<lua_Number>(quote->first));
  if (quote->second) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 2;
}

int LuaGetItemCooldown(lua_State* L) {
  const auto item_id = ResolveItemIdArg(L, 1);
  auto& adapter = RequireItemLuaAdapter(L);
  if (item_id == 0) {
    return PushItemCooldownResult(L, ItemCooldownResult{});
  }

  std::optional<ItemCooldownResult> best_cooldown =
      ResolveTrackedItemCooldown(adapter, item_id);
  if (const auto spell_id = ResolveItemCooldownSpellId(L, adapter, item_id);
      spell_id != 0) {
    if (const auto spellbook_cooldown =
            ResolveSpellbookItemCooldown(adapter.spell_book(), spell_id);
        spellbook_cooldown.has_value() &&
        (!best_cooldown.has_value() ||
         spellbook_cooldown->ExpiresAt() > best_cooldown->ExpiresAt())) {
      best_cooldown = spellbook_cooldown;
    }
  }

  if (best_cooldown.has_value()) {
    return PushItemCooldownResult(L, *best_cooldown);
  }

  return PushItemCooldownResult(L, ItemCooldownResult{});
}

int LuaGetInventorySlotInfo(lua_State* L) {
  if (lua_isstring(L, 1) != 0) {
    const char* slot_name = lua_tostring(L, 1);
    if (const auto* slot_info =
            FindLuaInventorySlotInfo(L, slot_name != nullptr ? slot_name : "")) {
      lua_pushnumber(L, static_cast<lua_Number>(slot_info->slot_number));
      lua_pushlstring(L, slot_info->slot_icon.data(), slot_info->slot_icon.size());
      lua_pushwowbool(L, slot_info->slot_number == 18);
      return 3;
    }
  }

  return luaL_error(L, "Invalid inventory slot in GetInventorySlotInfo");
}

int LuaGetInventoryItemID(lua_State* L) {
  InventoryUnitSlotArgument argument;
  if (!ParseInventoryUnitSlotArgument(L, "GetInventoryItemID", &argument)) {
    return 0;
  }

  auto& adapter = RequireItemLuaAdapter(L);
  const auto owner = ResolveInventoryOwner(adapter, argument.unit_token);
  const auto item_entry =
      owner.has_value()
          ? ResolveInventoryItemEntry(L, adapter, *owner, argument.slot)
          : std::nullopt;
  if (!item_entry.has_value()) {
    return 0;
  }

  lua_pushnumber(L, static_cast<lua_Number>(*item_entry));
  return 1;
}

int LuaGetInventoryItemLink(lua_State* L) {
  InventoryUnitSlotArgument argument;
  if (!ParseInventoryUnitSlotArgument(
          L, "GetInventoryItemLink", &argument)) {
    return 0;
  }

  auto& adapter = RequireItemLuaAdapter(L);
  const auto owner = ResolveInventoryOwner(adapter, argument.unit_token);
  if (!owner.has_value() || owner->player == nullptr) {
    return 0;
  }

  const auto item_entry =
      ResolveInventoryItemEntry(L, adapter, *owner, argument.slot);
  if (!item_entry.has_value()) {
    return 0;
  }

  const auto* item_template =
      adapter.queries().GetOrRequestItemTemplate(*item_entry);
  if (item_template == nullptr) {
    return 0;
  }

  std::string link;
  if (owner->is_active_player && argument.slot != -1) {
    const auto* item = ResolveInventoryQueryItemObject(
        L, adapter, *owner, argument.slot);
    if (item == nullptr) {
      return 0;
    }

    const auto* dbc = adapter.dbc();
    link = BuildItemHyperlinkStringFromObject(
        item,
        ItemHyperlinkTemplateView{
            .name = item_template->name,
            .quality = static_cast<std::uint32_t>(item_template->quality),
        },
        dbc, owner->player->State().GetLevel());
  } else {
    const auto enchant =
        !owner->is_active_player && argument.slot >= kEquipmentSlotStart &&
                argument.slot <= kEquipmentSlotEnd
            ? static_cast<std::int32_t>(owner->player->GetVisibleItemEnchant(
                  static_cast<std::uint8_t>(argument.slot)))
            : 0;
    const auto* active_player = adapter.objects().GetLocalPlayerTyped();
    link = openwow::game::HyperlinkParser::BuildItemLink(
        *item_entry, item_template->name,
        static_cast<std::uint32_t>(item_template->quality), enchant,
        0, 0, 0, 0, 0,
        active_player != nullptr
            ? static_cast<std::int32_t>(active_player->State().GetLevel())
            : 0);
  }

  if (link.empty()) {
    return 0;
  }

  lua_pushlstring(L, link.data(), link.size());
  return 1;
}

[[nodiscard]] static std::optional<std::uint32_t> ResolveSpecialBagFreeSlotCount(
    lua_State* L, const openwow::game::CGItem_C& item, const int slot) {
  if (!item.IsContainer() || slot < openwow::game::InventorySlots::kEquipStart ||
      slot >= openwow::game::InventorySlots::kBagSlotsEnd) {
    return std::nullopt;
  }

  const auto* const dbc = GetDbcLoader(L);
  if (dbc == nullptr) {
    return std::nullopt;
  }

  const auto* const sub_class = dbc->item_sub_class().LookupEntry(
      openwow::data::dbc::ItemSubClassEntry::ComposeKey(
          item.GetItemClassFromClientDbc(), item.GetItemSubClassFromClientDbc()));
  if (sub_class == nullptr) {
    return std::nullopt;
  }

  constexpr std::uint32_t kItemSubClassDisplayFlagCountFreeSlots = 0x4u;
  if ((sub_class->display_flags & kItemSubClassDisplayFlagCountFreeSlots) == 0u) {
    return std::uint32_t{0};
  }

  return static_cast<const openwow::game::CGContainer_C&>(item).GetNumFreeSlots();
}

int LuaGetInventoryItemCount(lua_State* L) {
  InventoryUnitSlotArgument argument;
  if (!ParseInventoryUnitSlotArgument(
          L, "GetInventoryItemCount", &argument)) {
    return 0;
  }

  auto& adapter = RequireItemLuaAdapter(L);
  const auto owner = ResolveInventoryOwner(adapter, argument.unit_token);
  if (!owner.has_value() || owner->player == nullptr) {

    lua_pushnumber(L, 1.0);
    return 1;
  }

  if (owner->is_active_player && argument.slot == -1) {

    const auto ammo_item_id = owner->player->GetUInt32(PLAYER_AMMO_ID);
    if (ammo_item_id == 0) {
      lua_pushnumber(L, 1.0);
      return 1;
    }
    lua_pushnumber(L, static_cast<lua_Number>(
                          openwow::game::CountCarriedItemsOfEntry(
                              adapter.inventory(), ammo_item_id)));
    return 1;
  }

  const auto* item = ResolveInventoryQueryItemObject(
      L, adapter, *owner, argument.slot);
  if (item == nullptr) {
    lua_pushnumber(L, 1.0);
    return 1;
  }

  if (const auto free_slots =
          ResolveSpecialBagFreeSlotCount(L, *item, argument.slot);
      free_slots.has_value()) {
    lua_pushnumber(L, static_cast<lua_Number>(*free_slots));
    return 1;
  }

  lua_pushnumber(L, static_cast<lua_Number>(item->GetStackCount()));
  return 1;
}

int LuaGetInventoryItemQuality(lua_State* L) {
  InventoryUnitSlotArgument argument;
  if (!ParseInventoryUnitSlotArgument(
          L, "GetInventoryItemQuality", &argument)) {
    return 0;
  }

  auto& adapter = RequireItemLuaAdapter(L);
  const auto owner = ResolveInventoryOwner(adapter, argument.unit_token);
  const auto item_entry =
      owner.has_value()
          ? ResolveInventoryItemEntry(L, adapter, *owner, argument.slot)
          : std::nullopt;
  const auto* item_template =
      item_entry.has_value()
          ? adapter.queries().GetOrRequestItemTemplate(*item_entry)
          : nullptr;
  if (item_template == nullptr) {
    lua_pushnil(L);
    return 1;
  }

  lua_pushnumber(
      L, item_template->inventory_type == openwow::game::InventoryType::NonEquip
             ? -1.0
             : static_cast<lua_Number>(
                   static_cast<std::uint32_t>(item_template->quality)));
  return 1;
}

int LuaGetInventoryItemCooldown(lua_State* L) {
  InventoryUnitSlotArgument argument;
  if (!ParseInventoryUnitSlotArgument(
          L, "GetInventoryItemCooldown", &argument)) {
    return 0;
  }

  auto& adapter = RequireItemLuaAdapter(L);
  const auto owner = ResolveInventoryOwner(adapter, argument.unit_token);
  if (!owner.has_value() || argument.slot == -1) {
    return PushUnavailableInventoryItemCooldown(L);
  }

  const auto* item = ResolveInventoryQueryItemObject(
      L, adapter, *owner, argument.slot);
  if (item == nullptr) {
    return PushUnavailableInventoryItemCooldown(L);
  }

  std::uint32_t duration_ms = 0;
  std::uint32_t start_time_ms = 0;
  bool enabled = false;
  if (!openwow::game::FillItemCooldownByInventoryItem(
          *item, &duration_ms, &start_time_ms, &enabled)) {
    return PushUnavailableInventoryItemCooldown(L);
  }

  return PushItemCooldownResult(
      L, ItemCooldownResult{
             .start = static_cast<double>(start_time_ms) / 1000.0,
             .duration = static_cast<double>(duration_ms) / 1000.0,
             .enabled = enabled ? 1.0 : 0.0,
         });
}

int LuaHasWandEquipped(lua_State* L) {
  const auto* ranged_item = GetLocalInventoryItemByAbsoluteSlot(
      L, openwow::game::InventorySlots::kRanged);
  if (ranged_item == nullptr) { lua_pushnil(L); return 1; }

  const auto* item_template =
      RequireItemLuaAdapter(L).queries().GetItemTemplate(
          ranged_item->GetEntry());
  if (item_template != nullptr && item_template->subclass == 19) {
    lua_pushnumber(L, 1);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaIsInventoryItemLocked(lua_State* L) {
  int slot = 0;
  if (!ResolveInventorySlotArgument(L, 1, &slot) || slot == -1) {
    lua_pushnil(L);
    return 1;
  }

  const auto* item = GetLocalInventoryItemInstanceByAbsoluteSlot(L, slot);
  auto* session = GetWorldSession(L);
  const auto* live_item =
      session != nullptr && item != nullptr
          ? session->objects().GetItem(
                ::openwow::game::ObjectGuid(item->guid))
          : nullptr;
  if (live_item != nullptr && live_item->IsLocked()) {
    lua_pushnumber(L, 1);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

}
