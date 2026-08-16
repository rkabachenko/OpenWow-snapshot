#include "openwow/game/inventory/loot/adapters/lua/loot_lua_api.h"
#include "openwow/game/inventory/loot/adapters/lua/loot_lua_adapter.h"
#include "openwow/ui/game/loot_tooltip_support.h"
#include "openwow/ui/script_boolean.h"

#include "openwow/runtime/time/game_clock.h"
#include "openwow/game/group_system.h"
#include "openwow/game/game_misc_utils.h"
#include "openwow/game/interaction_sender.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/items/item_icon_resolver.h"
#include "openwow/game/inventory/loot/loot_interaction.h"
#include "openwow/game/inventory/loot/loot_state.h"
#include "openwow/game/money_display.h"
#include "openwow/game/object_manager.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/foundation/text/ascii.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/lua_numeric.h"

extern "C" {
#include <lua.hpp>
}

#include <cctype>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace openwow::ui::game::detail {

namespace {

::openwow::game::LootState& RequireLootState(lua_State* L) {
  return RequireLootLuaAdapter(L).loot().state();
}

void PushWowBool(lua_State* state, const bool value) {
  if (value) {
    lua_pushnumber(state, 1.0);
  } else {
    lua_pushnil(state);
  }
}

}

static constexpr int kLootMethodFreeForAll = 0;
static constexpr int kLootMethodRoundRobin = 1;
static constexpr int kLootMethodMaster = 2;
static constexpr int kLootMethodGroup = 3;
static constexpr int kLootMethodNeedBeforeGreed = 4;

static constexpr int kRetailLootThresholdMinimum = 2;
static constexpr int kRetailLootThresholdMaximum = 6;

static const char* LootMethodToLuaString(const std::uint32_t method) {
  switch (method) {
    case kLootMethodFreeForAll:
      return "freeforall";
    case kLootMethodRoundRobin:
      return "roundrobin";
    case kLootMethodMaster:
      return "master";
    case kLootMethodGroup:
      return "group";
    case kLootMethodNeedBeforeGreed:
      return "needbeforegreed";
    default:
      return "ERROR!";
  }
}

static const ::openwow::game::LootWindow* GetActiveLoot(lua_State* L) {
  auto& loot = RequireLootLuaAdapter(L).loot();
  return loot.is_looting() ? &loot.loot_window() : nullptr;
}

static int TotalSlotCount(bool has_gold, std::size_t item_count) {
  return static_cast<int>(item_count) + (has_gold ? 1 : 0);
}

static int LootWindowDisplaySlotCount(const ::openwow::game::LootWindow& loot_window) {
  return static_cast<int>(
      ::openwow::game::LootInteraction::GetDisplaySlotCount(loot_window));
}

static int LootWindowDisplayIndexFromUiSlot(
    const ::openwow::game::LootWindow& loot_window,
    const int ui_slot) {
  return UISlotToItemIndex(loot_window.gold_slot_reserved, ui_slot,
                           LootWindowDisplaySlotCount(loot_window));
}

static const ::openwow::game::LootItem* FindLootWindowItemForUiSlot(
    const ::openwow::game::LootWindow& loot_window,
    const int ui_slot) {
  const int display_index = LootWindowDisplayIndexFromUiSlot(loot_window, ui_slot);
  if (display_index < 0) {
    return nullptr;
  }

  return ::openwow::game::LootInteraction::FindItemByDisplayIndex(
      loot_window, static_cast<std::size_t>(display_index));
}

struct LootSlotRequest {
  bool is_gold_slot = false;
  bool is_item_slot_in_range = false;
  std::uint32_t item_slot_index = 0;
};

static std::uint32_t ReadSaturatedLootU32Argument(lua_State* L,
                                                  const int argument_index,
                                                  const char* usage) {
  if (lua_isnumber(L, argument_index) == 0) {
    luaL_error(L, "%s", usage);
  }

  return ::openwow::ui::SaturateLuaNumberToU32(
      lua_tonumber(L, argument_index));
}

static std::uint32_t ReadDirectLootI32BitsArgument(lua_State* L,
                                                   const int argument_index,
                                                   const char* usage) {
  if (lua_isnumber(L, argument_index) == 0) {
    luaL_error(L, "%s", usage);
  }

  return static_cast<std::uint32_t>(
      TruncateLuaNumberToSseI32(lua_tonumber(L, argument_index)));
}

static std::uint32_t ReadLootSlotArgument(lua_State* L, const char* usage) {
  return ReadSaturatedLootU32Argument(L, 1, usage);
}

static std::uint64_t ReadLootRollIdArgument(lua_State* L,
                                            const int argument_index,
                                            const char* usage) {
  return ReadSaturatedLootU32Argument(L, argument_index, usage);
}

static LootSlotRequest ResolveLootSlotRequest(bool has_gold,
                                              std::uint32_t one_based_slot) {
  const std::uint32_t zero_based_slot = one_based_slot - 1u;
  if (has_gold && zero_based_slot == 0u) {
    return LootSlotRequest{.is_gold_slot = true};
  }

  const std::uint32_t item_slot_index =
      has_gold ? zero_based_slot - 1u : zero_based_slot;
  if (item_slot_index >= ::openwow::game::LootInteraction::kMaxLootSlots) {
    return {};
  }

  return LootSlotRequest{
      .is_item_slot_in_range = true,
      .item_slot_index = item_slot_index,
  };
}

static bool HasActiveLootWindowSource(
    const ::openwow::game::LootWindow* loot_window) {
  return loot_window != nullptr && !loot_window->source_guid.IsEmpty();
}

static int PushLootSlotInfoEmpty(lua_State* L, const int quality) {
  lua_pushnil(L);
  lua_pushliteral(L, "");
  lua_pushnumber(L, 0);
  lua_pushnumber(L, quality);
  lua_pushnil(L);
  return 5;
}

int LuaSetLootPortrait(lua_State* L) {
  const int texture_index = ValidateTextureWidgetArgument(L);
  auto& adapter = RequireLootLuaAdapter(L);
  auto& loot = RequireLootLuaAdapter(L).loot();
  if (!loot.is_looting()) {
    ClearPortraitState(L, texture_index);
    FrameScript_PushNil(L);
    return 1;
  }

  const ::openwow::game::ObjectGuid source_guid = loot.loot_window().source_guid;
  if (adapter.objects().GetUnit(source_guid) == nullptr) {
    ClearPortraitState(L, texture_index);
    FrameScript_PushNil(L);
    return 1;
  }

  BindPortraitGuid(L, texture_index, source_guid);
  FrameScript_PushNumber(L, 1.0);
  return 1;
}

static std::string GetLuaOrFallbackGlobalString(lua_State* L,
                                                const char* key,
                                                const char* fallback) {
  lua_getglobal(L, key);
  std::string value;
  if (lua_isstring(L, -1) != 0) {
    value = lua_tostring(L, -1);
  }
  lua_pop(L, 1);

  if (!value.empty()) {
    return value;
  }

  return RequireLootLuaAdapter(L).Localize(key, fallback);
}

static std::string FormatLootMoneyPart(lua_State* L,
                                       const char* key,
                                       const char* fallback,
                                       const int value) {
  return RequireLootLuaAdapter(L).Format(
      GetLuaOrFallbackGlobalString(L, key, fallback), {std::to_string(value)});
}

static std::string BuildLootMoneyText(lua_State* L, const std::uint32_t copper) {
  const int gold = static_cast<int>(copper / 10000);
  const int silver = static_cast<int>((copper % 10000) / 100);
  const int copper_only = static_cast<int>(copper % 100);

  std::string result;
  const auto append_part = [&result](std::string part) {
    if (part.empty()) {
      return;
    }
    if (!result.empty()) {
      result += '\n';
    }
    result += part;
  };

  if (gold > 0) {
    append_part(FormatLootMoneyPart(L, "GOLD_AMOUNT", "%d Gold", gold));
  }
  if (silver > 0) {
    append_part(FormatLootMoneyPart(L, "SILVER_AMOUNT", "%d Silver", silver));
  }
  if (copper_only > 0 || result.empty()) {
    append_part(
        FormatLootMoneyPart(L, "COPPER_AMOUNT", "%d Copper", copper_only));
  }

  return result;
}

static void HandleLootMoneyPickup(lua_State* L) {
  auto& adapter = RequireLootLuaAdapter(L);
  adapter.interaction().SendLootMoney();

  auto& loot = RequireLootLuaAdapter(L).loot();
  if (loot.is_looting()) {
    const auto result = loot.HandleLootClearMoney();
    if (!result.cleared_gold) {
      return;
    }

    adapter.Present(LootLuaEvent::kSlotCleared, 1);
    if (result.should_release_and_close) {
      adapter.CloseActiveLoot(false);
    }
    return;
  }
}

static std::string ResolveItemIcon(const ::openwow::game::ItemDefinitions& item_definitions,
                                   std::uint32_t item_id,
                                   const openwow::data::dbc::DbcLoader* dbc) {
  const auto* tmpl = item_definitions.GetItem(item_id);
  if (tmpl && tmpl->display_id > 0 && dbc) {
    return ::openwow::game::ResolveItemInventoryIconTexturePath(
        dbc, tmpl->display_id);
  }
  return ::openwow::game::BuildItemInventoryIconTexturePath(
      ::openwow::game::kFallbackItemInventoryIconName);
}

static bool HasLootMethodAuthority(LootLuaAdapter& adapter) {
  auto& group_system = adapter.group();
  if (group_system.GetTrackedPartyMemberCount() == 0 &&
      group_system.GetRealRaidMemberCount() == 0) {
    adapter.ShowSystemMessage(80);
    return false;
  }

  const auto active_player_guid =
      adapter.objects().GetActivePlayerGuid().GetRawValue();
  if (group_system.GetLeaderGuid() != active_player_guid) {
    adapter.ShowSystemMessage(84);
    return false;
  }

  return true;
}

static bool TryParseLootMethod(std::string_view method_name,
                               std::uint32_t* out_method) {
  if (!out_method) {
    return false;
  }

  if (::openwow::text::EqualsIgnoreCaseAscii(method_name, "freeforall")) {
    *out_method = kLootMethodFreeForAll;
    return true;
  }
  if (::openwow::text::EqualsIgnoreCaseAscii(method_name, "roundrobin")) {
    *out_method = kLootMethodRoundRobin;
    return true;
  }
  if (::openwow::text::EqualsIgnoreCaseAscii(method_name, "master")) {
    *out_method = kLootMethodMaster;
    return true;
  }
  if (::openwow::text::EqualsIgnoreCaseAscii(method_name, "group")) {
    *out_method = kLootMethodGroup;
    return true;
  }
  if (::openwow::text::EqualsIgnoreCaseAscii(method_name, "needbeforegreed")) {
    *out_method = kLootMethodNeedBeforeGreed;
    return true;
  }

  return false;
}

static bool TryResolveMasterLootGuid(lua_State* L,
                                     LootLuaAdapter& adapter,
                                     std::uint64_t* out_guid) {
  if (!out_guid) {
    return false;
  }
  *out_guid = 0;

  if (!lua_isstring(L, 2)) {
    RequireLootLuaAdapter(L).ShowSystemMessage(252);
    return false;
  }

  const char* token_or_name = lua_tostring(L, 2);
  if (!token_or_name || token_or_name[0] == '\0') {
    RequireLootLuaAdapter(L).ShowSystemMessage(252);
    return false;
  }

  auto& group_system = adapter.group();
  const bool exact_match = ReadClientBoolArgOrDefault(L, 3, false);
  const auto master_guid =
      adapter.ResolveMasterLooter(L, token_or_name, exact_match);
  const auto raw_guid = master_guid.value_or(0);
  const auto active_player_guid =
      adapter.objects().GetLocalPlayerGuid().GetRawValue();

  if (raw_guid == 0 ||
      (raw_guid != active_player_guid &&
       !group_system.IsPartyUnitGuid(adapter.objects(), raw_guid) &&
       !group_system.IsRaidUnitGuid(adapter.objects(), raw_guid))) {
    RequireLootLuaAdapter(L).ShowSystemMessage(81, token_or_name);
    return false;
  }

  *out_guid = raw_guid;
  return true;
}

int LuaGetNumLootItems(lua_State* L) {
  const auto* lw = GetActiveLoot(L);
  if (!lw) {
    lua_pushnumber(L, 0);
    return 1;
  }
  lua_pushnumber(L, static_cast<lua_Integer>(
      TotalSlotCount(lw->gold_slot_reserved, LootWindowDisplaySlotCount(*lw))));
  return 1;
}

int LuaGetLootSlotInfo(lua_State* L) {
  const auto ui_slot = ReadLootSlotArgument(L, "Usage: GetLootSlotInfo(slot)");
  const auto* dbc = RequireLootLuaAdapter(L).dbc();

  const auto* lw = GetActiveLoot(L);
  if (!lw) {
    return PushLootSlotInfoEmpty(L, 0);
  }

  const auto request = ResolveLootSlotRequest(lw->gold_slot_reserved, ui_slot);

  if (request.is_gold_slot) {
    if (lw->gold == 0) {
      return PushLootSlotInfoEmpty(L, 0);
    }
    const auto gold_texture =
        ::openwow::game::MoneyDisplay::GetCoinIconPath(
            static_cast<std::int32_t>(lw->gold));
    const auto gold_name = BuildLootMoneyText(L, lw->gold);
    lua_pushstring(L, gold_texture.c_str());
    lua_pushstring(L, gold_name.c_str());
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnil(L);
    return 5;
  }
  if (!request.is_item_slot_in_range) {
    return PushLootSlotInfoEmpty(L, 0);
  }

  const auto* item = ::openwow::game::LootInteraction::FindItemByDisplayIndex(
      *lw, request.item_slot_index);
  if (!item) {
    return PushLootSlotInfoEmpty(L, -1);
  }
  const int quality =
      ResolveCachedLootItemQuality(RequireLootLuaAdapter(L).item_definitions(), item->item_id);

  bool locked = item->slot_type == ::openwow::game::LootSlotType::kLocked;
  lua_pushstring(
      L, ResolveLootSlotIconTexturePath(item->display_info_id, dbc).c_str());
  lua_pushstring(
      L,
      ResolveLootSlotDisplayName(
             RequireLootLuaAdapter(L).item_definitions(),
             dbc,
             item->item_id,
             static_cast<std::int32_t>(item->random_property_id))
          .c_str());
  lua_pushnumber(L, static_cast<lua_Integer>(item->count));
  lua_pushnumber(L, static_cast<lua_Integer>(quality));
  PushWowBool(L, locked);
  return 5;
}

int LuaGetLootSlotLink(lua_State* L) {
  const auto ui_slot = ReadLootSlotArgument(L, "Usage: GetLootSlotLink(slot)");

  const auto* lw = GetActiveLoot(L);
  if (!lw) {
    lua_pushnil(L);
    return 1;
  }
  const auto* item =
      FindLootWindowItemForUiSlot(*lw, static_cast<int>(ui_slot));
  if (!item) {
    lua_pushnil(L);
    return 1;
  }
  auto& adapter = RequireLootLuaAdapter(L);
  const auto link = TryBuildCachedLootItemLink(
      adapter.item_definitions(), adapter.objects(), adapter.dbc(),
      item->item_id,
      static_cast<std::int32_t>(item->random_property_id),
      item->random_suffix);
  if (!link.has_value()) {
    lua_pushnil(L);
    return 1;
  }
  lua_pushstring(L, link->c_str());
  return 1;
}

int LuaLootSlot(lua_State* L) {
  const auto one_based_slot =
      ReadLootSlotArgument(L, "Usage: LootSlot(slot)");
  if (one_based_slot >
      static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
    return 0;
  }
  const auto ui_slot = static_cast<int>(one_based_slot);

  auto& adapter = RequireLootLuaAdapter(L);

  const auto* lw = GetActiveLoot(L);
  if (!lw) return 0;
  const bool has_gold = lw->gold_slot_reserved;
  const auto item_count =
      static_cast<std::size_t>(LootWindowDisplaySlotCount(*lw));

  int total = TotalSlotCount(has_gold, item_count);
  if (ui_slot < 1 || ui_slot > total) return 0;

  int idx = UISlotToItemIndex(has_gold, ui_slot, item_count);

  if (idx == -1) {
    HandleLootMoneyPickup(L);
    return 0;
  }

  if (idx < 0) return 0;

  if (lw) {
    const auto* item = FindLootWindowItemForUiSlot(*lw, ui_slot);
    if (item) {
      switch (item->slot_type) {
      case ::openwow::game::LootSlotType::kRollOngoing:
        RequireLootLuaAdapter(L).ShowSystemMessage(443);
        return 0;
      case ::openwow::game::LootSlotType::kLocked:
        RequireLootLuaAdapter(L).ShowSystemMessage(594);
        return 0;
      case ::openwow::game::LootSlotType::kMaster:
        adapter.Present(LootLuaEvent::kOpenMasterList);
        return 0;
      default:
        break;
      }

      const auto* item_template =
          RequireLootLuaAdapter(L).item_definitions().GetItem(item->item_id);
      if (item_template != nullptr &&
          ::openwow::game::LootItemRequiresBindConfirm(*item_template, item->slot_type) &&
          adapter.CanPromptBind(*item_template, item->count)) {
        RequireLootState(L).SetPendingConfirmSlot(ui_slot);
        adapter.Present(LootLuaEvent::kBindConfirm, ui_slot);
        return 0;
      }
    }
  }

  const auto* item = FindLootWindowItemForUiSlot(*lw, ui_slot);
  if (!item) {
    return 0;
  }
  adapter.interaction().SendAutoStoreLootItem(item->slot_index);
  return 0;
}

int LuaConfirmLootSlot(lua_State* L) {
  const auto one_based_slot =
      ReadLootSlotArgument(L, "Usage: LootSlot(slot)");
  if (one_based_slot >
      static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
    return 0;
  }
  const auto ui_slot = static_cast<int>(one_based_slot);

  auto& adapter = RequireLootLuaAdapter(L);
  auto& ls = RequireLootState(L);

  if (ls.GetPendingConfirmSlot() != ui_slot) return 0;

  const auto* lw = GetActiveLoot(L);
  if (!lw) return 0;
  const bool has_gold = lw->gold_slot_reserved;
  const auto item_count =
      static_cast<std::size_t>(LootWindowDisplaySlotCount(*lw));

  int idx = UISlotToItemIndex(has_gold, ui_slot, item_count);
  if (idx < 0) return 0;

  if (const auto* item = FindLootWindowItemForUiSlot(*lw, ui_slot)) {
    adapter.interaction().SendAutoStoreLootItem(item->slot_index);
    ls.ClearPendingConfirmSlot();
  }
  return 0;
}

int LuaLootSlotIsItem(lua_State* L) {
  const auto ui_slot = ReadLootSlotArgument(L, "Usage: LootSlotIsItem(slot)");

  const auto* lw = GetActiveLoot(L);
  if (!HasActiveLootWindowSource(lw)) {
    lua_pushnil(L);
    return 1;
  }

  const auto request = ResolveLootSlotRequest(lw->gold_slot_reserved, ui_slot);
  if (!request.is_item_slot_in_range) {
    lua_pushnil(L);
    return 1;
  }

  const auto* item = ::openwow::game::LootInteraction::FindItemByDisplayIndex(
      *lw, request.item_slot_index);
  PushWowBool(L, item != nullptr && item->item_id > 0);
  return 1;
}

int LuaLootSlotIsCoin(lua_State* L) {
  const auto ui_slot = ReadLootSlotArgument(L, "Usage: LootSlotIsCoin(slot)");

  const auto* lw = GetActiveLoot(L);
  PushWowBool(
      L, HasActiveLootWindowSource(lw) &&
             lw->gold > 0 &&
             ResolveLootSlotRequest(lw->gold_slot_reserved, ui_slot).is_gold_slot);
  return 1;
}

int LuaCloseLoot(lua_State* L) {
  RequireLootLuaAdapter(L).CloseActiveLoot(
      openwow::ui::ScriptReadBoolArgOrDefault(L, 1, false));
  return 0;
}

int LuaIsFishingLoot(lua_State* L) {
  PushWowBool(
      L,
      RequireLootLuaAdapter(L).loot().cached_loot_type() ==
              ::openwow::game::LootType::kFishing);
  return 1;
}

int LuaGetLootMethod(lua_State* L) {
  auto& adapter = RequireLootLuaAdapter(L);
  auto& gs = adapter.group();
  const auto method = gs.GetLootMethod();
  lua_pushstring(L, LootMethodToLuaString(method));

  const auto active_player_guid =
      adapter.objects().GetLocalPlayerGuid().GetRawValue();
  const auto indices = gs.ResolveLootMethodMasterIndices(active_player_guid);

  if (indices.party_index >= 0)
    lua_pushnumber(L, static_cast<lua_Number>(indices.party_index));
  else
    lua_pushnil(L);
  if (indices.raid_index >= 0)
    lua_pushnumber(L, static_cast<lua_Number>(indices.raid_index));
  else
    lua_pushnil(L);
  return 3;
}

int LuaSetLootMethod(lua_State* L) {
  auto& adapter = RequireLootLuaAdapter(L);

  if (!HasLootMethodAuthority(adapter)) {
    return 0;
  }

  if (!lua_isstring(L, 1)) {
    return luaL_error(L, "Usage: SetLootMethod(\"method\" [,master])");
  }

  const char* method_name = lua_tostring(L, 1);
  std::uint32_t method = 0;
  if (!method_name || !TryParseLootMethod(method_name, &method)) {
    return luaL_error(L, "Invalid loot method");
  }

  std::uint64_t master_guid = 0;
  if (method == kLootMethodMaster) {
    if (!TryResolveMasterLootGuid(L, adapter, &master_guid)) {
      return 0;
    }
  }

  if (adapter.objects().GetLocalPlayer() != nullptr) {
    adapter.interaction().SendLootMethod(
        method, master_guid, adapter.group().GetLootThreshold());
  }
  return 0;
}

int LuaGetLootThreshold(lua_State* L) {
  auto& gs = RequireLootLuaAdapter(L).group();
  lua_pushnumber(L, static_cast<lua_Number>(gs.GetLootThreshold()));
  return 1;
}

int LuaSetLootThreshold(lua_State* L) {
  auto& adapter = RequireLootLuaAdapter(L);

  if (!HasLootMethodAuthority(adapter)) {
    return 0;
  }

  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: SetLooThreshold(threshold)");
  }

  const int threshold = TruncateLuaNumberToSseI32(lua_tonumber(L, 1));
  if (threshold < kRetailLootThresholdMinimum ||
      threshold > kRetailLootThresholdMaximum) {
    return luaL_error(
        L, "SetLootThreshold(): threshold must be between %d and %d",
        kRetailLootThresholdMinimum, kRetailLootThresholdMaximum);
  }

  auto& gs = adapter.group();
  if (adapter.objects().GetLocalPlayer() != nullptr) {
    adapter.interaction().SendLootMethod(
        gs.GetLootMethod(), gs.GetMasterLooter(),
        static_cast<std::uint32_t>(threshold));
  }
  return 0;
}

int LuaGetMasterLootCandidate(lua_State* L) {
  const auto candidate_index = ReadSaturatedLootU32Argument(
                                   L, 1, "Usage: GetMasterLootCandidate(index)") -
                               1u;

  auto& ls = RequireLootState(L);
  const std::uint64_t guid = ls.GetMasterLootCandidateGuid(candidate_index);
  if (guid == 0) {
    lua_pushnil(L);
    return 1;
  }

  auto& adapter = RequireLootLuaAdapter(L);
  std::string name;
  if (const auto* cached_name = adapter.query_cache().GetPlayerName(guid)) {
    name = cached_name->name;
  }

  if (!name.empty()) {
    lua_pushstring(L, name.c_str());
    return 1;
  }

  adapter.RequestCandidateName(guid);

  lua_pushnil(L);
  return 1;
}

int LuaGiveMasterLoot(lua_State* L) {
  constexpr auto kUsage = "Usage: GiveMasterLoot(slot, index)";
  const auto one_based_slot = ReadSaturatedLootU32Argument(L, 1, kUsage);
  const auto candidate_index =
      ReadSaturatedLootU32Argument(L, 2, kUsage) - 1u;

  auto& adapter = RequireLootLuaAdapter(L);

  auto& ls = RequireLootState(L);
  const std::uint64_t target_guid =
      ls.GetMasterLootCandidateGuid(candidate_index);
  if (target_guid == 0) {
    return 0;
  }

  if (const auto* loot_window = GetActiveLoot(L); loot_window != nullptr) {
    if (one_based_slot >
        static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
      return 0;
    }
    const auto* item = FindLootWindowItemForUiSlot(
        *loot_window, static_cast<int>(one_based_slot));
    if (item != nullptr &&
        item->slot_type == ::openwow::game::LootSlotType::kMaster) {
      adapter.interaction().SendLootMasterGive(
          loot_window->source_guid.GetRawValue(), item->slot_index, target_guid);
    }
    return 0;
  }
  return 0;
}

int LuaGetOptOutOfLoot(lua_State* L) {
  auto& ls = RequireLootState(L);
  PushWowBool(L, ls.GetOptOut());
  return 1;
}

int LuaSetOptOutOfLoot(lua_State* L) {
  const bool opt_out = lua_gettop(L) >= 1 && lua_toboolean(L, 1) != 0;
  RequireLootLuaAdapter(L).ApplyOptOut(opt_out);
  return 0;
}

int LuaGetLootRollItemInfo(lua_State* L) {
  const auto roll_id =
      ReadLootRollIdArgument(L, 1, "Usage: GetLootRollItemInfo(id)");

  auto& ls = RequireLootState(L);
  const auto roll = ls.GetPendingRoll(roll_id);
  if (!roll.has_value()) {
    return 0;
  }

  const auto* tmpl =
      RequireLootLuaAdapter(L).item_definitions().GetItem(roll->item_id);
  if (!tmpl) {
    return 0;
  }

  const auto* dbc = RequireLootLuaAdapter(L).dbc();

  lua_pushstring(
      L, ResolveItemIcon(RequireLootLuaAdapter(L).item_definitions(), roll->item_id, dbc).c_str());

  const auto display_name = ResolveLootItemDisplayName(
      dbc, tmpl->name, roll->random_property_id);
  lua_pushstring(L, display_name.c_str());

  lua_pushnumber(L, static_cast<double>(roll->item_count));

  lua_pushnumber(L, static_cast<double>(
      static_cast<std::int32_t>(static_cast<std::uint32_t>(tmpl->quality))));

  PushWowBool(L, tmpl->bonding == 1);

  PushWowBool(L, (roll->roll_vote_mask & 0x02) != 0);
  PushWowBool(L, (roll->roll_vote_mask & 0x04) != 0);
  PushWowBool(L, (roll->roll_vote_mask & 0x08) != 0);

  lua_pushnumber(L, static_cast<double>(roll->reason_need));
  lua_pushnumber(L, static_cast<double>(roll->reason_greed));
  lua_pushnumber(L, static_cast<double>(roll->reason_disenchant));

  lua_pushnumber(L, static_cast<double>(
      static_cast<std::int32_t>(tmpl->required_disenchant_skill)));

  return 12;
}

int LuaGetLootRollItemLink(lua_State* L) {
  const auto roll_id =
      ReadLootRollIdArgument(L, 1, "Usage: GetLootRollItemLink(id)");

  auto& ls = RequireLootState(L);
  const auto roll = ls.GetPendingRoll(roll_id);
  if (!roll.has_value()) {
    return 0;
  }

  auto& adapter = RequireLootLuaAdapter(L);
  const auto link = TryBuildCachedLootItemLink(
      adapter.item_definitions(), adapter.objects(), adapter.dbc(),
      roll->item_id,
      static_cast<std::int32_t>(roll->random_property_id),
      roll->random_suffix);
  if (!link.has_value()) {
    return 0;
  }

  lua_pushstring(L, link->c_str());
  return 1;
}

int LuaGetLootRollTimeLeft(lua_State* L) {
  const auto roll_id =
      ReadLootRollIdArgument(L, 1, "Usage: GetLootRollTimeLeft(id)");

  auto& ls = RequireLootState(L);
  const std::uint32_t remaining = ls.GetPendingRollTimeLeft(
      roll_id, ::openwow::core::GameClock::GetTickCount32());

  lua_pushnumber(L, static_cast<lua_Number>(remaining));
  return 1;
}

namespace {

int SubmitLootRoll(lua_State* L, const bool is_confirmed) {
  const char* usage = is_confirmed
                          ? "Usage: ConfirmLootRoll(id, rollType)"
                          : "Usage: RollOnLoot(id, rollType)";
  if (lua_isnumber(L, 1) == 0 || lua_isnumber(L, 2) == 0) {
    return luaL_error(L, "%s", usage);
  }

  const auto roll_id = is_confirmed
                           ? ReadSaturatedLootU32Argument(L, 1, usage)
                           : ReadDirectLootI32BitsArgument(L, 1, usage);
  const auto roll_type = ReadDirectLootI32BitsArgument(L, 2, usage);

  auto& adapter = RequireLootLuaAdapter(L);

  const auto submission =
      RequireLootState(L).SubmitPendingRoll(
          roll_id, roll_type, is_confirmed);
  if (!submission.has_value()) {
    return 0;
  }

  switch (submission->prompt) {
    case ::openwow::game::PendingRollPrompt::kConfirmLoot:
      adapter.Present(LootLuaEvent::kConfirmRoll,
                      static_cast<int>(submission->roll_id),
                      static_cast<int>(submission->roll_type));
      return 0;
    case ::openwow::game::PendingRollPrompt::kConfirmDisenchant:
      adapter.Present(
          LootLuaEvent::kConfirmDisenchant,
          static_cast<int>(submission->roll_id),
          static_cast<int>(submission->roll_type));
      return 0;
    case ::openwow::game::PendingRollPrompt::kNone:
      break;
  }

  adapter.interaction().SendLootRoll(
      submission->loot_guid, submission->loot_slot, submission->roll_type);
  if (submission->fire_cancel_event) {
    adapter.Present(
        LootLuaEvent::kCancelRoll,
        static_cast<int>(submission->roll_id));
  }
  for (const auto& start_event : submission->start_events) {
    adapter.Present(
        LootLuaEvent::kStartRoll, static_cast<int>(start_event.roll_id),
        static_cast<int>(start_event.countdown_ms));
  }
  return 0;
}

}

int LuaRollOnLoot(lua_State* L) {
  return SubmitLootRoll(L, false);
}

int LuaConfirmLootRoll(lua_State* L) {
  return SubmitLootRoll(L, true);
}

int LuaConfirmBindOnUse(lua_State* L) {
  (void)RequireLootLuaAdapter(L).interaction().ConfirmPendingBindOnUse();
  return 0;
}

}
