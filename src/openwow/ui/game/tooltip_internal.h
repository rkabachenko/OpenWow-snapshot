#pragma once

#include "openwow/game/script_event_helpers.h"
#include "openwow/ui/game/tooltip_types.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/talent_info.h"
#include "openwow/core/storm_containers.h"
#include "openwow/core/storm_string.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct lua_State;
namespace openwow::data::dbc {
struct AchievementCriteriaEntry;
class DbcLoader;
struct CreatureFamilyEntry;
}
namespace openwow::game {
class CGPlayer_C;
class CGItem_C;
class CGUnit_C;
class ObjectManager;
class PlayerInventoryReplica;
class WorldSession;
struct CreatureTemplateInfo;
struct ItemInstance;
struct PetActionButton;
}
namespace openwow::ui::game {
class TooltipSystem;
struct QuestLeaderboardLine;
}

namespace openwow::ui::game::tooltip_internal {

inline constexpr float kTooltipGoldR = 1.0f;
inline constexpr float kTooltipGoldG = 210.0f / 255.0f;
inline constexpr float kTooltipGoldB = 0.0f;
inline constexpr float kTooltipGrayR = 128.0f / 255.0f;
inline constexpr float kTooltipGrayG = 128.0f / 255.0f;
inline constexpr float kTooltipGrayB = 128.0f / 255.0f;
inline constexpr float kTooltipMissingR = 1.0f;
inline constexpr float kTooltipMissingG = 32.0f / 255.0f;
inline constexpr float kTooltipMissingB = 32.0f / 255.0f;
inline constexpr float kTooltipCompleteR = 64.0f / 255.0f;
inline constexpr float kTooltipCompleteG = 192.0f / 255.0f;
inline constexpr float kTooltipCompleteB = 64.0f / 255.0f;
inline constexpr int kPetActionBarSlotCount = 10;
inline constexpr std::size_t kDietListBufferSize = 256;
inline constexpr std::size_t kPetSpellListBufferSize = 512;
inline constexpr std::uint32_t kSpellEffectSkinning = 95u;
inline constexpr std::uint32_t kSpellEffectSummon = 28u;
inline constexpr std::uint32_t kHumanoidCreatureTypeId = 7u;
inline constexpr const char* kSummonTitleHumanoidToken = "UNITNAME_SUMMON_TITLE1";
inline constexpr const char* kSummonTitleNonHumanoidToken = "UNITNAME_SUMMON_TITLE3";
inline constexpr std::uint32_t kCreatureTemplateTypeFlagTameable = 0x1u;
inline constexpr std::uint32_t kCreatureTemplateTypeFlagExotic = 0x10000u;
inline constexpr std::uint32_t kPetTooltipSpellAttributeMask = 0xC0u;
inline constexpr std::uintptr_t kAchievementNameCallbackFunctionId = 0x0067b270u;
inline constexpr std::uint32_t kCreatureTypeFlagHerbSkinnable = 0x100u;
inline constexpr std::uint32_t kCreatureTypeFlagMiningSkinnable = 0x200u;
inline constexpr std::uint32_t kCreatureTypeFlagEngineeringSkinnable = 0x8000u;
inline constexpr std::size_t kTooltipCompareItemGuidOffset = 832u;
inline constexpr std::size_t kTooltipCompareItemEntryOffset = 864u;
inline constexpr std::size_t kTooltipCompareSuffixFactorOffset = 1176u;
inline constexpr std::size_t kTooltipCompareRandomPropertyOffset = 1180u;
inline constexpr std::size_t kTooltipCompareLevelOffset = 1232u;
inline constexpr std::size_t kTooltipCompareEnchantSuffixOffset = 176u;
inline constexpr std::size_t kTooltipCompareEnchantRandomPropertyOffset = 180u;

struct DifficultyColor { float r; float g; float b; };
inline constexpr DifficultyColor kSkinningDifficultyColors[] = {
    {0.50f, 0.50f, 0.50f}, {0.25f, 0.75f, 0.25f}, {1.00f, 1.00f, 0.00f},
    {1.00f, 0.50f, 0.25f}, {1.00f, 0.13f, 0.13f},
};

struct TooltipComparisonCacheKey {
  std::uint32_t item_entry = 0;
  std::int32_t random_property_id = 0;
  std::uint32_t suffix_factor = 0;
  std::uint32_t scaling_level = 0;
  [[nodiscard]] bool Matches(const TooltipComparisonCacheKey& other) const {
    return item_entry == other.item_entry &&
           random_property_id == other.random_property_id &&
           scaling_level == other.scaling_level &&
           (random_property_id == 0 || suffix_factor == other.suffix_factor);
  }
};

struct TalentStaticLookup {
  const openwow::game::TalentInfoEntry* talent = nullptr;
  std::uint32_t tab_index = 0;
  std::uint32_t talent_index = 0;
};
struct TalentSpellDisplay {
  std::uint32_t spell_id = 0;
  std::string name;
  std::string rank;
  std::string description;
  [[nodiscard]] bool HasSpell() const { return spell_id != 0 && !name.empty(); }
};

extern openwow::core::TSGrowableArray<FrameStackInfo, 8> g_frameStackInfoArray;
TooltipSystem& ResolveTooltipState(void* tooltip);
openwow::game::WorldSession* GetWorldSessionForTooltip(lua_State* lua);
std::string GetTooltipString(const char* key);
std::string GetTooltipString(const std::string& key);
const openwow::data::dbc::DbcLoader* ResolveTooltipDbcLoader();
const openwow::data::dbc::DbcLoader* ResolveTooltipDbc(
    const openwow::data::dbc::DbcLoader* dbc);
openwow::game::WorldSession* ResolveTooltipWorldSession();
const std::vector<const openwow::data::dbc::AchievementCriteriaEntry*>&
GetAchievementTooltipCriteria(const openwow::data::dbc::DbcLoader& dbc,
                              std::uint32_t achievement_id);
std::uint32_t ReadTooltipComparisonU32(const void* base, std::size_t offset);
std::int32_t ReadTooltipComparisonS32(const void* base, std::size_t offset);
std::uint64_t ReadTooltipComparisonU64(const void* base, std::size_t offset);
bool BuildComparisonStatTableFromTemplate(openwow::game::ItemStatTable& table,
                                          const openwow::game::ItemTemplate& item,
                                          std::int32_t random_property_id,
                                          std::uint32_t suffix_factor,
                                          std::uint32_t scaling_level,
                                          const openwow::game::CGPlayer_C& player);
bool BuildComparisonStatTableFromItemObject(openwow::game::ItemStatTable& table,
                                            const openwow::game::CGItem_C& item,
                                            std::uint32_t scaling_level);
bool ResolveTooltipComparisonCacheKey(const void* data, TooltipComparisonCacheKey& key);
const openwow::game::CreatureTemplateInfo* ResolveTooltipCreatureTemplate(
    const openwow::game::CGUnit_C& unit);
const openwow::data::dbc::CreatureFamilyEntry* ResolveTooltipCreatureFamily(
    const openwow::game::CGUnit_C& unit, const openwow::data::dbc::DbcLoader* dbc,
    const openwow::game::CreatureTemplateInfo* creature_template);
bool BuildPetFoodTypeList(char* destination, std::size_t capacity,
                          const openwow::data::dbc::DbcLoader& dbc,
                          const openwow::data::dbc::CreatureFamilyEntry& family);
bool BuildPetSpellList(char* destination, std::size_t capacity,
                       const openwow::data::dbc::DbcLoader& dbc,
                       const openwow::data::dbc::CreatureFamilyEntry& family);
std::string FormatSummonTitleText(const openwow::game::CGUnit_C& unit,
                                  const openwow::data::dbc::DbcLoader* dbc,
                                  const openwow::game::ObjectManager* objects);
const char* ResolveTooltipObjectName(lua_State* lua);
int PushPetActionTooltip(lua_State* lua, const openwow::game::PetActionButton& action);
std::string ResolveTotemTooltipName(lua_State* lua, std::uint32_t spell_id);
std::string FormatTotemRemainingText(std::uint32_t remaining_ms);
std::uint32_t CountTalentRanks(const openwow::game::TalentInfoEntry& talent);
TalentStaticLookup FindTalentDefinition(std::uint32_t talent_id, bool inspect, bool is_pet);
std::int32_t ResolveTalentPointsForTooltip(const openwow::game::TalentInfoEntry& talent,
                                           const openwow::game::TalentGroupData* group,
                                           bool explicit_rank, int rank, bool preview);
TalentSpellDisplay ResolveTalentSpellDisplay(const openwow::game::TalentInfoEntry& talent,
                                             std::int32_t points);
TalentSpellDisplay ResolveSpellDisplay(std::uint32_t spell_id);
bool AppendTalentRequirementLines(TooltipSystem& tooltip,
                                  const openwow::game::TalentInfoEntry& talent,
                                  const openwow::game::TalentGroupData* group,
                                  bool inspect, bool is_pet, bool preview);
void AppendTalentActionLines(TooltipSystem& tooltip,
                             const openwow::game::TalentInfoEntry& talent,
                             const openwow::game::TalentGroupData* group,
                             bool requirements_met, std::int32_t current_points,
                             std::uint32_t max_rank, bool inspect,
                             bool explicit_rank, bool is_pet, bool preview);
bool InventoryContainsEquipmentSetItem(const openwow::game::PlayerInventoryReplica& inventory,
                                       std::uint64_t item_guid, bool bank_open);
void SetTooltipFromQuestSpecialItem(openwow::game::WorldSession& session,
                                    const openwow::game::ItemInstance& item);
bool BuildQuestTooltipFromSession(openwow::game::WorldSession* session,
                                  std::uint32_t quest_id);

template <typename... Args>
std::size_t FormatTooltipTextFromGlobalString(char* buffer, std::size_t buffer_size,
                                              const char* format, Args... args) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#pragma clang diagnostic ignored "-Wformat-security"
  return openwow::core::SStrPrintf(buffer, buffer_size, format, args...);
#pragma clang diagnostic pop
}

template <typename... Args>
std::string FormatTooltipLine(const char* token, Args... args) {
  if constexpr (sizeof...(Args) == 0) {
    return GetTooltipString(token);
  }
  std::array<char, 256> buffer{};
  FormatTooltipTextFromGlobalString(buffer.data(), buffer.size(),
                                    GetTooltipString(token).c_str(), args...);
  return buffer.data();
}

}
