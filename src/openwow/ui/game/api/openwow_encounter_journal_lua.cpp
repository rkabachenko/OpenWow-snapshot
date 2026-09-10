#include "openwow/ui/game/api/openwow_encounter_journal_lua.h"

#include "openwow/data/formats/dbc/dbc_enums.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_table_registry.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/game/inventory/items/item_icon_resolver.h"
#include "openwow/game/localization.h"
#include "openwow/game/spell_text_formatter.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/runtime/world_ui_runtime_context.h"

extern "C" {
#include <lua.hpp>
}

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace openwow::ui::game {

namespace {

constexpr std::string_view kApiTableName = "C_OpenWoWJournal";
constexpr std::uint32_t kSchemaVersion = 9;
constexpr std::uint32_t kRandomDungeonType = 6;
constexpr std::uint32_t kPlayableClassMask = 0x5FFu;
constexpr std::uint32_t kWeaponItemClass = 2u;
constexpr std::uint32_t kArmorItemClass = 4u;

// Build 12340 proficiency spells and SkillRaceClassInfo define which classes
// can equip each weapon and armor subclass. Journal class filtering needs this
// in addition to item_template.AllowableClass, which is commonly unrestricted.
constexpr std::array<std::uint32_t, 21> kWeaponSubclassClassMasks{{
    0x06Fu, 0x067u, 0x00Du, 0x00Du, 0x47Bu, 0x463u, 0x427u,
    0x1AFu, 0x027u, 0x000u, 0x5D5u, 0x000u, 0x000u, 0x44Du,
    0x5FFu, 0x5DDu, 0x00Du, 0x000u, 0x00Du, 0x190u, 0x5FFu,
}};
constexpr std::array<std::uint32_t, 11> kArmorSubclassClassMasks{{
    0x5FFu, 0x5FFu, 0x46Fu, 0x067u, 0x023u, 0x000u,
    0x043u, 0x002u, 0x400u, 0x040u, 0x020u,
}};

struct JournalSupplementSpell final {
  std::uint32_t spell_id{0};
  std::int32_t verified_build{0};
};

struct JournalEncounterSupplement final {
  std::uint32_t encounter_id{0};
  std::uint32_t creature_entry{0};
  std::uint32_t creature_display_id{0};
  std::array<JournalSupplementSpell, 8> spells{};
  std::uint32_t spell_count{0};
};

#include "openwow_encounter_journal_build12340.inc"

struct JournalEncounterLootCreatures final {
  std::uint32_t encounter_id{0};
  std::array<std::uint32_t, 4> creature_entries{};
};

struct JournalCreatureLootItem final {
  std::uint32_t creature_entry{0};
  std::uint32_t item_id{0};
};

struct JournalLootItem final {
  std::uint32_t item_id{0};
  std::uint32_t display_id{0};
  std::uint32_t quality{0};
  std::uint32_t inventory_type{0};
  std::int32_t allowable_class{-1};
  std::uint32_t item_level{0};
  std::uint32_t required_level{0};
  const char* name{nullptr};
};

#include "openwow_encounter_journal_loot_build12340.inc"

struct InstanceRecord final {
  const openwow::data::dbc::LfgDungeonsEntry* lfg{nullptr};
  const openwow::data::dbc::MapEntry* map{nullptr};
  bool is_raid{false};
};

struct AbilityRecord final {
  const JournalSupplementSpell* supplement{nullptr};
  const openwow::data::dbc::SpellEntry* spell{nullptr};
};

struct LootRecord final {
  const JournalLootItem* supplement{nullptr};
  const openwow::data::dbc::ItemEntry* item{nullptr};
};

std::int32_t ResolveLootUsableClassMask(const LootRecord& loot) {
  std::uint32_t usable = loot.supplement->allowable_class < 0
                             ? kPlayableClassMask
                             : static_cast<std::uint32_t>(
                                   loot.supplement->allowable_class) &
                                   kPlayableClassMask;
  const auto item_class = loot.item->class_id;
  const auto subclass = loot.item->subclass_id;
  if (item_class == kWeaponItemClass) {
    usable &= subclass < kWeaponSubclassClassMasks.size()
                  ? kWeaponSubclassClassMasks[subclass]
                  : 0u;
  } else if (item_class == kArmorItemClass) {
    usable &= subclass < kArmorSubclassClassMasks.size()
                  ? kArmorSubclassClassMasks[subclass]
                  : 0u;
  }
  return static_cast<std::int32_t>(usable);
}

const openwow::data::dbc::DbcLoader* GetDbc() {
  const auto* dbc = openwow::data::GetBoundDbcTableRegistryLoader();
  if (dbc == nullptr) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "Encounter journal query failed: build 12340 DBC registry is not bound");
  }
  return dbc;
}

bool IsSupportedMapType(const openwow::data::dbc::MapEntry& map,
                        const bool is_raid) {
  const auto expected =
      is_raid ? openwow::data::dbc::MapType::kRaid
              : openwow::data::dbc::MapType::kInstance;
  return map.map_type == static_cast<std::uint32_t>(expected);
}

bool IsBetterRepresentative(
    const openwow::data::dbc::LfgDungeonsEntry& candidate,
    const openwow::data::dbc::LfgDungeonsEntry& current,
    const bool is_raid) {
  const auto preferred_type = is_raid ? 2u : 1u;
  const auto candidate_key = std::tuple{
      candidate.type_id == preferred_type ? 0u : 1u,
      candidate.difficulty == 0 ? 0u : 1u,
      candidate.order_index,
      candidate.id,
  };
  const auto current_key = std::tuple{
      current.type_id == preferred_type ? 0u : 1u,
      current.difficulty == 0 ? 0u : 1u,
      current.order_index,
      current.id,
  };
  return candidate_key < current_key;
}

std::vector<InstanceRecord> BuildInstances(
    const openwow::data::dbc::DbcLoader& dbc,
    const bool is_raid) {
  std::unordered_set<std::uint32_t> maps_with_encounters;
  maps_with_encounters.reserve(dbc.dungeon_encounter().size());
  for (const auto& encounter : dbc.dungeon_encounter()) {
    maps_with_encounters.insert(encounter.map_id);
  }

  std::unordered_map<std::uint32_t, InstanceRecord> by_map;
  by_map.reserve(dbc.lfg_dungeons().size());
  for (const auto& lfg : dbc.lfg_dungeons()) {
    if (lfg.type_id == kRandomDungeonType ||
        !maps_with_encounters.contains(lfg.map_id)) {
      continue;
    }

    const auto* map = dbc.map().LookupEntry(lfg.map_id);
    if (map == nullptr || !IsSupportedMapType(*map, is_raid)) {
      continue;
    }

    auto [it, inserted] = by_map.try_emplace(
        lfg.map_id, InstanceRecord{.lfg = &lfg, .map = map, .is_raid = is_raid});
    if (!inserted && IsBetterRepresentative(lfg, *it->second.lfg, is_raid)) {
      it->second.lfg = &lfg;
    }
  }

  std::vector<InstanceRecord> instances;
  instances.reserve(by_map.size());
  for (const auto& [map_id, record] : by_map) {
    (void)map_id;
    instances.push_back(record);
  }
  std::sort(instances.begin(), instances.end(),
            [](const InstanceRecord& left, const InstanceRecord& right) {
              const auto left_key = std::tuple{
                  left.lfg->expansion_level,
                  left.lfg->order_index,
                  left.lfg->min_level,
                  left.map->id,
              };
              const auto right_key = std::tuple{
                  right.lfg->expansion_level,
                  right.lfg->order_index,
                  right.lfg->min_level,
                  right.map->id,
              };
              return left_key < right_key;
            });
  return instances;
}

std::vector<std::uint32_t> BuildDifficulties(
    const openwow::data::dbc::DbcLoader& dbc,
    const std::uint32_t map_id) {
  std::vector<std::uint32_t> difficulties;
  for (const auto& encounter : dbc.dungeon_encounter()) {
    if (encounter.map_id == map_id &&
        std::find(difficulties.begin(), difficulties.end(),
                  encounter.difficulty) == difficulties.end()) {
      difficulties.push_back(encounter.difficulty);
    }
  }
  std::sort(difficulties.begin(), difficulties.end());
  return difficulties;
}

std::optional<std::uint32_t> FindWorldMapAreaId(
    const openwow::data::dbc::DbcLoader& dbc,
    const std::uint32_t map_id) {
  const openwow::data::dbc::WorldMapAreaEntry* best = nullptr;
  for (const auto& area : dbc.world_map_area()) {
    if (area.map_id != map_id) {
      continue;
    }
    if (best == nullptr ||
        std::tuple{area.area_id == 0 ? 0u : 1u, area.id} <
            std::tuple{best->area_id == 0 ? 0u : 1u, best->id}) {
      best = &area;
    }
  }
  return best != nullptr ? std::optional<std::uint32_t>{best->id}
                         : std::nullopt;
}

std::vector<const openwow::data::dbc::DungeonEncounterEntry*> BuildEncounters(
    const openwow::data::dbc::DbcLoader& dbc,
    const std::uint32_t map_id,
    const std::uint32_t difficulty) {
  std::vector<const openwow::data::dbc::DungeonEncounterEntry*> encounters;
  for (const auto& encounter : dbc.dungeon_encounter()) {
    if (encounter.map_id == map_id && encounter.difficulty == difficulty) {
      encounters.push_back(&encounter);
    }
  }
  std::stable_sort(
      encounters.begin(), encounters.end(),
      [](const auto* left, const auto* right) {
        return left->order_index < right->order_index;
      });
  return encounters;
}

const JournalEncounterSupplement* FindSupplement(
    const std::uint32_t encounter_id) {
  const auto it = std::lower_bound(
      kJournalEncounterSupplements.begin(),
      kJournalEncounterSupplements.end(),
      encounter_id,
      [](const JournalEncounterSupplement& candidate, const std::uint32_t id) {
        return candidate.encounter_id < id;
      });
  return it != kJournalEncounterSupplements.end() &&
                 it->encounter_id == encounter_id
             ? &*it
             : nullptr;
}

void LogMissingSupplementSpellOnce(const std::uint32_t encounter_id,
                                   const std::uint32_t spell_id) {
  static std::unordered_set<std::uint64_t> logged;
  const auto key = (static_cast<std::uint64_t>(encounter_id) << 32u) | spell_id;
  if (!logged.insert(key).second) {
    return;
  }
  openwow::diagnostics::Log(
      openwow::diagnostics::LogLevel::kWarn,
      "Encounter journal skipped supplemental spell: encounter=" +
          std::to_string(encounter_id) + " spell=" + std::to_string(spell_id) +
          " reason=missing from active build 12340 Spell.dbc");
}

void LogInvalidCreatureDisplayOnce(const JournalEncounterSupplement& supplement) {
  static std::unordered_set<std::uint32_t> logged;
  if (!logged.insert(supplement.encounter_id).second) {
    return;
  }
  openwow::diagnostics::Log(
      openwow::diagnostics::LogLevel::kWarn,
      "Encounter journal skipped supplemental creature display: encounter=" +
          std::to_string(supplement.encounter_id) + " creature=" +
          std::to_string(supplement.creature_entry) + " display=" +
          std::to_string(supplement.creature_display_id) +
          " reason=missing from active build 12340 CreatureDisplayInfo.dbc");
}

std::vector<AbilityRecord> BuildAbilities(
    const openwow::data::dbc::DbcLoader& dbc,
    const std::uint32_t encounter_id) {
  std::vector<AbilityRecord> abilities;
  if (dbc.dungeon_encounter().LookupEntry(encounter_id) == nullptr) {
    return abilities;
  }
  const auto* supplement = FindSupplement(encounter_id);
  if (supplement == nullptr) {
    return abilities;
  }

  abilities.reserve(supplement->spell_count);
  for (std::uint32_t index = 0; index < supplement->spell_count; ++index) {
    const auto& candidate = supplement->spells[index];
    const auto* spell = dbc.spell().LookupEntry(candidate.spell_id);
    if (spell == nullptr) {
      LogMissingSupplementSpellOnce(encounter_id, candidate.spell_id);
      continue;
    }
    abilities.push_back({.supplement = &candidate, .spell = spell});
  }
  return abilities;
}

const JournalEncounterLootCreatures* FindLootCreatures(
    const std::uint32_t encounter_id) {
  const auto it = std::lower_bound(
      kJournalEncounterLootCreatures.begin(),
      kJournalEncounterLootCreatures.end(),
      encounter_id,
      [](const JournalEncounterLootCreatures& candidate, const std::uint32_t id) {
        return candidate.encounter_id < id;
      });
  return it != kJournalEncounterLootCreatures.end() &&
                 it->encounter_id == encounter_id
             ? &*it
             : nullptr;
}

const JournalLootItem* FindLootItem(const std::uint32_t item_id) {
  const auto it = std::lower_bound(
      kJournalLootItems.begin(),
      kJournalLootItems.end(),
      item_id,
      [](const JournalLootItem& candidate, const std::uint32_t id) {
        return candidate.item_id < id;
      });
  return it != kJournalLootItems.end() && it->item_id == item_id ? &*it
                                                                 : nullptr;
}

void LogInvalidLootItemOnce(const std::uint32_t encounter_id,
                            const std::uint32_t item_id,
                            const std::string_view reason) {
  static std::unordered_set<std::uint64_t> logged;
  const auto key = (static_cast<std::uint64_t>(encounter_id) << 32u) | item_id;
  if (!logged.insert(key).second) {
    return;
  }
  openwow::diagnostics::Log(
      openwow::diagnostics::LogLevel::kWarn,
      "Encounter journal skipped supplemental loot: encounter=" +
          std::to_string(encounter_id) + " item=" + std::to_string(item_id) +
          " reason=" + std::string(reason));
}

std::vector<LootRecord> BuildLoot(const openwow::data::dbc::DbcLoader& dbc,
                                  const std::uint32_t encounter_id,
                                  const std::uint32_t difficulty) {
  std::vector<LootRecord> loot;
  if (difficulty >= 4 ||
      dbc.dungeon_encounter().LookupEntry(encounter_id) == nullptr) {
    return loot;
  }
  const auto* creatures = FindLootCreatures(encounter_id);
  if (creatures == nullptr) {
    return loot;
  }
  const auto creature_entry = creatures->creature_entries[difficulty];
  const auto first = std::lower_bound(
      kJournalCreatureLootItems.begin(),
      kJournalCreatureLootItems.end(),
      creature_entry,
      [](const JournalCreatureLootItem& candidate, const std::uint32_t id) {
        return candidate.creature_entry < id;
      });
  for (auto it = first;
       it != kJournalCreatureLootItems.end() &&
       it->creature_entry == creature_entry;
       ++it) {
    const auto* supplement = FindLootItem(it->item_id);
    if (supplement == nullptr) {
      LogInvalidLootItemOnce(encounter_id, it->item_id,
                             "missing generated item metadata");
      continue;
    }
    const auto* item = dbc.item().LookupEntry(it->item_id);
    if (item == nullptr) {
      LogInvalidLootItemOnce(encounter_id, it->item_id,
                             "missing from active build 12340 Item.dbc");
      continue;
    }
    if (supplement->display_id != 0 &&
        supplement->display_id != item->display_info_id) {
      LogInvalidLootItemOnce(
          encounter_id, it->item_id,
          "supplemental display ID disagrees with active build 12340 Item.dbc");
      continue;
    }
    if (dbc.item_display_info().LookupEntry(item->display_info_id) == nullptr) {
      LogInvalidLootItemOnce(
          encounter_id, it->item_id,
          "active Item.dbc display is missing from ItemDisplayInfo.dbc");
      continue;
    }
    loot.push_back({.supplement = supplement, .item = item});
  }
  return loot;
}

void PushString(lua_State* state, const std::string_view value) {
  lua_pushlstring(state, value.data(), value.size());
}

void PushIconPath(lua_State* state,
                  const openwow::data::dbc::DbcLoader& dbc,
                  const std::uint32_t spell_icon_id) {
  const auto* icon = dbc.spell_icon().LookupEntry(spell_icon_id);
  if (icon == nullptr || icon->icon_path.empty()) {
    lua_pushnil(state);
    return;
  }
  PushString(state, icon->icon_path);
}

std::uint32_t CheckUnsigned(lua_State* state,
                           const int argument,
                           const char* label) {
  const auto value = luaL_checkinteger(state, argument);
  if (value < 0 ||
      static_cast<unsigned long long>(value) >
          std::numeric_limits<std::uint32_t>::max()) {
    luaL_error(state, "%s must be an unsigned 32-bit integer", label);
  }
  return static_cast<std::uint32_t>(value);
}

std::size_t CheckOneBasedIndex(lua_State* state, const int argument) {
  const auto index = luaL_checkinteger(state, argument);
  if (index <= 0) {
    luaL_error(state, "index must be one-based");
  }
  return static_cast<std::size_t>(index - 1);
}

int LuaGetSchemaVersion(lua_State* state) {
  lua_pushinteger(state, kSchemaVersion);
  return 1;
}

int LuaGetCurrentMapId(lua_State* state) {
  const auto* context =
      openwow::ui::game::runtime::WorldUiRuntimeContext::FromLua(state);
  const auto* session = context != nullptr ? context->world_session() : nullptr;
  if (session == nullptr) {
    lua_pushnil(state);
    return 1;
  }

  if (session->has_current_map()) {
    lua_pushinteger(state, session->current_map_id());
    return 1;
  }

  const auto world_state_map_id = session->world_states().map_id();
  if (world_state_map_id >= 0) {
    lua_pushinteger(state, world_state_map_id);
    return 1;
  }

  lua_pushinteger(state, session->objects().GetMapId());
  return 1;
}

int LuaGetNumInstances(lua_State* state) {
  const auto* dbc = GetDbc();
  if (dbc == nullptr) {
    lua_pushinteger(state, 0);
    return 1;
  }
  const auto instances = BuildInstances(*dbc, lua_toboolean(state, 1) != 0);
  lua_pushinteger(state, static_cast<lua_Integer>(instances.size()));
  return 1;
}

int LuaGetInstanceByIndex(lua_State* state) {
  const auto index = CheckOneBasedIndex(state, 1);
  const bool is_raid = lua_toboolean(state, 2) != 0;
  const auto* dbc = GetDbc();
  if (dbc == nullptr) {
    lua_pushnil(state);
    return 1;
  }

  const auto instances = BuildInstances(*dbc, is_raid);
  if (index >= instances.size()) {
    lua_pushnil(state);
    return 1;
  }

  const auto& instance = instances[index];
  lua_pushinteger(state, instance.map->id);
  PushString(state, !instance.lfg->name.empty() ? instance.lfg->name
                                                : instance.map->name);
  PushString(state, instance.lfg->description);
  PushString(state, instance.lfg->texture_filename);
  lua_pushinteger(state, instance.lfg->min_level);
  lua_pushinteger(state, instance.lfg->max_level);
  lua_pushinteger(state, instance.lfg->expansion_level);
  lua_pushboolean(state, instance.is_raid ? 1 : 0);
  lua_pushinteger(state, instance.lfg->id);
  const auto world_map_area_id = FindWorldMapAreaId(*dbc, instance.map->id);
  if (world_map_area_id.has_value()) {
    lua_pushinteger(state, *world_map_area_id);
  } else {
    lua_pushnil(state);
  }
  return 10;
}

int LuaGetNumDifficulties(lua_State* state) {
  const auto map_id = CheckUnsigned(state, 1, "mapID");
  const auto* dbc = GetDbc();
  if (dbc == nullptr) {
    lua_pushinteger(state, 0);
    return 1;
  }
  const auto difficulties = BuildDifficulties(*dbc, map_id);
  lua_pushinteger(state, static_cast<lua_Integer>(difficulties.size()));
  return 1;
}

int LuaGetDifficultyByIndex(lua_State* state) {
  const auto map_id = CheckUnsigned(state, 1, "mapID");
  const auto index = CheckOneBasedIndex(state, 2);
  const auto* dbc = GetDbc();
  if (dbc == nullptr) {
    lua_pushnil(state);
    return 1;
  }

  const auto difficulties = BuildDifficulties(*dbc, map_id);
  if (index >= difficulties.size()) {
    lua_pushnil(state);
    return 1;
  }

  const auto difficulty = difficulties[index];
  const auto* map_difficulty =
      openwow::data::DBClient_FindMapDifficulty(dbc, map_id, difficulty);
  lua_pushinteger(state, difficulty);
  if (map_difficulty == nullptr) {
    lua_pushnil(state);
    lua_pushnil(state);
    return 3;
  }
  lua_pushinteger(state, map_difficulty->max_players);
  const auto difficulty_key = std::string(map_difficulty->difficulty_string);
  PushString(state, openwow::game::Localization::Get().GetString(
                        difficulty_key, difficulty_key));
  return 3;
}

int LuaGetNumEncounters(lua_State* state) {
  const auto map_id = CheckUnsigned(state, 1, "mapID");
  const auto difficulty = CheckUnsigned(state, 2, "difficulty");
  const auto* dbc = GetDbc();
  if (dbc == nullptr) {
    lua_pushinteger(state, 0);
    return 1;
  }
  const auto encounters = BuildEncounters(*dbc, map_id, difficulty);
  lua_pushinteger(state, static_cast<lua_Integer>(encounters.size()));
  return 1;
}

int LuaGetEncounterByIndex(lua_State* state) {
  const auto map_id = CheckUnsigned(state, 1, "mapID");
  const auto difficulty = CheckUnsigned(state, 2, "difficulty");
  const auto index = CheckOneBasedIndex(state, 3);
  const auto* dbc = GetDbc();
  if (dbc == nullptr) {
    lua_pushnil(state);
    return 1;
  }

  const auto encounters = BuildEncounters(*dbc, map_id, difficulty);
  if (index >= encounters.size()) {
    lua_pushnil(state);
    return 1;
  }

  const auto& encounter = *encounters[index];
  lua_pushinteger(state, encounter.id);
  PushString(state, encounter.name);
  PushIconPath(state, *dbc, encounter.spell_icon_id);
  lua_pushinteger(state, encounter.order_index);
  const auto* supplement = FindSupplement(encounter.id);
  if (supplement != nullptr) {
    lua_pushinteger(state, supplement->creature_entry);
    if (supplement->creature_display_id != 0 &&
        dbc->creature_display_info().LookupEntry(
            supplement->creature_display_id) != nullptr) {
      lua_pushinteger(state, supplement->creature_display_id);
    } else {
      LogInvalidCreatureDisplayOnce(*supplement);
      lua_pushnil(state);
    }
  } else {
    lua_pushnil(state);
    lua_pushnil(state);
  }
  return 6;
}

int LuaGetNumAbilities(lua_State* state) {
  const auto encounter_id = CheckUnsigned(state, 1, "encounterID");
  const auto* dbc = GetDbc();
  if (dbc == nullptr) {
    lua_pushinteger(state, 0);
    return 1;
  }
  const auto abilities = BuildAbilities(*dbc, encounter_id);
  lua_pushinteger(state, static_cast<lua_Integer>(abilities.size()));
  return 1;
}

int LuaGetAbilityByIndex(lua_State* state) {
  const auto encounter_id = CheckUnsigned(state, 1, "encounterID");
  const auto index = CheckOneBasedIndex(state, 2);
  const auto* dbc = GetDbc();
  if (dbc == nullptr) {
    lua_pushnil(state);
    return 1;
  }

  const auto abilities = BuildAbilities(*dbc, encounter_id);
  if (index >= abilities.size()) {
    lua_pushnil(state);
    return 1;
  }

  const auto& ability = abilities[index];
  lua_pushinteger(state, ability.spell->id);
  PushString(state, ability.spell->spell_name);
  const auto description = openwow::game::ResolveSpellDescriptionForDisplay(
      ability.spell->id, ability.spell->description);
  PushString(state, description);
  const auto tooltip = openwow::game::ResolveSpellDescriptionForDisplay(
      ability.spell->id, ability.spell->tooltip);
  PushString(state, tooltip);
  PushIconPath(state, *dbc, ability.spell->spell_icon_id);
  lua_pushinteger(state, ability.supplement->verified_build);
  return 6;
}

int LuaGetNumLoot(lua_State* state) {
  const auto encounter_id = CheckUnsigned(state, 1, "encounterID");
  const auto difficulty = CheckUnsigned(state, 2, "difficulty");
  const auto* dbc = GetDbc();
  if (dbc == nullptr) {
    lua_pushinteger(state, 0);
    return 1;
  }
  const auto loot = BuildLoot(*dbc, encounter_id, difficulty);
  lua_pushinteger(state, static_cast<lua_Integer>(loot.size()));
  return 1;
}

int LuaGetLootByIndex(lua_State* state) {
  const auto encounter_id = CheckUnsigned(state, 1, "encounterID");
  const auto difficulty = CheckUnsigned(state, 2, "difficulty");
  const auto index = CheckOneBasedIndex(state, 3);
  const auto* dbc = GetDbc();
  if (dbc == nullptr) {
    lua_pushnil(state);
    return 1;
  }

  const auto loot = BuildLoot(*dbc, encounter_id, difficulty);
  if (index >= loot.size()) {
    lua_pushnil(state);
    return 1;
  }
  const auto& item = loot[index];
  lua_pushinteger(state, item.item->id);
  PushString(state, item.supplement->name != nullptr
                        ? std::string_view(item.supplement->name)
                        : std::string_view{});
  const auto icon = openwow::game::ResolveItemInventoryIconTexturePath(
      dbc, item.item->display_info_id);
  PushString(state, icon);
  lua_pushinteger(state, item.supplement->quality);
  lua_pushinteger(state, item.supplement->item_level);
  lua_pushinteger(state, item.supplement->required_level);
  lua_pushinteger(state, item.item->inventory_type);
  lua_pushinteger(state, ResolveLootUsableClassMask(item));
  std::string_view subclass_name;
  if (const auto* const subclass = dbc->item_sub_class().LookupEntry(
          openwow::data::dbc::ItemSubClassEntry::ComposeKey(
              item.item->class_id, item.item->subclass_id));
      subclass != nullptr) {
    subclass_name = subclass->display_name.empty() ? subclass->verbose_name
                                                    : subclass->display_name;
  }
  PushString(state, subclass_name);
  return 9;
}

int LuaGetSourceSummary(lua_State* state) {
  const auto* dbc = GetDbc();
  if (dbc == nullptr) {
    lua_pushnil(state);
    return 1;
  }

  const auto dungeons = BuildInstances(*dbc, false);
  const auto raids = BuildInstances(*dbc, true);
  lua_pushinteger(state, static_cast<lua_Integer>(dungeons.size()));
  lua_pushinteger(state, static_cast<lua_Integer>(raids.size()));
  lua_pushinteger(state,
                  static_cast<lua_Integer>(dbc->dungeon_encounter().size()));
  return 3;
}

int LuaExpandPresentationText(lua_State* state) {
  std::size_t source_size = 0;
  const char* source_data = luaL_checklstring(state, 1, &source_size);
  const std::string source(source_data, source_size);
  const std::size_t requested_size =
      std::max<std::size_t>(65536u, source.size() * 8u + 4096u);
  const std::size_t output_size = std::min<std::size_t>(requested_size, 1024u * 1024u);
  std::vector<char> output(output_size, '\0');
  const char* cursor = source.c_str();
  const bool resolved = openwow::game::SpellTextFormatter::ExpandTextVariables(
      nullptr, output.data(), static_cast<std::uint32_t>(output.size()),
      -1, 0, 1, 0, 0, &cursor, true);

  const std::string_view expanded(output.data());
  PushString(state, expanded.empty() && !source.empty()
                        ? std::string_view(source)
                        : expanded);
  lua_pushboolean(state, resolved ? 1 : 0);

  if (!resolved) {
    const auto map_id = static_cast<std::uint32_t>(luaL_optinteger(state, 2, 0));
    const auto encounter_id =
        static_cast<std::uint32_t>(luaL_optinteger(state, 3, 0));
    const auto section_id =
        static_cast<std::uint32_t>(luaL_optinteger(state, 4, 0));
    static std::unordered_set<std::uint64_t> logged;
    const auto key = (static_cast<std::uint64_t>(encounter_id) << 32u) |
                     static_cast<std::uint64_t>(section_id);
    if (logged.insert(key).second) {
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kWarn,
          "Encounter journal retained unresolved presentation text: map=" +
              std::to_string(map_id) + " encounter=" +
              std::to_string(encounter_id) + " section=" +
              std::to_string(section_id) +
              " reason=one or more donor spell variables are unavailable in active build 12340");
    }
  }
  return 2;
}

void SetFunction(lua_State* state,
                 const char* name,
                 const lua_CFunction function) {
  lua_pushcfunction(state, function);
  lua_setfield(state, -2, name);
}

void InstallEncounterJournal(lua_State* state, void*) {
  lua_newtable(state);
  SetFunction(state, "GetSchemaVersion", LuaGetSchemaVersion);
  SetFunction(state, "GetCurrentMapID", LuaGetCurrentMapId);
  SetFunction(state, "GetNumInstances", LuaGetNumInstances);
  SetFunction(state, "GetInstanceByIndex", LuaGetInstanceByIndex);
  SetFunction(state, "GetNumDifficulties", LuaGetNumDifficulties);
  SetFunction(state, "GetDifficultyByIndex", LuaGetDifficultyByIndex);
  SetFunction(state, "GetNumEncounters", LuaGetNumEncounters);
  SetFunction(state, "GetEncounterByIndex", LuaGetEncounterByIndex);
  SetFunction(state, "GetNumAbilities", LuaGetNumAbilities);
  SetFunction(state, "GetAbilityByIndex", LuaGetAbilityByIndex);
  SetFunction(state, "GetNumLoot", LuaGetNumLoot);
  SetFunction(state, "GetLootByIndex", LuaGetLootByIndex);
  SetFunction(state, "GetSourceSummary", LuaGetSourceSummary);
  SetFunction(state, "ExpandPresentationText", LuaExpandPresentationText);
  lua_setglobal(state, kApiTableName.data());
}

void UninstallEncounterJournal(lua_State* state, void*) {
  lua_pushnil(state);
  lua_setglobal(state, kApiTableName.data());
}

}

lua::NativeBindingCatalog EncounterJournalNativeBindingCatalog() {
  return {
      .owner = "openwow.extensions.encounter_journal",
      .scope = lua::BindingScope::kWorld,
      .install = InstallEncounterJournal,
      .uninstall = UninstallEncounterJournal,
  };
}

}
