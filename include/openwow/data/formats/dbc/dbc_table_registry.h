#pragma once

#include <cstdint>
#include <string_view>

namespace openwow::data::dbc {
struct AreaTableEntry;
class DbcLoader;
struct ItemClassEntry;
struct ItemSubClassEntry;
struct LfgDungeonExpansionEntry;
struct LiquidTypeEntry;
struct MapEntry;
struct MapDifficultyEntry;
struct ResistancesEntry;
struct WMOAreaTableEntry;
}

namespace openwow::vfs {
class VirtualFileSystem;
}

namespace openwow::data {

int DBClient_Initialize(dbc::DbcLoader &loader, const openwow::vfs::VirtualFileSystem &vfs);

void DBClient_FindActiveItemClass();

void DBClient_BuildResistancesIndex();
[[nodiscard]] int DBClient_GetArmorResistanceIndex(const dbc::DbcLoader *dbc);
[[nodiscard]] std::uint32_t DBClient_GetArmorResistanceMask(const dbc::DbcLoader *dbc);

[[nodiscard]] std::uint32_t GetResistanceFizzleSoundId(std::uint32_t school_index);

void DBClient_BuildTerrainTypeSoundIdIndex();
[[nodiscard]] std::uint32_t DBClient_GetTerrainTypeSoundId(std::uint32_t terrain_type_id);

void CombatData_Shutdown();

void DBClient_BuildItemSubClassIndex();

void DBClient_BuildSubClassMaskTable();

[[nodiscard]] const dbc::ItemSubClassEntry *DBClient_GetSubClassMaskEntry(
    std::uint32_t class_id, std::uint32_t subclass_id);

[[nodiscard]] const dbc::ItemSubClassEntry *FindItemSubClassEntryByClassAndSubclassMask(
    std::uint32_t class_id, std::uint32_t subclass_mask);

[[nodiscard]] const dbc::ItemSubClassEntry *DBClient_GetItemSubClassEntryByIndex(int index);

int DBClient_GetItemSubClassCount();

std::uint32_t DBClient_GetDefaultItemSubClassId();

bool DBClient_GetWeaponItemSubClassCount(std::uint32_t &count);

void BuildWeaponProficiencySpellMap();

[[nodiscard]] std::uint32_t GetWeaponProficiencySpellForSubClass(std::uint32_t subclass_index);

[[nodiscard]] std::uint32_t GetWeaponProficiencySpellMapSize();

[[nodiscard]] std::uint32_t GetDefaultDefenseProficiencySpellId();

void ClearWeaponProficiencySpellMap();

[[nodiscard]] bool DBClient_MaterialUsesWeaponImpactParryTypeOne(std::uint32_t material_id);

[[nodiscard]] std::uint32_t DBClient_MaterialGetArmorSoundCategory(std::uint32_t material_id);

void BindDbcTableRegistryLoader(const dbc::DbcLoader *dbc);
[[nodiscard]] const dbc::DbcLoader *GetBoundDbcTableRegistryLoader();

[[nodiscard]] std::string_view DBClient_GetFileDataFilename(std::uint32_t file_data_id);

[[nodiscard]] const dbc::MapDifficultyEntry *DBClient_FindMapDifficulty(
    const dbc::DbcLoader *dbc, std::uint32_t map_id, std::uint32_t difficulty,
    std::uint32_t *row_index = nullptr);
[[nodiscard]] const dbc::LfgDungeonExpansionEntry *DBClient_FindLfgDungeonExpansion(
    const dbc::DbcLoader *dbc, std::uint32_t lfg_id, std::uint32_t expansion);

[[nodiscard]] std::uint32_t DBClient_GetMapDifficultyRaidDuration(
    const dbc::DbcLoader *dbc, std::uint32_t map_id, std::uint32_t difficulty);

[[nodiscard]] const dbc::LiquidTypeEntry *DBClient_ResolveAreaLiquidType(
    const dbc::DbcLoader *dbc, std::uint32_t area_id,
    std::uint32_t liquid_type_id);

[[nodiscard]] const dbc::WMOAreaTableEntry *DBClient_FindWmoAreaTable(
    const dbc::DbcLoader *dbc, std::uint32_t wmo_id,
    std::uint32_t name_set_id, std::int32_t wmo_group_id);

std::uint32_t ResolveMaxPlayersForMapDifficulty(
    const dbc::MapEntry& map_entry,
    std::uint32_t difficulty);

int FindLocaleRingIndexOrEnUSFallback(const char *locale_code);

}
