#include "openwow/game/inventory/items/adapters/lua/item_lua_api.h"
#include "openwow/game/inventory/items/adapters/lua/item_lua_adapter.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/game/inventory/search/action_item_inventory_search.h"
#include "openwow/game/chat_display.h"
#include "openwow/game/cooldown_tracker.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/items/adapters/retail/item_display_name_formatter.h"
#include "openwow/game/inventory/equipment/item_equip_check.h"
#include "openwow/game/inventory/equipment/equipped_item_type_matcher.h"
#include "openwow/game/localization.h"
#include "openwow/game/objects/cgitem.h"
#include "openwow/game/objects/unit/unit_cast_runtime.h"
#include "openwow/game/object_guid.h"
#include "openwow/game/readable_text.h"
#include "openwow/game/reputation_info.h"
#include "openwow/game/script_event_helpers.h"
#include "openwow/game/spell_cast_execution.h"
#include "openwow/game/spell_runtime_values.h"
#include "openwow/game/spell_target_resolver.h"
#include "openwow/game/spell_c_internals.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/lua_numeric.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <cstring>
#include <optional>

namespace openwow::ui::game::detail {

namespace {

constexpr std::uint32_t kSpellEffectCreateItem = 24;
constexpr std::uint32_t kSpellEffectLearnSpell = 36;
constexpr std::uint8_t kSocketGemCount = 3;

std::uint32_t FindFirstLearnSpellId(const openwow::game::ItemTemplate& item) {
  for (const auto& spell_info : item.spells) {
    if (spell_info.spell_id != 0 && spell_info.trigger == 6) {
      return spell_info.spell_id;
    }
  }

  return 0;
}

std::uint32_t ResolveCreatedItemId(
    const openwow::data::dbc::SpellEntry& spell) {
  for (std::size_t effect_index = 0; effect_index < spell.effect.size();
       ++effect_index) {
    if (spell.effect[effect_index] == kSpellEffectCreateItem &&
        spell.effect_item_type[effect_index] != 0) {
      return spell.effect_item_type[effect_index];
    }
  }

  return 0;
}

std::uint32_t ResolveDressableRedirectItemId(
    const openwow::data::dbc::SpellEntry& spell,
    const std::uint32_t fallback_learn_spell_id,
    const openwow::data::dbc::DbcLoader& dbc) {
  for (std::size_t effect_index = 0; effect_index < spell.effect.size();
       ++effect_index) {
    if (spell.effect[effect_index] != kSpellEffectLearnSpell) {
      continue;
    }

    auto learned_spell_id = spell.effect_trigger_spell[effect_index];
    if (learned_spell_id == 0) {
      learned_spell_id = fallback_learn_spell_id;
    }
    if (learned_spell_id == 0) {
      continue;
    }

    const auto* learned_spell = dbc.spell().LookupEntry(learned_spell_id);
    if (learned_spell == nullptr) {
      continue;
    }

    if (const auto created_item_id = ResolveCreatedItemId(*learned_spell);
        created_item_id != 0) {
      return created_item_id;
    }
  }

  return 0;
}

int PushSocketGemItemIds(lua_State* L,
                         const ::openwow::game::ItemInstance& item) {
  for (std::uint8_t socket_index = 0; socket_index < kSocketGemCount;
       ++socket_index) {
    const auto gem_item_id =
        ResolveSpellItemEnchantmentGemId(L, item.GetSocketEnchant(socket_index));
    if (gem_item_id != 0) {
      lua_pushnumber(L, static_cast<lua_Number>(gem_item_id));
      continue;
    }

    lua_pushnil(L);
  }

  return kSocketGemCount;
}

int PushSocketGemItemIds(lua_State* L,
                         const ::openwow::game::CGItem_C& item) {
  constexpr auto first_socket_enchant_slot =
      static_cast<std::uint8_t>(::openwow::game::EnchantmentSlot::Socket1);

  for (std::uint8_t socket_index = 0; socket_index < kSocketGemCount;
       ++socket_index) {
    const auto gem_item_id = ResolveSpellItemEnchantmentGemId(
        L, item.GetEnchantId(first_socket_enchant_slot + socket_index));
    if (gem_item_id != 0) {
      lua_pushnumber(L, static_cast<lua_Number>(gem_item_id));
      continue;
    }

    lua_pushnil(L);
  }

  return kSocketGemCount;
}

void PushNilSocketGemItemIds(lua_State* L) {
  for (std::uint8_t socket_index = 0; socket_index < kSocketGemCount;
       ++socket_index) {
    lua_pushnil(L);
  }
}

const ::openwow::game::ItemInstance* ResolveContainerGemQueryItem(
    lua_State* L, const int bag_id, const int zero_based_slot) {
  if (zero_based_slot < 0) {
    return nullptr;
  }

  auto& adapter = RequireItemLuaAdapter(L);
  if (adapter.objects().GetActivePlayer() == nullptr) {
    return nullptr;
  }

  auto& inventory = adapter.inventory();
  const auto slot = static_cast<std::uint8_t>(zero_based_slot);
  const bool bank_frame_open = adapter.world_session().bank_npc_guid() != 0;

  switch (bag_id) {
    case -2:
      return inventory.GetKeyringSlot(slot);
    case -1:
      return inventory.GetBankSlot(slot);
    case 0:
      return inventory.GetBackpackSlot(slot);
    default:
      break;
  }

  if (bag_id >= 1 &&
      bag_id <= static_cast<int>(::openwow::game::PlayerInventoryReplica::kMaxBags)) {
    return inventory.GetBagSlot(static_cast<std::uint8_t>(bag_id), slot);
  }

  if (bag_id >= 5 && bag_id <= 11) {
    if (!bank_frame_open) {
      return nullptr;
    }

    return inventory.GetBankBagSlot(static_cast<std::uint8_t>(bag_id - 5),
                                    slot);
  }

  return nullptr;
}

}

int LuaGetItemSpell(lua_State *L) {
  const auto view = ResolveItemSpellView(L, 1);
  if (!view) {
    lua_pushnil(L);
    lua_pushnil(L);
    return 2;
  }

  lua_pushstring(L, std::string(view.spell->spell_name).c_str());
  lua_pushstring(L, std::string(view.spell->rank).c_str());
  return 2;
}

int LuaIsUsableItem(lua_State *L) {
  const auto item_id = ResolveItemIdArg(L, 1);
  auto& adapter = RequireItemLuaAdapter(L);
  auto* player = adapter.objects().GetLocalPlayerTyped();
  if (item_id == 0 || player == nullptr ||
      adapter.inventory()
              .CountDefaultPlayerItemsOfEntry(item_id) == 0) {
    lua_pushnil(L);
    lua_pushnil(L);
    return 2;
  }

  const auto* item_template = adapter.queries().GetItemTemplate(item_id);
  if (item_template == nullptr) {
    lua_pushnil(L);
    lua_pushnil(L);
    return 2;
  }

  const auto* dbc = adapter.dbc();
  const auto* item_spell = FindItemOnUseSpell(*item_template);
  if (item_spell != nullptr) {
    const auto* cooldown_spell =
        dbc != nullptr ? dbc->spell().LookupEntry(item_spell->spell_id) : nullptr;
    const auto category_id =
        cooldown_spell != nullptr ? cooldown_spell->category : 0u;
    if (!adapter.cooldowns().IsItemUseReady(
            item_spell->spell_id, item_id, category_id,
            adapter.MonotonicMilliseconds())) {
      lua_pushnil(L);
      lua_pushnil(L);
      return 2;
    }
  }

  if (adapter.CurrentMapIsArena() &&
      !openwow::game::ItemPassesScriptArenaUseRestrictions(
          adapter.inventory(), item_id, *item_template, dbc)) {
    lua_pushnil(L);
    lua_pushnil(L);
    return 2;
  }

  const auto spell_id =
      openwow::game::ResolveItemUseSpellIdWithEquippedFallback(
          adapter.inventory(), item_id, item_template, dbc);
  const auto* spell =
      dbc != nullptr ? dbc->spell().LookupEntry(spell_id) : nullptr;

  if (spell == nullptr) {
    lua_pushnumber(L, 1.0);
    lua_pushnil(L);
    return 2;
  }

  std::uint64_t target_guid = 0;
  if (!openwow::game::ValidateSpellRequirements(
          adapter.world_session(),
          reinterpret_cast<std::uintptr_t>(player),
          reinterpret_cast<std::uintptr_t>(spell),
          reinterpret_cast<std::uintptr_t>(&target_guid),
          reinterpret_cast<std::uintptr_t>(item_template), false)) {
    lua_pushnil(L);
    lua_pushnil(L);
    return 2;
  }

  if (!adapter.HasSpellPower(*spell, *player)) {
    lua_pushnil(L);
    lua_pushnumber(L, 1.0);
    return 2;
  }

  lua_pushnumber(L, 1.0);
  lua_pushnil(L);
  return 2;
}

int LuaIsEquippableItem(lua_State *L) {
  const auto item_id = ResolveItemIdArg(L, 1);
  if (item_id == 0) {
    lua_pushnil(L);
    return 1;
  }
  const auto *item =
      RequireItemLuaAdapter(L).queries().GetItemTemplate(item_id);
  if (item != nullptr &&
      item->inventory_type != ::openwow::game::InventoryType::NonEquip) {
    lua_pushwowbool(L, true);
    return 1;
  }
  lua_pushnil(L);
  return 1;
}

int LuaIsConsumableItem(lua_State *L) {
  const auto item_id = ResolveItemIdArg(L, 1);
  if (item_id == 0) {
    lua_pushnil(L);
    return 1;
  }

  const auto *item =
      RequireItemLuaAdapter(L).queries().GetItemTemplate(item_id);
  if (!item) {
    lua_pushnil(L);
    return 1;
  }

  if (item->inventory_type == ::openwow::game::InventoryType::Ammo ||
      item->inventory_type == ::openwow::game::InventoryType::Thrown) {
    lua_pushnumber(L, 1.0);
    return 1;
  }

  for (const auto &spell_info : item->spells) {
    if (spell_info.spell_id == 0 || spell_info.trigger != 0) {
      continue;
    }

    const auto *spell = LookupSpellEntry(L, spell_info.spell_id);
    if (!spell) {
      continue;
    }

    if (spell_info.charges < 0 || SpellHasConsumableRequirement(*spell)) {
      lua_pushnumber(L, 1.0);
      return 1;
    }
  }

  lua_pushnil(L);
  return 1;
}

int LuaIsDressableItem(lua_State *L) {
  const auto item_id = ResolveItemIdArg(L, 1);
  if (item_id == 0) {
    lua_pushnil(L);
    return 1;
  }

  auto& adapter = RequireItemLuaAdapter(L);
  const auto* item = adapter.queries().GetOrRequestItemTemplate(item_id);
  if (item == nullptr) {
    lua_pushnil(L);
    return 1;
  }

  if (item->inventory_type != ::openwow::game::InventoryType::NonEquip) {
    lua_pushnumber(L, 1.0);
    return 1;
  }

  const auto* dbc = adapter.dbc();
  if (dbc == nullptr) {
    lua_pushnil(L);
    return 1;
  }

  const auto fallback_learn_spell_id = FindFirstLearnSpellId(*item);
  for (const auto& item_spell : item->spells) {
    if (item_spell.spell_id == 0) {
      continue;
    }

    const auto* spell = dbc->spell().LookupEntry(item_spell.spell_id);
    if (spell == nullptr) {
      continue;
    }

    const auto redirected_item_id =
        ResolveDressableRedirectItemId(*spell, fallback_learn_spell_id, *dbc);
    if (redirected_item_id == 0) {
      continue;
    }

    const auto* redirected_item =
        adapter.queries().GetOrRequestItemTemplate(redirected_item_id);
    if (redirected_item != nullptr &&
        redirected_item->inventory_type !=
            ::openwow::game::InventoryType::NonEquip) {
      lua_pushnumber(L, 1.0);
      return 1;
    }
  }

  lua_pushnil(L);
  return 1;
}

int LuaGetItemFamily(lua_State *L) {

  const auto item_id = ResolveItemIdArg(L, 1);
  if (item_id == 0) {
    return 0;
  }

  const auto *item =
      RequireItemLuaAdapter(L).queries().GetItemTemplate(item_id);
  if (item == nullptr) {
    return 0;
  }

  lua_pushnumber(L, static_cast<lua_Number>(item->bag_family));
  return 1;
}

int LuaGetItemStats(lua_State *L) {
  if (!lua_isstring(L, 1) || (lua_type(L, 2) > 0 && lua_type(L, 2) != LUA_TTABLE)) {
    return luaL_error(L, "Usage: GetItemStats(itemLink[, statTable])");
  }

  openwow::game::ItemStatTable stat_values{};
  auto& adapter = RequireItemLuaAdapter(L);
  const auto* player = adapter.objects().GetActivePlayer();
  if (player == nullptr) {
    return 0;
  }
  if (!openwow::game::BuildItemStatTableFromItemLink(
          lua_tostring(L, 1), adapter.queries(), adapter.dbc(),
          *player, stat_values)) {
    return 0;
  }

  if (lua_type(L, 2) == LUA_TTABLE) {
    lua_settop(L, 2);
  } else {
    lua_createtable(L, 0, 0);
  }

  return openwow::game::PushItemStatFields(stat_values, L);
}

int LuaIsHarmfulItem(lua_State *L) {
  const auto view = ResolveItemSpellView(L, 1);
  if (view && ::openwow::game::GetHelpfulHarmfulDisposition(*view.spell) ==
                  ::openwow::game::SpellHelpfulHarmfulDisposition::kHarmful) {
    lua_pushnumber(L, 1.0);
    return 1;
  }

  lua_pushnil(L);
  return 1;
}

int LuaIsHelpfulItem(lua_State *L) {
  const auto view = ResolveItemSpellView(L, 1);
  if (view && ::openwow::game::GetHelpfulHarmfulDisposition(*view.spell) ==
                  ::openwow::game::SpellHelpfulHarmfulDisposition::kHelpful) {
    lua_pushnumber(L, 1.0);
    return 1;
  }

  lua_pushnil(L);
  return 1;
}

int LuaCancelItemTempEnchantment(lua_State *L) {
  auto& adapter = RequireItemLuaAdapter(L);
  if (adapter.objects().GetActivePlayer() == nullptr)
    return 0;

  if (!lua_isnumber(L, 1))
    return luaL_error(L, "Usage: CancelItemTempEnchantment(slot)");

  const auto requested_slot =
      openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1));
  const auto raw = requested_slot - 1u;
  if (raw > 1u)
    return luaL_error(L, "Usage: CancelItemTempEnchantment(slot)");

  constexpr std::array<std::uint8_t, 2> kTemporaryEnchantSlots{
      openwow::game::InventorySlots::kMainHand,
      openwow::game::InventorySlots::kOffHand,
  };
  const auto abs_slot = kTemporaryEnchantSlots[raw];

  const auto *item =
      adapter.inventory().GetEquipSlot(
          static_cast<std::uint8_t>(abs_slot));
  if (!item)
    return 0;

  if (item->IsQuestItem() || item->GetTemporaryEnchant() == 0)
    return 0;

  adapter.interaction().SendCancelTempEnchantment(abs_slot);
  return 0;
}

int LuaCancelPendingEquip(lua_State *L) {
  if (lua_isnumber(L, 1) == 0) {
    return luaL_error(L, "Usage: CancelPendingEquip(index)");
  }

  auto* session = GetWorldSession(L);
  if (session == nullptr || session->objects().GetActivePlayer() == nullptr) {
    return 0;
  }

  const auto pending_index =
      openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1));
  RequireInventoryCommands(L).ResolvePending(pending_index, false);
  return 0;
}

int LuaGetContainerItemGems(lua_State *L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: GetContainerItemGems(bag, slot)");
  }

  const int bag = static_cast<int>(lua_tonumber(L, 1));
  const int zero_based_slot = static_cast<int>(lua_tonumber(L, 2)) - 1;
  const auto* item = ResolveContainerGemQueryItem(L, bag, zero_based_slot);
  if (item != nullptr && !item->IsEmpty()) {
    return PushSocketGemItemIds(L, *item);
  }

  PushNilSocketGemItemIds(L);
  return kSocketGemCount;
}

int LuaGetInventoryItemGems(lua_State *L) {
  if (lua_isnumber(L, 1) == 0) {
    return luaL_error(L, "Usage: GetInventoryItemGems(slot)");
  }

  int slot = 0;
  if (!ResolveInventorySlotArgument(L, 1, &slot)) {
    return luaL_error(L, "Usage: GetInventoryItemGems(slot)");
  }
  if (slot == -1) {
    return 0;
  }

  const auto* item = GetLocalInventoryItemByAbsoluteSlot(L, slot);
  if (item == nullptr || item->GetEntry() == 0) {
    return 0;
  }

  return PushSocketGemItemIds(L, *item);
}

int LuaGetInventoryItemsForSlot(lua_State *L) {
  using namespace openwow::game;

  if (!(lua_isnumber(L, 1) || lua_isstring(L, 1))) {
    return luaL_error(L, "Usage: GetInventoryItemsForSlot(slot [, returnTable])");
  }

  std::uint32_t target_slot = 0;
  if (lua_isnumber(L, 1)) {
    target_slot = openwow::ui::SaturateLuaNumberToU32(
        lua_tonumber(L, 1) - 1.0);
    if (target_slot >= 19u) {
      return 0;
    }
  } else {
    const char *slot_name = lua_tostring(L, 1);
    const auto *slot_info = FindLuaInventorySlotInfo(
        L, slot_name != nullptr ? slot_name : "");
    if (slot_info == nullptr || slot_info->slot_number == 0u) {
      return luaL_error(L, "Usage: GetInventoryItemsForSlot(slot [, returnTable])");
    }
    target_slot = slot_info->slot_number;
  }

  if (lua_gettop(L) >= 2 && lua_istable(L, 2)) {
    lua_pushvalue(L, 2);
  } else {
    lua_newtable(L);
  }

  auto& adapter = RequireItemLuaAdapter(L);
  const auto *player = adapter.objects().GetLocalPlayerTyped();
  if (player == nullptr) {
    return 1;
  }

  auto &inventory = adapter.inventory();
  auto &query_cache = adapter.queries();
  auto &rep_info = adapter.reputation();
  const auto* dbc = adapter.dbc();

  auto check_item = [&](std::uint32_t entry) -> bool {
    if (entry == 0) {
      return false;
    }
    const auto *tmpl = query_cache.GetOrRequestItemTemplate(entry);
    if (tmpl == nullptr) {
      return false;
    }
    if (!CanItemOccupyInventorySlot(
            *tmpl, static_cast<std::int32_t>(target_slot), player, dbc)) {
      return false;
    }
    const std::int32_t faction_standing =
        (tmpl->required_reputation_faction != 0)
            ? rep_info.GetCurrentStanding(
                  static_cast<std::int32_t>(
                      tmpl->required_reputation_faction))
            : 0;
    return PlayerMeetsItemEquipRequirements(
        *tmpl, target_slot,
        ItemEquipRequirementContext{
            .player_level = player->State().GetLevel(),
            .player_class = player->State().GetClass(),
            .player_race = player->State().GetRace(),
            .proficiency_mask = adapter.ProficiencyMask(
                static_cast<std::uint8_t>(tmpl->item_class)),
            .faction_standing = faction_standing,
            .can_dual_wield = player->Casts().CanEquipWeaponInOffHand(),
            .dbc = dbc,
        });
  };

  auto push_result = [&](std::uint32_t location_key, std::uint32_t entry) {
    lua_pushnumber(L, static_cast<double>(location_key));
    lua_pushnumber(L, static_cast<double>(entry));
    lua_rawset(L, -3);
  };

  for (std::uint8_t slot = 0; slot <= 18; ++slot) {
    const auto *item = inventory.GetEquipSlot(slot);
    if (item == nullptr || item->IsEmpty()) {
      continue;
    }
    if (check_item(item->entry)) {
      push_result((slot + 1) | 0x100000, item->entry);
    }
  }

  for (std::uint8_t slot = 0; slot < PlayerInventoryReplica::kBackpackSize; ++slot) {
    const auto *item = inventory.GetBackpackSlot(slot);
    if (item == nullptr || item->IsEmpty()) {
      continue;
    }
    if (check_item(item->entry)) {
      push_result(static_cast<std::uint32_t>(slot + 1) | 0x300000, item->entry);
    }
  }

  for (std::uint8_t bag_idx = 1; bag_idx <= PlayerInventoryReplica::kMaxBags; ++bag_idx) {
    const auto *bag = inventory.GetBag(bag_idx);
    if (bag == nullptr || bag->IsEmpty()) {
      continue;
    }
    const std::uint32_t bag_base = (bag_idx) * 256;
    for (std::uint8_t sub = 0; sub < bag->num_slots; ++sub) {
      if (sub >= bag->slots.size()) {
        break;
      }
      const auto &item = bag->slots[sub];
      if (item.IsEmpty()) {
        continue;
      }
      if (check_item(item.entry)) {
        push_result((sub + 1 + bag_base) | 0x300000, item.entry);
      }
    }
  }

  const bool bank_open = adapter.world_session().bank_npc_guid() != 0;
  if (bank_open) {
    for (std::uint8_t slot = 0; slot < PlayerInventoryReplica::kBankSlots; ++slot) {
      const auto *item = inventory.GetBankSlot(slot);
      if (item == nullptr || item->IsEmpty()) {
        continue;
      }
      if (check_item(item->entry)) {
        const std::uint32_t abs_slot = slot + 39;
        push_result(abs_slot + 0x400001u, item->entry);
      }
    }

    for (std::uint8_t bag_idx = 0; bag_idx < PlayerInventoryReplica::kMaxBankBags;
         ++bag_idx) {
      const auto *bag = inventory.GetBankBag(bag_idx);
      if (bag == nullptr || bag->IsEmpty()) {
        continue;
      }
      const std::uint32_t bag_num = bag_idx + 1;
      const std::uint32_t bag_base = bag_num * 256;
      for (std::uint8_t sub = 0; sub < bag->num_slots; ++sub) {
        if (sub >= bag->slots.size()) {
          break;
        }
        const auto &item = bag->slots[sub];
        if (item.IsEmpty()) {
          continue;
        }
        if (check_item(item.entry)) {
          push_result((sub + 1 + bag_base) | 0x600000, item.entry);
        }
      }
    }
  }

  return 1;
}

int LuaGetItemStatDelta(lua_State *L) {
  if (!lua_isstring(L, 1) || !lua_isstring(L, 2) ||
      (lua_type(L, 3) > 0 && lua_type(L, 3) != LUA_TTABLE)) {
    return luaL_error(L, "Usage: GetItemStatSummary(itemLink1, itemLink2[, statTable])");
  }

  openwow::game::ItemStatTable left_stats{};
  openwow::game::ItemStatTable right_stats{};
  auto& adapter = RequireItemLuaAdapter(L);
  const auto* dbc = adapter.dbc();
  const auto* player = adapter.objects().GetActivePlayer();
  if (player == nullptr) {
    return 0;
  }
  if (!openwow::game::BuildItemStatTableFromItemLink(
          lua_tostring(L, 1), adapter.queries(), dbc, *player, left_stats) ||
      !openwow::game::BuildItemStatTableFromItemLink(
          lua_tostring(L, 2), adapter.queries(), dbc, *player, right_stats)) {
    return 0;
  }

  openwow::game::ItemStatTable delta{};
  openwow::game::BuildItemStatDelta(left_stats, right_stats, delta);

  if (lua_type(L, 3) == LUA_TTABLE) {
    lua_settop(L, 3);
  } else {
    lua_newtable(L);
  }

  return openwow::game::PushItemStatFields(delta, L);
}

}
