
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/api/game_lua_api_profession.h"
#include "openwow/core/storm_string.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/combat_rating.h"
#include "openwow/game/skill_info.h"
#include "openwow/game/skill_line_ability_lookup.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/lua_numeric.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace openwow::ui::game::detail {

namespace {

constexpr std::uint32_t kWeaponItemClassId = 2u;
constexpr std::array<std::uint8_t, 3> kWeaponEquipSlots = {15u, 16u, 17u};
constexpr std::array<std::uint8_t, 3> kWeaponSkillBonusRatings = {20u, 21u, 22u};

[[nodiscard]] const openwow::data::dbc::SkillRaceClassInfoEntry* FindSkillRaceClassInfo(
    const openwow::data::dbc::DbcLoader& dbc,
    const std::uint8_t race,
    const std::uint8_t player_class,
    const std::uint32_t skill_line) {
  return ::openwow::game::FindSkillRaceClassInfoBySkillId(
      dbc.skill_race_class_info().entries(), race, player_class, skill_line);
}

void PushLuaStringView(lua_State* L, const std::string_view value) {
  lua_pushlstring(L, value.data(), value.size());
}

[[nodiscard]] int TruncateFloatToInt(const float value) {
  return static_cast<int>(value);
}

struct WeaponSkillLookupCache {
  const openwow::data::dbc::DbcLoader* dbc = nullptr;
  std::vector<std::uint32_t> subclass_to_spell;
  std::uint32_t default_subclass = 0;
};

WeaponSkillLookupCache& GetWeaponSkillLookupCache() {
  static WeaponSkillLookupCache cache;
  return cache;
}

[[nodiscard]] bool IsSingleBitMask(const std::uint32_t mask) {
  return mask != 0 && (mask & (mask - 1u)) == 0;
}

[[nodiscard]] std::uint32_t FirstSetBitIndex(const std::uint32_t mask) {
  std::uint32_t bit_index = 0;
  while (((mask >> bit_index) & 1u) == 0u) {
    ++bit_index;
  }
  return bit_index;
}

void RefreshWeaponSkillLookupCache(const openwow::data::dbc::DbcLoader& dbc) {
  auto& cache = GetWeaponSkillLookupCache();
  if (cache.dbc == &dbc) {
    return;
  }

  cache = {};
  cache.dbc = &dbc;

  bool has_weapon_subclass = false;
  std::uint32_t max_weapon_subclass = 0;
  for (const auto& entry : dbc.item_sub_class().entries()) {
    if (entry.class_id != kWeaponItemClassId) {
      continue;
    }

    has_weapon_subclass = true;
    max_weapon_subclass = std::max(max_weapon_subclass, entry.subclass_id);
    if ((entry.flags & 0x4u) != 0) {
      cache.default_subclass = entry.subclass_id;
    }
  }

  if (!has_weapon_subclass) {
    return;
  }

  cache.subclass_to_spell.assign(max_weapon_subclass + 1u, 0u);
  const auto& spells = dbc.spell().entries();
  for (auto it = spells.rbegin(); it != spells.rend(); ++it) {
    const auto& spell = *it;
    if ((spell.attributes & 0x40u) == 0u || spell.equipped_item_class != 2) {
      continue;
    }

    const auto subclass_mask =
        static_cast<std::uint32_t>(spell.equipped_item_sub_class_mask);
    if (!IsSingleBitMask(subclass_mask)) {
      continue;
    }

    const auto subclass_id = FirstSetBitIndex(subclass_mask);
    if (subclass_id >= cache.subclass_to_spell.size()) {
      continue;
    }

    cache.subclass_to_spell[subclass_id] = spell.id;
  }
}

[[nodiscard]] std::uint32_t ResolveWeaponSkillLineByVirtualSlot(
    const openwow::game::CGPlayer_C& player,
    const openwow::data::dbc::DbcLoader& dbc,
    const std::uint8_t virtual_slot) {
  if (virtual_slot >= kWeaponEquipSlots.size()) {
    return 0;
  }

  RefreshWeaponSkillLookupCache(dbc);
  const auto& cache = GetWeaponSkillLookupCache();
  if (cache.subclass_to_spell.empty()) {
    return 0;
  }

  std::uint32_t subclass_id = cache.default_subclass;
  if (const auto item_metadata =
          player.GetVisibleItemTemplateMetadata(kWeaponEquipSlots[virtual_slot]);
      item_metadata.has_value()) {
    if (item_metadata->item_class != kWeaponItemClassId) {
      return 0;
    }
    subclass_id = item_metadata->subclass;
  }

  if (subclass_id >= cache.subclass_to_spell.size()) {
    return 0;
  }

  const auto spell_id = cache.subclass_to_spell[subclass_id];
  if (spell_id == 0) {
    return 0;
  }

  for (const auto& ability : dbc.skill_line_ability().entries()) {
    if (ability.spell_id != spell_id ||
        !::openwow::game::SkillLineAbilityMatchesRaceClass(
            ability, player.State().GetRace(), player.State().GetClass())) {
      continue;
    }

    if (FindSkillRaceClassInfo(dbc, player.State().GetRace(), player.State().GetClass(), ability.skill_id) ==
        nullptr) {
      continue;
    }

    return ability.skill_id;
  }

  return 0;
}

[[nodiscard]] int ResolveWeaponSkillDisplayBonus(
    const openwow::game::CGPlayer_C& player,
    const openwow::data::dbc::DbcLoader& dbc,
    const std::uint32_t skill_line_id) {
  for (std::size_t virtual_slot = 0; virtual_slot < kWeaponEquipSlots.size(); ++virtual_slot) {
    if (ResolveWeaponSkillLineByVirtualSlot(
            player, dbc, static_cast<std::uint8_t>(virtual_slot)) != skill_line_id) {
      continue;
    }

    auto bonus = TruncateFloatToInt(
        openwow::game::ComputeCombatRatingBonus(player, dbc, 0u));
    bonus += TruncateFloatToInt(
        openwow::game::ComputeCombatRatingBonus(
            player, dbc, kWeaponSkillBonusRatings[virtual_slot]));
    return bonus;
  }

  return 0;
}

const openwow::game::CGPlayer_C* GetBoundActivePlayer(lua_State* L) {
  const auto* session = GetWorldSession(L);
  if (session == nullptr) {
    return nullptr;
  }

  return session->objects().GetActivePlayer();
}

struct SkillInfoSyncSignature {
  std::uint64_t player_guid = 0;
  std::uint64_t skill_hash = 0;
  std::uint32_t level = 0;
  std::uint8_t race = 0;
  std::uint8_t player_class = 0;
  const openwow::data::dbc::DbcLoader* dbc = nullptr;

  [[nodiscard]] bool Matches(const SkillInfoSyncSignature& other) const {
    return player_guid == other.player_guid &&
           skill_hash == other.skill_hash &&
           level == other.level &&
           race == other.race &&
           player_class == other.player_class &&
           dbc == other.dbc;
  }
};

[[nodiscard]] std::uint64_t HashPlayerSkillState(
    const openwow::game::CGPlayer_C& player) {
  constexpr std::uint64_t kOffsetBasis = 1469598103934665603ull;
  constexpr std::uint64_t kPrime = 1099511628211ull;

  std::uint64_t hash = kOffsetBasis;
  const auto mix = [&hash](const std::uint64_t value) {
    hash ^= value;
    hash *= kPrime;
  };

  for (std::uint16_t skill_index = 0; skill_index < 128; ++skill_index) {
    const auto skill = player.GetSkill(skill_index);
    mix(skill.skill_id);
    mix(skill.step);
    mix(skill.value);
    mix(skill.max_value);
    mix(static_cast<std::uint16_t>(skill.modifier));
    mix(static_cast<std::uint16_t>(skill.step_modifier));
  }

  return hash;
}

void SynchronizeSkillInfoStore(lua_State* L) {
  static SkillInfoSyncSignature last_signature;

  auto& store = openwow::game::SkillInfoStore::Get();
  const auto* active_player = GetBoundActivePlayer(L);
  const auto* dbc = GetDbcLoader(L);
  if (active_player == nullptr || dbc == nullptr) {
    store.Reset();
    last_signature = {};
    return;
  }

  const SkillInfoSyncSignature current_signature{
      .player_guid = active_player->GetGuid().GetRawValue(),
      .skill_hash = HashPlayerSkillState(*active_player),
      .level = active_player->State().GetLevel(),
      .race = active_player->State().GetRace(),
      .player_class = active_player->State().GetClass(),
      .dbc = dbc,
  };
  if (store.GetNumSkillLines() == 0 || !current_signature.Matches(last_signature)) {
    store.UpdateFromPlayer(*active_player, *dbc);
    last_signature = current_signature;
  }
}

void PushEmptySkillLineInfo(lua_State* L) {
  lua_pushnil(L);
  lua_pushnil(L);
  lua_pushnil(L);
  lua_pushnumber(L, 0);
  lua_pushnumber(L, 0);
  lua_pushnumber(L, 0);
  lua_pushnumber(L, 0);
  lua_pushnil(L);
  lua_pushnil(L);
  lua_pushnil(L);
  lua_pushnumber(L, 0);
  lua_pushnumber(L, 0);
  lua_pushnil(L);
}

void PushHeaderSkillLineInfo(lua_State* L,
                             const std::string_view category_name,
                             const bool is_expanded) {
  PushLuaStringView(L, category_name);
  lua_pushnumber(L, 1);
  if (is_expanded) {
    lua_pushnumber(L, 1);
  } else {
    lua_pushnil(L);
  }
  lua_pushnumber(L, 0);
  lua_pushnumber(L, 0);
  lua_pushnumber(L, 0);
  lua_pushnumber(L, 0);
  lua_pushnil(L);
  lua_pushnil(L);
  lua_pushnil(L);
  lua_pushnumber(L, 0);
  lua_pushnumber(L, 0);
}

void FireSkillLinesChangedEvent() {
  ScriptEventDispatch::Get().FireEvent(events::SKILL_LINES_CHANGED);
}

}

int LuaGetNumSkillLines(lua_State* L) {
  SynchronizeSkillInfoStore(L);
  lua_pushnumber(
      L,
      static_cast<lua_Integer>(
          openwow::game::SkillInfoStore::Get().GetNumVisibleLines()));
  return 1;
}

int LuaGetSkillLineInfo(lua_State* L) {
  SynchronizeSkillInfoStore(L);
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetSkillLineInfo(index)");
  }

  const auto skill_index =
      openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1)) - 1u;
  const auto* player = GetBoundActivePlayer(L);
  const auto* dbc = GetDbcLoader(L);
  const auto& store = openwow::game::SkillInfoStore::Get();
  const auto* visible_entry =
      (skill_index < store.GetNumVisibleLines())
          ? store.GetSkillEntry(skill_index)
          : nullptr;
  if (player == nullptr || dbc == nullptr || visible_entry == nullptr) {
    PushEmptySkillLineInfo(L);
    return 13;
  }

  if (visible_entry->skill_id == 0) {
    if (const auto* category =
            dbc->skill_line_category().LookupEntry(visible_entry->category_id);
        category != nullptr) {
      PushHeaderSkillLineInfo(
          L,
          category->name,
          store.GetCategoryCollapseState(skill_index) == 0);
      return 12;
    }

    PushEmptySkillLineInfo(L);
    return 13;
  }

  const auto* skill_line = dbc->skill_line().LookupEntry(visible_entry->skill_id);
  const auto* race_class_info = FindSkillRaceClassInfo(
      *dbc, player->State().GetRace(), player->State().GetClass(), visible_entry->skill_id);
  const auto skill_slot =
      player->FindActiveSkillSlot(static_cast<std::uint16_t>(visible_entry->skill_id));
  if (skill_line == nullptr || race_class_info == nullptr || !skill_slot.has_value()) {
    PushEmptySkillLineInfo(L);
    return 13;
  }

  const auto skill = player->GetSkill(*skill_slot);
  auto displayed_rank = static_cast<std::uint32_t>(skill.value);
  if (displayed_rank != 0) {
    displayed_rank += static_cast<std::uint16_t>(skill.step_modifier);
  }
  displayed_rank += ResolveWeaponSkillDisplayBonus(*player, *dbc, visible_entry->skill_id);

  auto displayed_max_rank = static_cast<std::uint32_t>(skill.max_value);
  if (displayed_max_rank != 0) {
    displayed_max_rank += static_cast<std::uint16_t>(skill.step_modifier);
  }
  if ((race_class_info->flags & 0x400u) != 0) {
    displayed_max_rank = 1;
    displayed_rank = std::min(displayed_rank, 1u);
  }

  PushLuaStringView(L, skill_line->name);
  lua_pushnil(L);
  lua_pushnil(L);
  lua_pushnumber(L, static_cast<lua_Integer>(displayed_rank));

  const auto queued_points =
      store.GetQueuedPointsForSkill(visible_entry->skill_id);
  lua_pushnumber(L, static_cast<lua_Integer>(queued_points));
  lua_pushnumber(L, static_cast<lua_Integer>(skill.modifier));
  lua_pushnumber(L, static_cast<lua_Integer>(displayed_max_rank));

  if (skill.step != 0 && (race_class_info->flags & 0x20u) != 0) {
    lua_pushnumber(L, 1);
  } else {
    lua_pushnil(L);
  }

  const auto* tiers = dbc->skill_tiers().LookupEntry(race_class_info->skill_tier_id);
  const bool exposes_step_cost =
      (race_class_info->flags & 0x8u) != 0 ||
      (skill.step == 0 && (race_class_info->flags & 0x4u) != 0);
  if (tiers != nullptr &&
      exposes_step_cost &&
      skill.step < tiers->value.size() &&
      tiers->value[skill.step] != 0) {
    lua_pushnumber(L, static_cast<lua_Integer>(tiers->value[skill.step]));
  } else {
    lua_pushnil(L);
  }

  const auto rank_cost = openwow::game::ResolveSkillRankCost(
      *dbc,
      *skill_line,
      *race_class_info,
      displayed_rank + queued_points + 1u);
  if (rank_cost != 0) {
    lua_pushnumber(L, static_cast<lua_Integer>(rank_cost));
  } else {
    lua_pushnil(L);
  }

  lua_pushnumber(L, static_cast<lua_Integer>(race_class_info->min_level));
  lua_pushnumber(L,
                 static_cast<lua_Integer>(race_class_info->skill_cost_index + 1u));
  PushLuaStringView(L, skill_line->description);
  return 13;
}

int LuaExpandSkillHeader(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: ExpandSkillHeader(index)");
  }

  SynchronizeSkillInfoStore(L);

  const auto index_bits =
      static_cast<std::uint32_t>(
          openwow::ui::TruncateLuaNumberToI32(lua_tonumber(L, 1))) -
      1u;
  auto& store = openwow::game::SkillInfoStore::Get();
  bool rebuilt_display_list = false;
  if ((index_bits & 0x80000000u) != 0) {
    store.ExpandAllCategories();
    rebuilt_display_list = true;
  } else {
    rebuilt_display_list = store.ExpandCategory(index_bits);
  }

  if (rebuilt_display_list) {
    FireSkillLinesChangedEvent();
  }
  return 0;
}

int LuaCollapseSkillHeader(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: CollapseSkillHeader(index)");
  }

  SynchronizeSkillInfoStore(L);

  const auto index_bits =
      static_cast<std::uint32_t>(
          openwow::ui::TruncateLuaNumberToI32(lua_tonumber(L, 1))) -
      1u;
  auto& store = openwow::game::SkillInfoStore::Get();
  bool rebuilt_display_list = false;
  if ((index_bits & 0x80000000u) != 0) {
    store.CollapseAllCategories();
    rebuilt_display_list = true;
  } else {
    rebuilt_display_list = store.CollapseCategory(index_bits);
  }

  if (rebuilt_display_list) {
    FireSkillLinesChangedEvent();
  }
  return 0;
}

int LuaAbandonSkill(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: AbandonSkill(index)");
  }

  auto* session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  SynchronizeSkillInfoStore(L);

  const auto skill_index =
      openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1)) - 1u;
  const auto* skill_entry = openwow::game::SkillInfoStore::Get().GetSkillEntry(
      skill_index);
  if (skill_entry == nullptr) {
    return 0;
  }

  session->interaction().SendUnlearnSkill(skill_entry->skill_id);
  return 0;
}

int LuaAddSkillUp(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: AddSkillUp(index)");
  }

  SynchronizeSkillInfoStore(L);

  const auto skill_index =
      static_cast<std::uint32_t>(
          openwow::ui::TruncateLuaNumberToI32(lua_tonumber(L, 1))) -
      1u;
  const auto* player = GetBoundActivePlayer(L);
  if (player == nullptr) {
    return 0;
  }

  const auto* dbc = GetDbcLoader(L);
  if (dbc != nullptr &&
      openwow::game::SkillInfoStore::Get().AddQueuedPoint(
          skill_index, *player, *dbc)) {
    FireSkillLinesChangedEvent();
  }

  return 0;
}

int LuaRemoveSkillUp(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: RemoveSkillUp(index)");
  }

  SynchronizeSkillInfoStore(L);

  const auto skill_index =
      static_cast<std::uint32_t>(
          openwow::ui::TruncateLuaNumberToI32(lua_tonumber(L, 1))) -
      1u;
  const auto* player = GetBoundActivePlayer(L);
  const auto* dbc = GetDbcLoader(L);
  if (player != nullptr && dbc != nullptr &&
      openwow::game::SkillInfoStore::Get().RemoveQueuedPoint(
          skill_index, *player, *dbc)) {
    FireSkillLinesChangedEvent();
  }

  return 0;
}

int LuaBuySkillTier(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: BuySkillTier(index)");
  }

  SynchronizeSkillInfoStore(L);

  auto* session = GetWorldSession(L);
  const auto* player = GetBoundActivePlayer(L);
  const auto* dbc = GetDbcLoader(L);
  if (session == nullptr || player == nullptr || dbc == nullptr) {
    return 0;
  }

  const std::uint32_t skill_index =
      openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1)) - 1u;
  auto& store = openwow::game::SkillInfoStore::Get();

  if (skill_index >= store.GetNumSkillLines()) {
    return 0;
  }

  const auto* visible_entry = store.GetSkillEntry(skill_index);
  if (visible_entry == nullptr || visible_entry->skill_id == 0) {
    return 0;
  }

  const auto* skill_line = dbc->skill_line().LookupEntry(visible_entry->skill_id);
  const auto* race_class_info = FindSkillRaceClassInfo(
      *dbc, player->State().GetRace(), player->State().GetClass(), visible_entry->skill_id);
  const auto skill_slot = player->FindActiveSkillSlot(
      static_cast<std::uint16_t>(visible_entry->skill_id));
  if (skill_line == nullptr || race_class_info == nullptr ||
      !skill_slot.has_value()) {
    return 0;
  }

  const auto skill = player->GetSkill(*skill_slot);
  const bool can_buy_step =
      (race_class_info->flags & 0x8u) != 0 ||
      (skill.step == 0 && (race_class_info->flags & 0x4u) != 0);
  const auto* tiers =
      dbc->skill_tiers().LookupEntry(race_class_info->skill_tier_id);
  if (!can_buy_step || tiers == nullptr || skill.step >= tiers->value.size()) {
    return 0;
  }

  const auto step_cost = tiers->value[skill.step];
  if (step_cost == 0 || step_cost > player->GetProfessionPoints()) {
    return 0;
  }

  session->interaction().SendBuySkillStep(visible_entry->skill_id);
  return 0;
}

int LuaAcceptSkillUps(lua_State* L) {
  SynchronizeSkillInfoStore(L);

  auto* session = GetWorldSession(L);
  auto& store = openwow::game::SkillInfoStore::Get();
  if (session == nullptr || store.GetTotalPointsUsed() == 0) {
    return 0;
  }

  const auto queued_points = store.CollectQueuedPoints();

  session->interaction().SendBuySkillRanks(queued_points);
  store.ClearQueuedPoints();
  return 0;
}

int LuaCancelSkillUps(lua_State* ) {

  openwow::game::SkillInfoStore::Get().ClearQueuedPoints();
  FireSkillLinesChangedEvent();
  return 0;
}

int LuaGetAdjustedSkillPoints(lua_State* L) {
  SynchronizeSkillInfoStore(L);

  const auto* player = GetBoundActivePlayer(L);
  if (player == nullptr) {
    lua_pushnumber(L, 0);
    return 1;
  }

  const auto available_points = static_cast<int>(player->GetProfessionPoints());
  const auto adjusted_points =
      available_points - static_cast<int>(
                             openwow::game::SkillInfoStore::Get()
                                 .GetTotalPointsUsed());
  lua_pushnumber(L, static_cast<lua_Integer>(adjusted_points));
  return 1;
}

int GetSelectedProfessionSkillIndex() {
  const auto selected_index = openwow::game::SkillInfoStore::Get().GetSelectedSkillIndex();
  return selected_index >= 0 ? selected_index + 1 : 0;
}

void SetSelectedProfessionSkillIndex(const int lua_index) {
  auto& store = openwow::game::SkillInfoStore::Get();
  if (lua_index < 1) {
    store.SetSelectedSkillEntryIndex(store.GetNumSkillLines());
    return;
  }

  store.SetSelectedSkillEntryIndex(static_cast<std::uint32_t>(lua_index - 1));
}

void ResetProfessionSkillUiState() {
  openwow::game::SkillInfoStore::Get().Reset();
}

}
