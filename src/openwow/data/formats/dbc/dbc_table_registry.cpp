
#include "openwow/data/formats/dbc/dbc_table_registry.h"
#include "openwow/core/console.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/dbc_loader.h"
#include "openwow/data/startup_filesystem_state.h"
#include "openwow/world/environment/day_night.h"
#include "openwow/game/game_table.h"
#include "openwow/ui/game/cvar_system.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <vector>

namespace openwow::data {

static bool g_dbc_compress = true;

namespace {

constexpr std::uint32_t kArmorResistanceFlag = 0x1u;
constexpr std::uint32_t kRetailSpellSchoolCount = 7u;
constexpr std::uint32_t kWeaponItemClassId = 2;
constexpr std::uint32_t kActiveItemClassFlag = 0x1u;
constexpr std::uint32_t kDefaultItemSubClassFlag = 0x4u;
constexpr std::uint32_t kDwordShiftCountMask = 31u;
constexpr std::uint32_t kMaterialWeaponImpactParryTypeOneFlag = 0x1u;
constexpr std::uint32_t kMaterialHardArmorSoundFlag = 0x2u;
constexpr std::uint32_t kMaterialChainArmorSoundFlag = 0x4u;
constexpr std::uint32_t kMaterialChainArmorSoundShift = 2u;
constexpr std::uint32_t kMaterialHardArmorSoundCategory = 2u;
constexpr std::uint32_t kAreaLiquidOverrideIdCount = 20u;
constexpr std::uint32_t kLiquidCategoryMask = 0x3u;

constexpr std::uint32_t DwordBit(const std::uint32_t index) {

  return 1u << (index & kDwordShiftCountMask);
}

int FindArmorResistanceIndex(const dbc::DbcLoader &dbc) {
  const auto &entries = dbc.resistances().entries();
  const auto count =
      std::min(entries.size(),
               static_cast<std::size_t>(kRetailSpellSchoolCount));

  int armor_index = -1;
  for (std::size_t index = 0; index < count; ++index) {
    if ((entries[index].flags & kArmorResistanceFlag) != 0) {
      armor_index = static_cast<int>(index);
    }
  }
  return armor_index;
}

template <typename Entry, typename PrimaryKey, typename SecondaryKey,
          typename PrimaryProjection, typename SecondaryProjection>
const Entry *FindInFirstContiguousPrimaryRun(
    const std::vector<Entry> &entries, const PrimaryKey primary_key,
    const SecondaryKey secondary_key, PrimaryProjection primary_projection,
    SecondaryProjection secondary_projection, std::uint32_t *row_index = nullptr) {

  for (std::size_t first = 0; first < entries.size(); ++first) {
    if (primary_projection(entries[first]) != primary_key) {
      continue;
    }

    for (std::size_t index = first; index < entries.size(); ++index) {
      const auto &entry = entries[index];
      if (primary_projection(entry) != primary_key) {
        return nullptr;
      }
      if (secondary_projection(entry) == secondary_key) {
        if (row_index != nullptr) {
          *row_index = static_cast<std::uint32_t>(index);
        }
        return &entry;
      }
    }
    return nullptr;
  }
  return nullptr;
}

int CompareRetailSignedDword(const std::uint32_t left,
                             const std::uint32_t right) noexcept {

  return std::bit_cast<std::int32_t>(left - right);
}

int CompareWmoAreaKey(const std::uint32_t wmo_id,
                      const std::uint32_t name_set_id,
                      const std::int32_t wmo_group_id,
                      const dbc::WMOAreaTableEntry &entry) noexcept {
  if (const int result = CompareRetailSignedDword(wmo_id, entry.wmo_id);
      result != 0) {
    return result;
  }
  if (const int result =
          CompareRetailSignedDword(name_set_id, entry.name_set_id);
      result != 0) {
    return result;
  }
  return CompareRetailSignedDword(
      static_cast<std::uint32_t>(wmo_group_id),
      static_cast<std::uint32_t>(entry.wmo_group_id));
}

void ApplyDbClientCompressionSetting() {
  auto &cvars = openwow::ui::game::CVarSystem::Instance();
  if (cvars.Exists("dbCompress")) {
    switch (cvars.GetCVarInt("dbCompress")) {
    case -1:
    case 1:
      g_dbc_compress = true;
      break;
    case 0:
      g_dbc_compress = false;
      break;
    default:
      break;
    }
  }

  WowClientDB_SetCompressionEnabled(g_dbc_compress);
}

void RunDbClientPostLoadInitialization() {
  if (const auto *loader = GetBoundDbcTableRegistryLoader(); loader != nullptr) {
    openwow::game::DBClient_InitializeGameTables(loader->game_tables());
  }
  DBClient_BuildResistancesIndex();
  DBClient_BuildTerrainTypeSoundIdIndex();
  DBClient_BuildItemSubClassIndex();
  DBClient_BuildSubClassMaskTable();
  openwow::game::DBClient_InitializeLightDB(1);
}

}

static std::vector<const dbc::ResistancesEntry *> g_resistances_index;
static int g_armor_resistance_index = -1;
static std::vector<std::uint32_t> g_terrain_type_sound_ids;

static std::vector<const dbc::ItemSubClassEntry *> g_item_subclass_index;

static const dbc::ItemClassEntry *g_active_item_class = nullptr;

static const dbc::ItemSubClassEntry *g_default_item_subclass = nullptr;

static const dbc::DbcLoader *g_bound_dbc_loader = nullptr;

static constexpr std::uint32_t kItemClassSlotCount = 17u;
static std::array<std::vector<const dbc::ItemSubClassEntry *>, kItemClassSlotCount>
    g_subclass_mask_table;

int DBClient_Initialize(dbc::DbcLoader &loader, const openwow::vfs::VirtualFileSystem &vfs) {
  BindDbcTableRegistryLoader(&loader);
  ApplyDbClientCompressionSetting();

  loader.LoadAreaTableRetailStrict(vfs);
  const int loaded_count = loader.LoadAll(vfs);
  RunDbClientPostLoadInitialization();
  return loaded_count;
}

void DBClient_FindActiveItemClass() {
  g_active_item_class = nullptr;

  if (g_bound_dbc_loader == nullptr) {
    return;
  }

  const auto &entries = g_bound_dbc_loader->item_class().entries();

  for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
    if ((it->flags & kActiveItemClassFlag) != 0) {
      g_active_item_class = &*it;
    }
  }
}

const dbc::DbcLoader *GetBoundDbcTableRegistryLoader() {
  return g_bound_dbc_loader;
}

void DBClient_BuildResistancesIndex() {
  const auto *const loader = g_bound_dbc_loader;
  const auto raw_count = loader != nullptr ? loader->resistances().size() : 0u;
  auto count = raw_count;

  g_armor_resistance_index = -1;

  if (loader != nullptr && raw_count != kRetailSpellSchoolCount) {
    openwow::core::ida::ConsoleLog(
        "Warning: The Resistances table has the wrong number of entries");
    count = std::min(count, kRetailSpellSchoolCount);
  }

  g_resistances_index.assign(count, nullptr);

  if (loader == nullptr) {
    return;
  }

  const auto &entries = loader->resistances().entries();
  for (uint32_t i = 0; i < count; ++i) {
    const auto *const record = &entries[i];
    g_resistances_index[i] = record;
    if ((record->flags & kArmorResistanceFlag) != 0) {
      g_armor_resistance_index = static_cast<int>(i);
    }
  }
}

int DBClient_GetArmorResistanceIndex(const dbc::DbcLoader *dbc) {
  if (dbc != nullptr) {
    g_armor_resistance_index = FindArmorResistanceIndex(*dbc);
  }
  return g_armor_resistance_index;
}

std::uint32_t DBClient_GetArmorResistanceMask(const dbc::DbcLoader *dbc) {
  const auto armor_index = static_cast<std::uint32_t>(
      DBClient_GetArmorResistanceIndex(dbc));
  return DwordBit(armor_index);
}

std::uint32_t GetResistanceFizzleSoundId(std::uint32_t school_index) {
  if (school_index >= kRetailSpellSchoolCount) {
    return 0;
  }

  if (school_index >= g_resistances_index.size()) {
    return 0;
  }

  const auto *entry = g_resistances_index[school_index];
  if (entry == nullptr) {
    return 0;
  }

  return entry->fizzle_sound_id;
}

void DBClient_BuildTerrainTypeSoundIdIndex() {
  g_terrain_type_sound_ids.clear();
  if (g_bound_dbc_loader == nullptr || g_bound_dbc_loader->terrain_type().empty()) {
    return;
  }

  const auto slot_count = g_bound_dbc_loader->terrain_type().max_id() + 1u;
  g_terrain_type_sound_ids.assign(slot_count, 0u);
  for (const auto &entry : g_bound_dbc_loader->terrain_type().entries()) {
    if (entry.id < g_terrain_type_sound_ids.size()) {
      g_terrain_type_sound_ids[entry.id] = entry.sound_id;
    }
  }
}

std::uint32_t DBClient_GetTerrainTypeSoundId(const std::uint32_t terrain_type_id) {
  return terrain_type_id < g_terrain_type_sound_ids.size()
             ? g_terrain_type_sound_ids[terrain_type_id]
             : 0u;
}

void CombatData_Shutdown() {
  std::vector<const dbc::ResistancesEntry *>().swap(g_resistances_index);
  std::vector<std::uint32_t>().swap(g_terrain_type_sound_ids);
}

void DBClient_BuildItemSubClassIndex() {
  g_item_subclass_index.clear();

  g_default_item_subclass = nullptr;
  DBClient_FindActiveItemClass();
  if (g_bound_dbc_loader == nullptr || g_active_item_class == nullptr) {
    return;
  }

  const auto &entries = g_bound_dbc_loader->item_sub_class().entries();
  const auto active_class_id = g_active_item_class->id;

  std::uint32_t max_subclass_plus_one = 0;
  for (const auto &entry : entries) {
    if (entry.class_id != active_class_id) {
      continue;
    }

    max_subclass_plus_one = std::max(max_subclass_plus_one, entry.subclass_id + 1u);
  }

  if (max_subclass_plus_one == 0) {
    return;
  }

  g_item_subclass_index.assign(max_subclass_plus_one, nullptr);

  for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
    if (it->class_id != active_class_id || it->subclass_id >= max_subclass_plus_one) {
      continue;
    }

    g_item_subclass_index[it->subclass_id] = &*it;
    if ((it->flags & kDefaultItemSubClassFlag) != 0) {
      g_default_item_subclass = &*it;
    }
  }
}

void DBClient_BuildSubClassMaskTable() {
  if (g_bound_dbc_loader == nullptr) {
    for (auto &slot : g_subclass_mask_table) {
      slot.clear();
    }
    return;
  }

  const auto &entries = g_bound_dbc_loader->item_sub_class().entries();

  std::uint32_t max_subclass_per_class[kItemClassSlotCount] = {};
  for (const auto &entry : entries) {
    const auto cls = entry.class_id;
    if (cls >= kItemClassSlotCount) {
      continue;
    }
    const auto needed = entry.subclass_id + 1u;
    if (max_subclass_per_class[cls] < needed) {
      max_subclass_per_class[cls] = needed;
    }
  }

  for (std::uint32_t c = 0; c < kItemClassSlotCount; ++c) {
    const auto new_count = max_subclass_per_class[c];
    auto &slot = g_subclass_mask_table[c];
    slot.assign(new_count, nullptr);
  }

  for (const auto &entry : entries) {
    const auto cls = entry.class_id;
    if (cls >= kItemClassSlotCount) {
      continue;
    }
    auto &slot = g_subclass_mask_table[cls];
    if (entry.subclass_id < slot.size()) {
      slot[entry.subclass_id] = &entry;
    }
  }
}

const dbc::ItemSubClassEntry *DBClient_GetSubClassMaskEntry(
    const std::uint32_t class_id, const std::uint32_t subclass_id) {
  if (class_id >= kItemClassSlotCount) {
    return nullptr;
  }
  const auto &slot = g_subclass_mask_table[class_id];
  if (subclass_id >= slot.size()) {
    return nullptr;
  }
  return slot[subclass_id];
}

const dbc::ItemSubClassEntry *FindItemSubClassEntryByClassAndSubclassMask(
    const std::uint32_t class_id, const std::uint32_t subclass_mask) {
  if (g_bound_dbc_loader == nullptr) {
    return nullptr;
  }

  const auto &entries = g_bound_dbc_loader->item_sub_class().entries();
  for (const auto &entry : entries) {
    if (entry.class_id == class_id &&
        (DwordBit(entry.subclass_id) & subclass_mask) != 0) {
      return &entry;
    }
  }

  return nullptr;
}

const dbc::ItemSubClassEntry *DBClient_GetItemSubClassEntryByIndex(const int index) {
  if (g_bound_dbc_loader == nullptr) {
    return nullptr;
  }

  return g_bound_dbc_loader->item_sub_class().LookupEntryByRowIndex(index);
}

int DBClient_GetItemSubClassCount() {
  return static_cast<int>(g_item_subclass_index.size());
}

std::uint32_t DBClient_GetDefaultItemSubClassId() {
  return g_default_item_subclass != nullptr ? g_default_item_subclass->subclass_id : 0u;
}

bool DBClient_GetWeaponItemSubClassCount(std::uint32_t &count) {
  count = 1;

  if (g_bound_dbc_loader == nullptr) {
    return false;
  }

  bool has_weapon_subclass = false;
  std::uint32_t max_weapon_subclass = 0;
  const auto &entries = g_bound_dbc_loader->item_sub_class().entries();
  for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
    if (it->class_id != kWeaponItemClassId) {
      continue;
    }

    has_weapon_subclass = true;
    max_weapon_subclass = std::max(max_weapon_subclass, it->subclass_id);
  }

  if (has_weapon_subclass) {
    count = max_weapon_subclass + 1u;
  }

  return has_weapon_subclass;
}

namespace {

constexpr std::uint32_t kSpellAttr0Passive    = 0x40u;
constexpr std::uint32_t kSpellEffectDefense   = 26u;

std::vector<std::uint32_t> s_weapon_proficiency_spell_map;
std::uint32_t              s_default_defense_proficiency_spell_id = 0;

}

void BuildWeaponProficiencySpellMap() {
  if (g_bound_dbc_loader == nullptr) {
    return;
  }

  std::uint32_t weapon_subclass_count = 0;
  if (!DBClient_GetWeaponItemSubClassCount(weapon_subclass_count)) {
    return;
  }

  s_weapon_proficiency_spell_map.assign(weapon_subclass_count, 0u);
  s_default_defense_proficiency_spell_id = 0;

  const auto& spells = g_bound_dbc_loader->spell().entries();

  for (auto it = spells.rbegin(); it != spells.rend(); ++it) {
    const auto& spell = *it;

    if ((spell.attributes & kSpellAttr0Passive) == 0) {
      continue;
    }

    if (s_default_defense_proficiency_spell_id == 0 &&
        spell.effect[0] == kSpellEffectDefense) {
      s_default_defense_proficiency_spell_id = spell.id;
    }

    if (spell.equipped_item_class != static_cast<std::int32_t>(kWeaponItemClassId)) {
      continue;
    }

    const auto subclass_mask =
        static_cast<std::uint32_t>(spell.equipped_item_sub_class_mask);

    if (!std::has_single_bit(subclass_mask)) {
      continue;
    }
    const auto subclass_index =
        static_cast<std::uint32_t>(std::countr_zero(subclass_mask));
    if (subclass_index < weapon_subclass_count) {
      s_weapon_proficiency_spell_map[subclass_index] = spell.id;
    }
  }
}

std::uint32_t GetWeaponProficiencySpellForSubClass(std::uint32_t subclass_index) {
  if (subclass_index >= s_weapon_proficiency_spell_map.size()) {
    return 0;
  }
  return s_weapon_proficiency_spell_map[subclass_index];
}

std::uint32_t GetWeaponProficiencySpellMapSize() {
  return static_cast<std::uint32_t>(s_weapon_proficiency_spell_map.size());
}

std::uint32_t GetDefaultDefenseProficiencySpellId() {
  return s_default_defense_proficiency_spell_id;
}

void ClearWeaponProficiencySpellMap() {
  s_weapon_proficiency_spell_map.clear();
  s_weapon_proficiency_spell_map.shrink_to_fit();
  s_default_defense_proficiency_spell_id = 0;
}

bool DBClient_MaterialUsesWeaponImpactParryTypeOne(const std::uint32_t material_id) {
  if (g_bound_dbc_loader == nullptr) {
    return false;
  }

  const auto *material = g_bound_dbc_loader->material().LookupEntry(material_id);
  return material != nullptr &&
         (material->flags & kMaterialWeaponImpactParryTypeOneFlag) != 0u;
}

std::uint32_t DBClient_MaterialGetArmorSoundCategory(const std::uint32_t material_id) {
  if (g_bound_dbc_loader == nullptr) {
    return 0;
  }

  const auto *material = g_bound_dbc_loader->material().LookupEntry(material_id);
  if (material == nullptr) {
    return 0;
  }

  if ((material->flags & kMaterialHardArmorSoundFlag) != 0u) {
    return kMaterialHardArmorSoundCategory;
  }

  return (material->flags & kMaterialChainArmorSoundFlag) >>
         kMaterialChainArmorSoundShift;
}

void BindDbcTableRegistryLoader(const dbc::DbcLoader *dbc) {
  g_bound_dbc_loader = dbc;
}

std::string_view DBClient_GetFileDataFilename(const std::uint32_t file_data_id) {
  if (g_bound_dbc_loader == nullptr) {
    return {};
  }
  const auto *entry = g_bound_dbc_loader->file_data().LookupEntry(file_data_id);
  return entry != nullptr ? entry->filename : std::string_view{};
}

const dbc::MapDifficultyEntry *DBClient_FindMapDifficulty(
    const dbc::DbcLoader *dbc, const std::uint32_t map_id,
    const std::uint32_t difficulty, std::uint32_t *row_index) {
  if (dbc == nullptr) {
    return nullptr;
  }

  return FindInFirstContiguousPrimaryRun(
      dbc->map_difficulty().entries(), map_id, difficulty,
      [](const dbc::MapDifficultyEntry &entry) { return entry.map_id; },
      [](const dbc::MapDifficultyEntry &entry) { return entry.difficulty; }, row_index);
}

const dbc::LfgDungeonExpansionEntry *DBClient_FindLfgDungeonExpansion(
    const dbc::DbcLoader *dbc, const std::uint32_t lfg_id,
    const std::uint32_t expansion) {
  if (dbc == nullptr) {
    return nullptr;
  }

  return FindInFirstContiguousPrimaryRun(
      dbc->lfg_dungeon_expansion().entries(), lfg_id, expansion,
      [](const dbc::LfgDungeonExpansionEntry &entry) { return entry.lfg_id; },
      [](const dbc::LfgDungeonExpansionEntry &entry) { return entry.expansion; });
}

std::uint32_t DBClient_GetMapDifficultyRaidDuration(
    const dbc::DbcLoader *dbc, const std::uint32_t map_id,
    const std::uint32_t difficulty) {
  const auto *entry = DBClient_FindMapDifficulty(dbc, map_id, difficulty);
  return entry != nullptr ? entry->raid_duration : 0u;
}

const dbc::LiquidTypeEntry *DBClient_ResolveAreaLiquidType(
    const dbc::DbcLoader *dbc, const std::uint32_t area_id,
    const std::uint32_t liquid_type_id) {
  if (dbc == nullptr || liquid_type_id == 0u) {
    return nullptr;
  }

  if (area_id != 0u &&
      liquid_type_id - 1u < kAreaLiquidOverrideIdCount) {
    const std::size_t variation =
        (liquid_type_id - 1u) & kLiquidCategoryMask;
    if (const auto *area = dbc->area_table().LookupEntry(area_id);
        area != nullptr) {
      std::uint32_t resolved_id = area->liquid_type_id[variation];
      if (resolved_id == 0u && area->parent_area != 0u) {
        if (const auto *parent =
                dbc->area_table().LookupEntry(area->parent_area);
            parent != nullptr) {
          resolved_id = parent->liquid_type_id[variation];
        }
      }

      if (resolved_id != 0u) {
        return dbc->liquid_type().LookupEntry(resolved_id);
      }
    }
  }

  return dbc->liquid_type().LookupEntry(liquid_type_id);
}

const dbc::WMOAreaTableEntry *DBClient_FindWmoAreaTable(
    const dbc::DbcLoader *dbc, const std::uint32_t wmo_id,
    const std::uint32_t name_set_id, const std::int32_t wmo_group_id) {
  if (dbc == nullptr) {
    return nullptr;
  }

  const auto &entries = dbc->wmo_area_table().entries();
  std::size_t base = 0u;
  std::size_t count = entries.size();
  while (count != 0u) {
    const std::size_t half = count / 2u;
    const std::size_t index = base + half;
    const int comparison = CompareWmoAreaKey(
        wmo_id, name_set_id, wmo_group_id, entries[index]);
    if (comparison == 0) {
      return &entries[index];
    }
    if (comparison < 0) {
      count = half;
    } else {
      base = index + 1u;
      count -= half + 1u;
    }
  }

  return nullptr;
}

std::uint32_t ResolveMaxPlayersForMapDifficulty(
    const dbc::MapEntry& map_entry,
    std::uint32_t difficulty) {
  const dbc::DbcLoader* dbc = GetBoundDbcTableRegistryLoader();
  if (dbc == nullptr) {
    return map_entry.max_players;
  }

  const auto *entry = DBClient_FindMapDifficulty(dbc, map_entry.id, difficulty);
  if (entry != nullptr && entry->max_players != 0) {
    return entry->max_players;
  }
  return map_entry.max_players;
}

int FindLocaleRingIndexOrEnUSFallback(const char *locale_code) {
  return openwow::data::FindStartupLocaleRingIndexOrEnUSFallback(locale_code ? locale_code : "");
}

}
