#include "openwow/ui/game/tooltip_builders.h"
#include "openwow/ui/game/tooltip_internal.h"
#include "openwow/ui/game/tooltip_runtime.h"

#include "openwow/core/storm_containers.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_table_registry.h"
#include "openwow/game/actions/macros/application/macro_catalog.h"
#include "openwow/game/activities/dance/application/dance_studio.h"
#include "openwow/game/client_config.h"
#include "openwow/game/commerce/auctions/auction_interaction.h"
#include "openwow/game/commerce/mail/mail_compose_state.h"
#include "openwow/game/commerce/mail/mail_interaction.h"
#include "openwow/game/container_slot_mapping.h"
#include "openwow/game/currency_system.h"
#include "openwow/game/guild_system.h"
#include "openwow/game/hyperlink.h"
#include "openwow/game/inventory/equipment/equipment_sets.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/items/item_link_parser.h"
#include "openwow/game/inventory/loot/loot_interaction.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/search/action_item_inventory_search.h"
#include "openwow/game/localization.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgcorpse.h"
#include "openwow/game/objects/cgitem.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/quest_dialog_text.h"
#include "openwow/game/quest_log.h"
#include "openwow/game/quest_query_bridge.h"
#include "openwow/game/reputation_info.h"
#include "openwow/game/script_event_helpers.h"
#include "openwow/game/shapeshift_form_resolver.h"
#include "openwow/game/spell_cooldown_state.h"
#include "openwow/game/spellbook_system.h"

#include "openwow/foundation/text/ascii.h"
#include "openwow/game/aura_lua_bridge.h"
#include "openwow/game/commerce/trade/trade_interaction.h"
#include "openwow/game/commerce/trade/trade_item_location.h"
#include "openwow/game/inventory/items/item_interactions.h"
#include "openwow/game/spell_query_bridge.h"
#include "openwow/game/spell_target_validation.h"
#include "openwow/game/spell_text_formatter.h"
#include "openwow/game/talent_info.h"
#include "openwow/game/unit_level_display.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/api/game_lua_api_action.h"
#include "openwow/ui/game/api/game_lua_api_craft.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/api/game_lua_api_quest.h"
#include "openwow/ui/game/api/game_lua_api_tradeskill_state.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/loot_tooltip_support.h"
#include "openwow/ui/game/merchant_repair_cost.h"
#include "openwow/ui/game/quest_leaderboard_builder.h"
#include "openwow/ui/game/quest_special_item.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/game/tooltip_helpers.h"
#include "openwow/ui/game/tooltip_system.h"
#include "openwow/ui/game/trade_cursor_utils.h"
#include "openwow/ui/widgets/script_object.h"

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

std::uint32_t CountTalentRanks(const openwow::game::TalentInfoEntry &talent) {
  std::uint32_t count = 0;
  for (const auto spell_id : talent.spell_ids) {
    if (spell_id != 0) {
      ++count;
    }
  }
  return count;
}

TalentStaticLookup FindTalentDefinition(const std::uint32_t talent_id, const bool inspect,
                                        const bool is_pet) {
  const auto &store = openwow::game::TalentInfoStore::Get();
  const auto tab_count = store.GetTalentTabCount(inspect, is_pet);
  for (std::uint32_t tab_index = 0; tab_index < tab_count; ++tab_index) {
    const auto *tab = store.GetTalentTabArray(tab_index, inspect, is_pet);
    if (tab == nullptr) {
      continue;
    }

    for (std::uint32_t talent_index = 0; talent_index < tab->talents.size(); ++talent_index) {
      if (tab->talents[talent_index].talent_id == talent_id) {
        return {.talent = &tab->talents[talent_index],
                .tab_index = tab_index,
                .talent_index = talent_index};
      }
    }
  }

  return {};
}

std::int32_t ResolveTalentPointsForTooltip(const openwow::game::TalentInfoEntry &talent,
                                           const openwow::game::TalentGroupData *group,
                                           const bool has_explicit_rank, const int explicit_rank,
                                           const bool preview) {
  const std::int32_t max_rank = static_cast<std::int32_t>(CountTalentRanks(talent));
  if (has_explicit_rank && explicit_rank >= 0 && explicit_rank <= max_rank) {
    return explicit_rank;
  }

  if (group == nullptr) {
    return 0;
  }

  const auto it = group->talent_by_id.find(talent.talent_id);
  if (it == group->talent_by_id.end() || it->second == nullptr) {
    return 0;
  }

  const auto raw_rank = preview ? it->second->preview_rank : it->second->current_rank;
  return raw_rank < 0 ? 0 : raw_rank + 1;
}

TalentSpellDisplay ResolveSpellDisplay(const std::uint32_t spell_id) {
  if (spell_id == 0) {
    return {};
  }

  TalentSpellDisplay display{};
  display.spell_id = spell_id;

  if (const auto query = openwow::game::SpellQueryBridge::Get().Query(spell_id);
      query.has_value()) {
    display.name = query->name;
    display.rank = query->subtext;
    display.description =
        openwow::game::ResolveSpellDescriptionForDisplay(spell_id, query->description);
  }

  if (display.name.empty() || display.rank.empty()) {
    if (const auto *dbc = ResolveTooltipDbcLoader(); dbc != nullptr) {
      if (const auto *spell = dbc->spell().LookupEntry(spell_id); spell != nullptr) {
        if (display.name.empty() && !spell->spell_name.empty()) {
          display.name = std::string(spell->spell_name);
        }
        if (display.rank.empty() && !spell->rank.empty()) {
          display.rank = std::string(spell->rank);
        }
      }
    }
  }

  return display;
}

TalentSpellDisplay ResolveTalentSpellDisplay(const openwow::game::TalentInfoEntry &talent,
                                             const std::int32_t points) {
  if (points <= 0 || static_cast<std::size_t>(points - 1) >= talent.spell_ids.size()) {
    return {};
  }

  return ResolveSpellDisplay(talent.spell_ids[static_cast<std::size_t>(points - 1)]);
}

std::string ResolveTalentTabDisplayName(const std::uint32_t tab_id) {
  if (const auto *dbc = ResolveTooltipDbcLoader(); dbc != nullptr) {
    if (const auto *tab = dbc->talent_tab().LookupEntry(tab_id);
        tab != nullptr && !tab->name.empty()) {
      return std::string(tab->name);
    }
  }

  return "Talent Tab " + std::to_string(tab_id);
}

bool HasRequiredTalentSpell(const std::uint32_t spell_id, const bool inspect, const bool is_pet) {
  if (inspect) {
    return !is_pet;
  }

  if (is_pet) {
    return true;
  }

  const auto *session = ResolveTooltipWorldSession();
  if (spell_id == 0 || session == nullptr) {
    return false;
  }

  const auto *player = session->objects().GetActivePlayer();
  return player != nullptr &&
         (session->spell_book().HasSpell(spell_id) || player->Auras().HasSpellId(spell_id));
}

TalentSpellDisplay
ResolveTalentPrerequisiteSpellDisplay(const openwow::game::TalentInfoEntry &talent,
                                      const std::size_t minimum_rank_index) {
  for (std::size_t rank_index = minimum_rank_index; rank_index < talent.spell_ids.size();
       ++rank_index) {
    const auto display =
        ResolveTalentSpellDisplay(talent, static_cast<std::int32_t>(rank_index + 1));
    if (display.HasSpell()) {
      return display;
    }
  }

  return {};
}

bool AppendTalentRequirementLines(openwow::ui::game::TooltipSystem &tooltip,
                                  const openwow::game::TalentInfoEntry &talent,
                                  const openwow::game::TalentGroupData *group, const bool inspect,
                                  const bool is_pet, const bool preview) {
  auto &store = openwow::game::TalentInfoStore::Get();
  bool requirements_met = true;

  for (std::size_t prereq_index = 0; prereq_index < talent.prereq_talent.size(); ++prereq_index) {
    const auto prereq_talent_id = talent.prereq_talent[prereq_index];
    if (prereq_talent_id == 0) {
      continue;
    }

    const auto prereq_definition = store.FindTalentDefinitionByID(prereq_talent_id);
    if (!prereq_definition.has_value()) {
      continue;
    }

    const auto required_rank_index = static_cast<std::size_t>(talent.prereq_rank[prereq_index]);
    const auto required_points = static_cast<std::uint32_t>(required_rank_index + 1);
    std::uint32_t known_points = 0;
    if (group != nullptr) {
      const auto prereq_it = group->talent_by_id.find(prereq_talent_id);
      if (prereq_it != group->talent_by_id.end() && prereq_it->second != nullptr) {
        const auto raw_rank =
            preview ? prereq_it->second->preview_rank : prereq_it->second->current_rank;
        known_points = raw_rank < 0 ? 0u : static_cast<std::uint32_t>(raw_rank + 1);
      }
    }

    if (known_points >= required_points) {
      continue;
    }

    const auto prereq_spell =
        ResolveTalentPrerequisiteSpellDisplay(*prereq_definition, required_rank_index);
    if (prereq_spell.HasSpell()) {
      tooltip.AddLine(
          FormatTooltipLine("TOOLTIP_TALENT_PREREQ", required_points, prereq_spell.name.c_str()),
          1.0f, 0.1f, 0.1f);
    }
    requirements_met = false;
  }

  if (group != nullptr && talent.tier > 0) {
    const auto tab_info = group->FindTabInfoById(talent.tab_id);
    const auto current_points = tab_info.has_value()
                                    ? std::max(static_cast<std::int32_t>(tab_info->points_spent) +
                                                   tab_info->preview_points_spent,
                                               0)
                                    : 0;
    const auto required_points = talent.tier * (is_pet ? 3u : 5u);
    if (static_cast<std::uint32_t>(current_points) < required_points) {
      tooltip.AddLine(FormatTooltipLine("TOOLTIP_TALENT_TIER_POINTS", required_points,
                                        ResolveTalentTabDisplayName(talent.tab_id).c_str()),
                      1.0f, 0.1f, 0.1f);
      requirements_met = false;
    }
  }

  if (talent.required_spell_id != 0 &&
      !HasRequiredTalentSpell(talent.required_spell_id, inspect, is_pet)) {
    tooltip.AddLine(FormatTooltipLine("ITEM_REQ_SKILL",
                                      ResolveSpellDisplay(talent.required_spell_id).name.c_str()),
                    1.0f, 0.1f, 0.1f);
    requirements_met = false;
  }

  return requirements_met;
}

void AppendTalentActionLines(openwow::ui::game::TooltipSystem &tooltip,
                             const openwow::game::TalentInfoEntry &talent,
                             const openwow::game::TalentGroupData *group,
                             const bool requirements_met, const std::int32_t current_points,
                             const std::uint32_t max_rank, const bool inspect,
                             const bool has_explicit_rank, const bool is_pet, const bool preview) {
  if (group == nullptr || inspect || has_explicit_rank) {
    return;
  }

  const auto *const session = tooltip.GetWorldSession();
  if (session == nullptr || session->objects().GetActivePlayer() == nullptr) {
    return;
  }

  auto &store = openwow::game::TalentInfoStore::Get();
  const auto active_group_index = store.GetDefaultGroupIndex(is_pet);
  const auto *active_group = store.GetTalentGroupData(active_group_index, false, is_pet);
  if (active_group == nullptr || active_group != group) {
    return;
  }

  const auto lookup = FindTalentDefinition(talent.talent_id, false, is_pet);
  if (lookup.talent == nullptr) {
    return;
  }

  if (preview) {
    if (!requirements_met) {
      return;
    }

    if (store
            .ResolvePreviewPointDelta(active_group_index, lookup.tab_index, lookup.talent_index, 1,
                                      is_pet)
            .has_value()) {
      tooltip.AddLine(GetTooltipString("TALENT_TOOLTIP_ADDPREVIEWPOINT"), 0.1f, 1.0f, 0.1f);
    }
    if (store
            .ResolvePreviewPointDelta(active_group_index, lookup.tab_index, lookup.talent_index, -1,
                                      is_pet)
            .has_value()) {
      tooltip.AddLine(GetTooltipString("TALENT_TOOLTIP_REMOVEPREVIEWPOINT"), 1.0f, 0.1f, 0.1f);
    }
    return;
  }

  if (requirements_met && store.GetUnspentPointsForGroup(active_group_index, is_pet) > 0 &&
      current_points < static_cast<std::int32_t>(max_rank)) {
    tooltip.AddLine(GetTooltipString("TOOLTIP_TALENT_LEARN"), 0.1f, 1.0f, 0.1f);
  }
}

}

bool BuildSpellTooltip(const SpellTooltipRequest &request) {
  auto &tooltip_system = request.tooltip;
  const auto spellId = static_cast<int>(request.spell_id);
  const auto cooldownRemaining = static_cast<int>(request.cooldown_remaining.count());
  const bool showSimple = request.simple;
  const bool showRank = request.show_rank;
  const bool isInspect = request.inspect;
  const bool appendMode = request.append;
  const bool talentData = request.talent_context;
  const int curRank = request.current_rank;
  const int maxRank = request.max_rank;
  const bool isFinal = request.final;

  if (!appendMode) {
    tooltip_system.ClearLines();
  }

  const auto *session = ResolveTooltipWorldSession();
  if (session == nullptr) {
    return 0;
  }
  const auto *objects = tooltip_system.GetObjectManager();
  if (objects == nullptr) {
    objects = &session->objects();
  }
  const auto *active_player = objects->GetActivePlayer();
  if (active_player == nullptr) {
    return 0;
  }

  const auto spell_query =
      openwow::game::SpellQueryBridge::Get().Query(static_cast<std::uint32_t>(spellId));
  if (!spell_query.has_value()) {
    return 0;
  }
  const auto &spell = spell_query.value();

  if (!appendMode) {
    tooltip_system.SetSpellId(static_cast<std::uint32_t>(spellId));
  }

  bool isChanneledItem = false;
  if ((spell.attributes & 0x20) != 0) {
    for (std::size_t i = 0; i < spell.effectIds.size(); ++i) {
      if (spell.effectIds[i] == 53) {
        isChanneledItem = true;
        break;
      }
      if (spell.effectIds[i] == 24 || spell.effectIds[i] == 157 || spell.effectIds[i] == 59) {
        isChanneledItem = true;
      }
    }
  }

  if (talentData && appendMode) {
    tooltip_system.AddLine(FormatTooltipLine("TOOLTIP_TALENT_NEXT_RANK"), kTooltipGoldR,
                           kTooltipGoldG, kTooltipGoldB);
  } else if (isChanneledItem) {
    tooltip_system.AddLine(spell.name, kTooltipGoldR, kTooltipGoldG, kTooltipGoldB);
  } else {
    if ((showSimple || showRank) && !spell.subtext.empty()) {
      tooltip_system.AddDoubleLine(spell.name, spell.subtext);
    } else {
      tooltip_system.AddLine(spell.name);
    }
  }

  if (talentData && !appendMode) {
    if (maxRank >= 0) {
      tooltip_system.AddLine(FormatTooltipLine("TOOLTIP_TALENT_RANK", curRank + 1, maxRank + 1));
    }
  }

  if (!showSimple) {
    const auto power_type = spell.powerType;
    const std::uint32_t cost = spell.manaCost;

    std::string cost_text;
    if (power_type == openwow::game::PowerType::kRune && spell.hasRuneCost) {
      if (spell.runeCost.blood > 0)
        cost_text += FormatTooltipLine("RUNE_COST_BLOOD", spell.runeCost.blood);
      if (spell.runeCost.unholy > 0) {
        if (!cost_text.empty())
          cost_text += " ";
        cost_text += FormatTooltipLine("RUNE_COST_UNHOLY", spell.runeCost.unholy);
      }
      if (spell.runeCost.frost > 0) {
        if (!cost_text.empty())
          cost_text += " ";
        cost_text += FormatTooltipLine("RUNE_COST_FROST", spell.runeCost.frost);
      }
    } else if (cost > 0) {
      const char *cost_tokens[] = {
          "MANA_COST",
          "RAGE_COST",
          "FOCUS_COST",
          "ENERGY_COST",
          "PET_HAPPINESS",
          nullptr,
          "RUNIC_POWER_COST"
      };
      const auto pt = static_cast<std::uint8_t>(power_type);
      const char *token = (pt <= 6 && cost_tokens[pt] != nullptr) ? cost_tokens[pt] : "HEALTH_COST";
      cost_text = FormatTooltipLine(token, cost);
    }

    std::string range_text;
    if (!showSimple && (spell.attributes & 0x404) == 0 && (spell.attributesEx3 & 0x40000000) == 0) {
      const float range_val = spell.range;
      if (range_val > 0.0f) {
        if (range_val >= 50000.0f) {
          range_text = GetTooltipString("SPELL_RANGE_UNLIMITED");
        } else {
          std::array<char, 32> range_num{};
          std::snprintf(range_num.data(), range_num.size(), "%d", static_cast<int>(range_val));
          range_text = FormatTooltipLine("SPELL_RANGE", range_num.data());
        }
      } else if (spell.range == 0.0f && (spell.attributes & 0x404) == 0) {
        range_text = GetTooltipString("MELEE_RANGE");
      }
    }

    if (!cost_text.empty() || !range_text.empty()) {
      if (!cost_text.empty() && !range_text.empty()) {
        tooltip_system.AddDoubleLine(cost_text, range_text);
      } else if (!cost_text.empty()) {
        tooltip_system.AddLine(cost_text);
      } else {
        tooltip_system.AddLine(range_text);
      }
    }
  }

  if (!showSimple && !isChanneledItem) {
    std::string cast_text;
    std::string cooldown_text;

    const bool isAutoAttack = (spell.effectIds[0] == 47) || ((spell.attributes & 0x40) != 0);
    const bool isCritChanceSpell = (spell.effectIds[0] == 78);

    if (isAutoAttack) {
    } else if (!isCritChanceSpell) {
      const float cast_seconds = spell.castTime;
      if (cast_seconds > 0.0f) {
        if (cast_seconds >= 60.0f) {
          cast_text = FormatTooltipLine("SPELL_CAST_TIME_MIN", cast_seconds / 60.0f);
        } else {
          cast_text = FormatTooltipLine("SPELL_CAST_TIME_SEC", cast_seconds);
        }
      } else if (spell.isChanneled) {
        cast_text = GetTooltipString("SPELL_CAST_CHANNELED");
      } else if (cast_seconds == 0.0f) {
        if ((spell.attributes & 0x404) != 0) {
          cast_text = GetTooltipString("SPELL_ON_NEXT_SWING");
        } else if ((spell.attributes & 0x2) != 0) {
          cast_text = GetTooltipString("SPELL_ON_NEXT_RANGED");
        } else if (spell.manaCost > 0 || spell.powerType != openwow::game::PowerType::kMana) {
          cast_text = GetTooltipString("SPELL_CAST_TIME_INSTANT");
        } else {
          cast_text = GetTooltipString("SPELL_CAST_TIME_INSTANT_NO_MANA");
        }
      } else {
        cast_text = GetTooltipString("SPELL_CAST_TIME_INSTANT");
      }
    }

    const float cd = spell.cooldown;
    if (cd > 0.0f) {
      if (cd >= 60.0f) {
        cooldown_text = FormatTooltipLine("SPELL_RECAST_TIME_MIN", cd / 60.0f);
      } else {
        cooldown_text = FormatTooltipLine("SPELL_RECAST_TIME_SEC", cd);
      }
    }

    if (!cast_text.empty() || !cooldown_text.empty()) {
      if (!cast_text.empty() && !cooldown_text.empty()) {
        tooltip_system.AddDoubleLine(cast_text, cooldown_text);
      } else if (!cast_text.empty()) {
        tooltip_system.AddLine(cast_text);
      } else {
        tooltip_system.AddLine(cooldown_text);
      }
    }
  }

  if (cooldownRemaining > 0) {
    std::array<char, 128> cd_buf{};

    FormatMultiUnitDurationText(cd_buf.data(), cd_buf.size(), cooldownRemaining,
                                "ITEM_COOLDOWN_TIME");
    tooltip_system.AddLine(cd_buf.data());
  }

  if (!showSimple) {
    if ((spell.attributesEx & 0x2) != 0) {
      const char *all_power_tokens[] = {
          "SPELL_USE_ALL_MANA",
          "SPELL_USE_ALL_RAGE",
          "SPELL_USE_ALL_FOCUS",
          "SPELL_USE_ALL_ENERGY",
          nullptr,
          nullptr,
          "SPELL_USE_ALL_RUNIC_POWER"
      };
      const auto pt = static_cast<std::uint8_t>(spell.powerType);
      const char *token = (pt <= 6 && all_power_tokens[pt] != nullptr) ? all_power_tokens[pt]
                                                                       : "SPELL_USE_ALL_HEALTH";
      tooltip_system.AddLine(GetTooltipString(token));
    }

    const auto description = openwow::game::ResolveSpellDescriptionForDisplay(
        static_cast<std::uint32_t>(spellId), spell.description);
    if (!description.empty()) {
      tooltip_system.AddLine(description, kTooltipGoldR, kTooltipGoldG, kTooltipGoldB, true);
    }

    if (talentData && !isInspect && isFinal) {
    }
  }

  tooltip_system.Show();
  return tooltip_system.GetNumLines() > 0;
}

void BuildSimpleSpellTooltip(TooltipSystem &tooltip_system, const std::uint32_t spellId) {
  tooltip_system.ClearLines();

  const auto spell_query =
      openwow::game::SpellQueryBridge::Get().Query(static_cast<std::uint32_t>(spellId));
  if (!spell_query.has_value()) {
    return;
  }
  const auto &spell = spell_query.value();

  if (!spell.subtext.empty()) {
    tooltip_system.AddDoubleLine(spell.name, spell.subtext, kTooltipGoldR, kTooltipGoldG,
                                 kTooltipGoldB, kTooltipGoldR, kTooltipGoldG, kTooltipGoldB);
  } else {
    tooltip_system.AddLine(spell.name, kTooltipGoldR, kTooltipGoldG, kTooltipGoldB);
  }

  const auto description = openwow::game::ResolveSpellDescriptionForDisplay(
      static_cast<std::uint32_t>(spellId), spell.description);
  if (!description.empty()) {
    tooltip_system.AddLine(description, 1.0f, 1.0f, 1.0f, true);
  }

  tooltip_system.Show();
}

void BuildTalentTooltip(const TalentTooltipRequest &request) {
  auto &tooltip_system = request.tooltip;
  const auto *talentEntry = &request.talent;
  const auto *talentGroup = request.group;
  const bool isInspect = request.inspect;
  const bool hasExplicitRank = request.explicit_rank.has_value();
  const int explicitRank = request.explicit_rank.value_or(-1);
  const bool isPet = request.is_pet;
  const bool preview = request.preview;
  tooltip_system.ClearLines();

  const auto max_rank = CountTalentRanks(*talentEntry);
  if (max_rank == 0) {
    tooltip_system.Show();
    return;
  }

  const auto current_points =
      std::clamp(ResolveTalentPointsForTooltip(*talentEntry, talentGroup, hasExplicitRank,
                                               explicitRank, false),
                 0, static_cast<int>(max_rank));
  const auto display_points =
      std::clamp(ResolveTalentPointsForTooltip(*talentEntry, talentGroup, hasExplicitRank,
                                               explicitRank, preview),
                 0, static_cast<int>(max_rank));
  const auto current_spell = ResolveTalentSpellDisplay(*talentEntry, display_points);
  const auto next_spell = ResolveTalentSpellDisplay(
      *talentEntry, display_points < static_cast<int>(max_rank) ? display_points + 1 : 0);

  if (current_spell.HasSpell()) {
    tooltip_system.AddLine(current_spell.name);
  } else if (next_spell.HasSpell()) {
    tooltip_system.AddLine(next_spell.name);
  } else {
    return;
  }

  char rank_buffer[256];
  FormatTooltipTextFromGlobalString(rank_buffer, sizeof(rank_buffer),
                                    GetTooltipString("TOOLTIP_TALENT_RANK").c_str(),
                                    display_points, max_rank);
  tooltip_system.AddLine(rank_buffer);

  const bool requirements_met = AppendTalentRequirementLines(
      tooltip_system, *talentEntry, talentGroup, isInspect, isPet, preview);
  bool added_current_description = false;
  if (current_spell.HasSpell() && !current_spell.description.empty()) {
    tooltip_system.AddLine(current_spell.description, 1.0f, 1.0f, 1.0f, true);
    added_current_description = true;
  }

  if (next_spell.HasSpell() && !next_spell.description.empty()) {
    if (added_current_description) {
      tooltip_system.AddLine(" ");
      tooltip_system.AddLine(GetTooltipString("TOOLTIP_TALENT_NEXT_RANK"));
    }
    tooltip_system.AddLine(next_spell.description, 1.0f, 1.0f, 1.0f, true);
  }

  AppendTalentActionLines(tooltip_system, *talentEntry, talentGroup, requirements_met,
                          current_points, max_rank, isInspect, hasExplicitRank, isPet, preview);
  tooltip_system.Show();
}

}
