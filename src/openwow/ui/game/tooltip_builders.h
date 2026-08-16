#pragma once

#include "openwow/ui/game/tooltip_types.h"

#include <cstddef>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace openwow::data::dbc { class DbcLoader; }
namespace openwow::game {
class CGItem_C;
class CGPlayer_C;
class CGUnit_C;
class ObjectManager;
class QueryCache;
struct ItemTemplate;
struct TalentGroupData;
struct TalentInfoEntry;
}
namespace openwow::ui::game {
class TooltipSystem;

struct SpellTooltipRequest {
  TooltipSystem& tooltip;
  std::uint32_t spell_id = 0;
  std::chrono::milliseconds cooldown_remaining{0};
  bool simple = false;
  bool show_rank = true;
  bool inspect = false;
  bool append = false;
  bool talent_context = false;
  int current_rank = -1;
  int max_rank = -1;
  bool final = false;
};

struct AchievementTooltipRequest {
  TooltipSystem& tooltip;
  std::uint32_t achievement_id = 0;
  std::uint64_t player_guid = 0;
  bool completed = false;
  std::array<std::uint32_t, 8> criteria_data{};
  std::array<std::uint32_t, 4> criteria_mask{};
  bool async_rebuild = false;
};

struct TalentTooltipRequest {
  TooltipSystem& tooltip;
  const openwow::game::TalentInfoEntry& talent;
  const openwow::game::TalentGroupData* group = nullptr;
  bool inspect = false;
  std::optional<int> explicit_rank;
  bool is_pet = false;
  bool preview = false;
};

bool BuildUnitTooltipForUnit(TooltipSystem& tooltip,
                             const openwow::game::CGUnit_C& unit,
                             const openwow::game::ObjectManager* objects,
                             const openwow::data::dbc::DbcLoader* dbc,
                             bool hide_status);
void BuildCorpseTooltip(TooltipSystem& tooltip, std::uint64_t corpse_guid);
bool BuildQuestTooltip(TooltipSystem& tooltip, std::uint32_t quest_id);
void BuildGlyphTooltip(TooltipSystem& tooltip, std::uint32_t slot_id,
                       std::uint32_t glyph_id, bool enabled, bool can_remove);
int BuildEquipmentSetTooltip(TooltipSystem& tooltip, std::uint32_t set_id);
bool BuildSpellTooltip(const SpellTooltipRequest& request);
void BuildSimpleSpellTooltip(TooltipSystem& tooltip, std::uint32_t spell_id);
void BuildTalentTooltip(const TalentTooltipRequest& request);
bool BuildAchievementTooltip(const AchievementTooltipRequest& request);
bool ItemTooltip_TryResolveComparisonSlot(ItemComparisonContext& context, int pass,
                                          std::uint32_t item_entry,
                                          std::uint32_t slot_index,
                                          const openwow::game::QueryCache& cache);
void CGTooltip_AddDeltaDescriptionHeader(void* flag, void* tooltip);
void CGTooltip_AddStatDeltaLine(void* tooltip, int delta, const char* stat_name,
                                int* needs_header);
int Tooltip_BuildItemComparisonFromUnit(void* destination,
                                        const openwow::game::CGItem_C* item,
                                        std::uint32_t scaling_level);
int Tooltip_BuildItemComparisonFromTemplate(void* destination,
                                            const openwow::game::ItemTemplate* item,
                                            const void* enchant_data,
                                            std::uint32_t scaling_level);
int Tooltip_BuildItemStatBlock(void* destination, const void* tooltip_data);
int ItemTooltip_BuildComparisonData(const void* item1, const void* item2,
                                    char* output);
void CGTooltip_BuildUnitStatLines(void* tooltip, const void* unit_data,
                                  char* buffer, unsigned int buffer_size);
void CGTooltip_BuildUnitSkinningLine(void* tooltip, void* unit_data,
                                     char* buffer, unsigned int buffer_size);
std::string BuildSummonTitleText(const openwow::game::CGUnit_C& unit,
                                 const openwow::data::dbc::DbcLoader* dbc = nullptr,
                                 const openwow::game::ObjectManager* objects = nullptr);
void Tooltip_BuildSummonTitle(const char* arg1, int unit_data, char* output,
                              unsigned int output_size);

}
