#include "openwow/ui/game/tooltip_formatter.h"
#include "openwow/ui/game/tooltip_runtime.h"
#include "openwow/ui/game/tooltip_internal.h"
#include "openwow/ui/game/tooltip_types.h"

#include "openwow/core/storm_containers.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_table_registry.h"
#include "openwow/game/commerce/auctions/auction_interaction.h"
#include "openwow/game/inventory/search/action_item_inventory_search.h"
#include "openwow/game/client_config.h"
#include "openwow/game/container_slot_mapping.h"
#include "openwow/game/currency_system.h"
#include "openwow/game/activities/dance/application/dance_studio.h"
#include "openwow/game/inventory/equipment/equipment_sets.h"
#include "openwow/game/guild_system.h"
#include "openwow/game/hyperlink.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/items/item_link_parser.h"
#include "openwow/game/inventory/loot/loot_interaction.h"
#include "openwow/game/commerce/mail/mail_interaction.h"
#include "openwow/game/commerce/mail/mail_compose_state.h"
#include "openwow/game/actions/macros/application/macro_catalog.h"
#include "openwow/game/objects/cgitem.h"
#include "openwow/game/objects/cgcorpse.h"
#include "openwow/game/localization.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/quest_dialog_text.h"
#include "openwow/game/quest_log.h"
#include "openwow/game/quest_query_bridge.h"
#include "openwow/game/reputation_info.h"
#include "openwow/game/script_event_helpers.h"
#include "openwow/game/shapeshift_form_resolver.h"
#include "openwow/game/spell_cooldown_state.h"
#include "openwow/game/spellbook_system.h"

#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/game/spell_text_formatter.h"
#include "openwow/game/spell_target_validation.h"
#include "openwow/game/spell_query_bridge.h"
#include "openwow/game/talent_info.h"
#include "openwow/game/commerce/trade/trade_interaction.h"
#include "openwow/game/commerce/trade/trade_item_location.h"
#include "openwow/game/unit_level_display.h"
#include "openwow/game/world_session.h"
#include "openwow/game/inventory/items/item_interactions.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/api/game_lua_api_action.h"
#include "openwow/ui/game/api/game_lua_api_craft.h"
#include "openwow/ui/game/api/game_lua_api_quest.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/api/game_lua_api_tradeskill_state.h"
#include "openwow/ui/game/loot_tooltip_support.h"
#include "openwow/ui/game/merchant_repair_cost.h"
#include "openwow/ui/game/quest_special_item.h"
#include "openwow/ui/game/quest_leaderboard_builder.h"
#include "openwow/ui/game/tooltip_system.h"
#include "openwow/ui/game/trade_cursor_utils.h"
#include "openwow/ui/widgets/script_object.h"
#include "openwow/game/aura_lua_bridge.h"
#include "openwow/foundation/text/ascii.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::ui::game {
using namespace tooltip_internal;

namespace tooltip_internal {

std::string GetTooltipString(const char* key) {
  return openwow::game::Localization::Get().GetString(key, key);
}

std::string GetTooltipString(const std::string& key) {
  return openwow::game::Localization::Get().GetString(key, key);
}

}

static char s_linkBuffer[1024];

namespace {
constexpr const char *kQuestLinkColorRed = "|cffff2020";
constexpr const char *kQuestLinkColorOrange = "|cffff8040";
constexpr const char *kQuestLinkColorYellow = "|cffffff00";
constexpr const char *kQuestLinkColorGreen = "|cff40c040";
constexpr const char *kQuestLinkColorGray = "|cff808080";

const char *GetQuestLinkDifficultyColor(const openwow::game::CGPlayer_C *activePlayer,
                                        const std::int32_t questLevel) {
  if (activePlayer == nullptr) {
    return kQuestLinkColorYellow;
  }

  const auto player_level = static_cast<std::int32_t>(activePlayer->State().GetLevel());
  const auto level_delta = questLevel - player_level;
  if (level_delta >= 5) {
    return kQuestLinkColorRed;
  }
  if (level_delta >= 3) {
    return kQuestLinkColorOrange;
  }
  if (level_delta >= -2) {
    return kQuestLinkColorYellow;
  }

  const auto green_range =
      static_cast<std::int32_t>(
        openwow::ui::game::detail::GetQuestGreenRangeForPlayerLevel(activePlayer->State().GetLevel()));
  return -level_delta <= green_range ? kQuestLinkColorGreen : kQuestLinkColorGray;
}

}

const char *GetStatModifierGlobalStringName(uint32_t modId, char *out, int outSize) {
  static const char *kStatModNames[] = {
       "ITEM_MOD_MANA",
       "ITEM_MOD_HEALTH",
       "",
       "ITEM_MOD_AGILITY",
       "ITEM_MOD_STRENGTH",
       "ITEM_MOD_INTELLECT",
       "ITEM_MOD_SPIRIT",
       "ITEM_MOD_STAMINA",
       "",
       "",
       "",
       "",
       "ITEM_MOD_DEFENSE_SKILL_RATING",
       "ITEM_MOD_DODGE_RATING",
       "ITEM_MOD_PARRY_RATING",
       "ITEM_MOD_BLOCK_RATING",
       "ITEM_MOD_HIT_MELEE_RATING",
       "ITEM_MOD_HIT_RANGED_RATING",
       "ITEM_MOD_HIT_SPELL_RATING",
       "ITEM_MOD_CRIT_MELEE_RATING",
       "ITEM_MOD_CRIT_RANGED_RATING",
       "ITEM_MOD_CRIT_SPELL_RATING",
       "ITEM_MOD_HIT_TAKEN_MELEE_RATING",
       "ITEM_MOD_HIT_TAKEN_RANGED_RATING",
       "ITEM_MOD_HIT_TAKEN_SPELL_RATING",
       "ITEM_MOD_CRIT_TAKEN_MELEE_RATING",
       "ITEM_MOD_CRIT_TAKEN_RANGED_RATING",
       "ITEM_MOD_CRIT_TAKEN_SPELL_RATING",
       "ITEM_MOD_HASTE_MELEE_RATING",
       "ITEM_MOD_HASTE_RANGED_RATING",
       "ITEM_MOD_HASTE_SPELL_RATING",
       "ITEM_MOD_HIT_RATING",
       "ITEM_MOD_CRIT_RATING",
       "ITEM_MOD_HIT_TAKEN_RATING",
       "ITEM_MOD_CRIT_TAKEN_RATING",
       "ITEM_MOD_RESILIENCE_RATING",
       "ITEM_MOD_HASTE_RATING",
       "ITEM_MOD_EXPERTISE_RATING",
       "ITEM_MOD_ATTACK_POWER",
       "ITEM_MOD_RANGED_ATTACK_POWER",
       "",
       "ITEM_MOD_SPELL_HEALING_DONE",
       "ITEM_MOD_SPELL_DAMAGE_DONE",
       "ITEM_MOD_MANA_REGENERATION",
       "ITEM_MOD_ARMOR_PENETRATION_RATING",
       "ITEM_MOD_SPELL_POWER",
       "ITEM_MOD_HEALTH_REGEN",
       "ITEM_MOD_SPELL_PENETRATION",
       "ITEM_MOD_BLOCK_VALUE",
  };

  static const char *kSocketNames[] = {"META", "RED", "YELLOW", "BLUE"};

  if (modId >= 11 && modId <= 59) {
    std::snprintf(out, outSize, "%s_SHORT", kStatModNames[modId - 11]);
  } else if (modId < 7) {
    std::snprintf(out, outSize, "RESISTANCE%u_NAME", modId);
  } else if (modId >= 69 && modId <= 72) {
    std::snprintf(out, outSize, "EMPTY_SOCKET_%s", kSocketNames[modId - 69]);
  } else if (modId >= 61 && modId <= 67) {
    std::snprintf(out, outSize, "ITEM_MOD_POWER_REGEN%u_SHORT", modId - 61);
  } else {
    switch (modId) {
    case 7:
      std::snprintf(out, outSize, "ITEM_MOD_ATTACK_POWER_SHORT");
      break;
    case 8:
      std::snprintf(out, outSize, "ITEM_MOD_MELEE_ATTACK_POWER_SHORT");
      break;
    case 9:
      std::snprintf(out, outSize, "ITEM_MOD_RANGED_ATTACK_POWER_SHORT");
      break;
    case 10:
      std::snprintf(out, outSize, "ITEM_MOD_FERAL_ATTACK_POWER_SHORT");
      break;
    case 60:
      std::snprintf(out, outSize, "ITEM_MOD_BLOCK_VALUE_SHORT");
      break;
    case 68:
      std::snprintf(out, outSize, "ITEM_MOD_HEALTH_REGEN_SHORT");
      break;
    default:
      out[0] = '\0';
      break;
    }
  }
  return out;
}

int SpellAuraArray_GetElement(void *array, int index) {
  if (!array || index < 0) {
    return 0;
  }

  const auto *header = static_cast<const std::int32_t *>(array);
  const std::int32_t count = header[2];
  if (index >= count) {
    return 0;
  }

  const auto data_ptr_raw = *reinterpret_cast<const std::uint32_t *>(
      static_cast<const char *>(array) + 7 * sizeof(std::int32_t));
  if (data_ptr_raw == 0) {
    return 0;
  }

  return static_cast<int>(data_ptr_raw + 76 * index);
}

uint32_t FrameStackInfoArray_Add(uint32_t count, const FrameStackInfo *source) {
  return g_frameStackInfoArray.AppendCopiedRange(
      count,
      static_cast<uint32_t>(sizeof(FrameStackInfo)),
      source);
}

const char *FormatAchievementLink(int achievementId, uint64_t playerGuid, uint8_t completed,
                                  int month, int day, int year, uint32_t criteria1,
                                  uint32_t criteria2, uint32_t criteria3, uint32_t criteria4) {
  s_linkBuffer[0] = '\0';

  const auto *dbc = ResolveTooltipDbc(nullptr);
  if (dbc == nullptr || achievementId <= 0) {
    return s_linkBuffer;
  }

  const auto *achievement =
      dbc->achievement().LookupEntry(static_cast<std::uint32_t>(achievementId));
  if (achievement == nullptr) {
    return s_linkBuffer;
  }

  std::array<char, 800> expanded_name{};
  openwow::game::BindSpellTextFormatterDbcLoader(dbc);
  openwow::game::SpellTextFormatter::ExpandObjectTextVariables(
      achievement->name.data(), expanded_name.data(),
      static_cast<std::uint32_t>(expanded_name.size()), playerGuid, nullptr, 0, {}, 0,
      achievementId);

  std::snprintf(s_linkBuffer, sizeof(s_linkBuffer),
                "|cffffff00|Hachievement:%d:%016llX:%d:%d:%d:%d:%u:%u:%u:%u|h[%s]|h|r",
                achievementId, static_cast<unsigned long long>(playerGuid),
                static_cast<int>(completed), month + 1, day + 1, year, criteria1, criteria2,
                criteria3, criteria4, expanded_name.data());
  return s_linkBuffer;
}

const char *FormatQuestLink(const std::uint32_t questId, const std::int32_t questLevel,
                            const std::string_view questTitle,
                            const openwow::game::CGPlayer_C *activePlayer) {
  const char *const color_prefix = GetQuestLinkDifficultyColor(activePlayer, questLevel);
  const char *const reset_suffix = color_prefix[0] != '\0' ? "|r" : "";
  std::snprintf(s_linkBuffer, sizeof(s_linkBuffer), "%s|Hquest:%u:%d|h[%.*s]|h%s", color_prefix,
                questId, questLevel, static_cast<int>(questTitle.size()), questTitle.data(),
                reset_suffix);
  return s_linkBuffer;
}

const char *FormatGlyphLink(int slotId, int glyphId) {
  const auto *dbc = TooltipSystem::Get().GetDbcLoader();
  const auto *slot_entry = dbc ? dbc->glyph_slot().LookupEntry(slotId) : nullptr;
  const auto *glyph_entry = dbc ? dbc->glyph_properties().LookupEntry(glyphId) : nullptr;
  const auto *spell_entry =
      (dbc && glyph_entry) ? dbc->spell().LookupEntry(glyph_entry->spell_id) : nullptr;
  if (!slot_entry || !glyph_entry || !spell_entry || spell_entry->spell_name.empty()) {
    s_linkBuffer[0] = '\0';
    return s_linkBuffer;
  }

  std::snprintf(s_linkBuffer, sizeof(s_linkBuffer), "|cff66bbff|Hglyph:%d:%d|h[%.*s]|h|r", slotId,
                glyphId, static_cast<int>(spell_entry->spell_name.size()),
                spell_entry->spell_name.data());
  return s_linkBuffer;
}

int Tooltip_ComputeScalingLevel(
    const openwow::game::ItemTemplate& item,
    std::uint32_t contextLevel,
    std::uint64_t ownerGuid,
    bool forceDefault,
    const openwow::game::ObjectManager* objects,
    const openwow::data::dbc::DbcLoader* dbc,
    std::uint64_t merchantGuid) {
  using openwow::game::ObjectGuid;

  if (dbc == nullptr) {
    dbc = TooltipSystem::Get().GetDbcLoader();
  }
  if (objects == nullptr) {
    objects = TooltipSystem::Get().GetObjectManager();
  }

  const openwow::data::dbc::ScalingStatDistributionEntry* dist = nullptr;
  if (dbc != nullptr && item.scaling_stat_distribution != 0u) {
    dist = dbc->scaling_stat_distribution().LookupEntry(item.scaling_stat_distribution);
  }

  std::uint32_t level = 0;
  if (forceDefault) {
    level = 1;
  } else {
    if (contextLevel != 0u) {
      level = contextLevel;
    }

    if (level == 0u && ownerGuid != 0u && ownerGuid != merchantGuid && objects != nullptr) {
      if (const auto* unit = objects->GetUnit(ObjectGuid(ownerGuid)); unit != nullptr) {
        level = unit->State().GetLevel();
      }
    }

    if (level == 0u && objects != nullptr) {
      if (const auto* player = objects->GetActivePlayer(); player != nullptr) {
        level = player->State().GetLevel();
      }
    }
  }

  if (dist != nullptr && dist->max_level != 0u) {
    level = std::max(level, item.required_level);
    level = std::min(level, dist->max_level);
  }

  return static_cast<int>(std::max(level, 1u));
}

std::string BuildSummonTitleText(
    const openwow::game::CGUnit_C &unit,
    const openwow::data::dbc::DbcLoader *dbc,
    const openwow::game::ObjectManager *objects) {
  return FormatSummonTitleText(unit, dbc, objects);
}

void Tooltip_BuildSummonTitle([[maybe_unused]] const char *arg1, int unitData, char *out,
                              unsigned int outSize) {
  if (unitData == 0 || out == nullptr || outSize == 0) {
    return;
  }

  const auto *unit = reinterpret_cast<const openwow::game::CGUnit_C *>(unitData);
  const std::string title =
      BuildSummonTitleText(*unit, TooltipSystem::Get().GetDbcLoader(),
                           TooltipSystem::Get().GetObjectManager());
  if (title.empty()) {
    return;
  }

  std::snprintf(out, outSize, "%s", title.c_str());
}
uint8_t CGTooltip_GetAuraField13([[maybe_unused]] void *unitData, [[maybe_unused]] int auraIndex) {
  return 0;
}
uint8_t CGTooltip_GetAuraField14([[maybe_unused]] void *unitData, [[maybe_unused]] int auraIndex) {
  return 0;
}

}
