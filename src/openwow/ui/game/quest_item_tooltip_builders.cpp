#include "openwow/ui/game/tooltip_builders.h"
#include "openwow/ui/game/tooltip_runtime.h"
#include "openwow/ui/game/tooltip_internal.h"

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

void SetTooltipFromQuestSpecialItem(openwow::game::WorldSession &session,
                                    const openwow::game::ItemInstance &item) {
  auto &tooltip = openwow::ui::game::TooltipSystem::Get();
  tooltip.SetItemFromLoot(item.entry, item.random_property, item.random_suffix, 0, item.guid);

  const auto display_name =
    openwow::ui::game::detail::ResolveQuestSpecialItemDisplayName(
      openwow::ui::game::TooltipSystem::Get().GetDbcLoader(), session, item);
  if (display_name.empty()) {
    return;
  }

  tooltip.OverrideItemIdentity(
      display_name,
      openwow::ui::game::detail::BuildQuestSpecialItemLink(
        openwow::ui::game::TooltipSystem::Get().GetDbcLoader(), session, item));
}

struct QuestRequirementLineBuilderState {
  int objective_index = -1;
  bool include_progress = true;
  openwow::game::AsyncQueryChannel::Callback on_quest_template_query;
  openwow::game::AsyncQueryChannel::Callback on_gameobject_template_query;
  openwow::game::AsyncQueryChannel::Callback on_creature_template_query;
  openwow::game::AsyncQueryChannel::Callback on_item_template_query;
};

QuestRequirementLineBuilderState MakeQuestRequirementLineBuilderState() {
  QuestRequirementLineBuilderState state;
  state.on_quest_template_query =
      openwow::ui::game::BuildQuestRequirementQueryCallback("Invalid quest log entry");
  state.on_gameobject_template_query =
      openwow::ui::game::BuildQuestRequirementQueryCallback("Invalid object in quest");
  state.on_creature_template_query =
      openwow::ui::game::BuildQuestRequirementQueryCallback("Invalid creature in quest");
  state.on_item_template_query = openwow::ui::game::BuildQuestRequirementQueryCallback("");
  return state;
}

bool BuildQuestTooltipRequirementLine(openwow::game::WorldSession &session,
                                      const std::uint32_t quest_id,
                                      const QuestRequirementLineBuilderState &state,
                                      openwow::ui::game::QuestLeaderboardLine *line) {
  if (line == nullptr) {
    return false;
  }

  openwow::ui::game::QuestRequirementQueryCallbacks callbacks;
  callbacks.on_quest_template_query = state.on_quest_template_query;
  callbacks.on_gameobject_template_query = state.on_gameobject_template_query;
  callbacks.on_creature_template_query = state.on_creature_template_query;
  callbacks.on_item_template_query = state.on_item_template_query;
  return openwow::ui::game::BuildQuestLeaderboardLine(session, quest_id, state.objective_index,
                                                      state.include_progress, callbacks, line);
}

bool BuildQuestTooltipFromSession(openwow::game::WorldSession *session,
                                  const std::uint32_t quest_id) {
  auto &tooltip = openwow::ui::game::TooltipSystem::Get();
  tooltip.ClearLines();

  if (session == nullptr || quest_id == 0) {
    return false;
  }

  auto requirement_state = MakeQuestRequirementLineBuilderState();
  requirement_state.include_progress = false;
  openwow::ui::game::QuestRequirementQueryCallbacks callbacks;
  callbacks.on_quest_template_query = requirement_state.on_quest_template_query;
  callbacks.on_gameobject_template_query = requirement_state.on_gameobject_template_query;
  callbacks.on_creature_template_query = requirement_state.on_creature_template_query;
  callbacks.on_item_template_query = requirement_state.on_item_template_query;

  const auto *quest_template = session->quests().GetOrRequestTemplate(
      quest_id, {.dedupe_callbacks = false, .callback = requirement_state.on_quest_template_query});
  if (quest_template == nullptr) {
    return false;
  }

  tooltip.AddLine(quest_template->title);

  if (session->quests().FindQuestLogEntry(quest_id) != nullptr) {
    tooltip.AddLine(GetTooltipString("QUEST_TOOLTIP_ACTIVE"), 0.1f, 1.0f, 0.1f);
  }

  tooltip.AddLine(" ");
  tooltip.AddLine(openwow::game::ExpandQuestDialogText(*session, quest_template->objectives, false),
                  1.0f, 1.0f, 1.0f, true);

  const auto requirement_count =
      openwow::ui::game::CountQuestLeaderboardObjectives(*session, quest_id, callbacks);
  if (requirement_count > 0) {
    tooltip.AddLine(" ");
    tooltip.AddLine(GetTooltipString("QUEST_TOOLTIP_REQUIREMENTS"));

    for (int ordinal = 0; ordinal < requirement_count; ++ordinal) {
      requirement_state.objective_index = ordinal;
      openwow::ui::game::QuestLeaderboardLine line;
      if (!BuildQuestTooltipRequirementLine(*session, quest_id, requirement_state, &line)) {
        continue;
      }
      tooltip.AddLine(" - " + line.text);
    }
  }

  tooltip.Show();
  return true;
}

}

bool BuildQuestTooltip(TooltipSystem& tooltip_system, const std::uint32_t questId) {
  tooltip_system.ClearLines();

  tooltip_system.SetQuestId(static_cast<std::uint32_t>(questId));

  const auto quest =
      openwow::game::QuestQueryBridge::Get().Query(static_cast<std::uint32_t>(questId));
  if (!quest.has_value()) {
    return false;
  }

  tooltip_system.AddLine(quest->title);

  if (openwow::game::QuestQueryBridge::Get().IsQuestTracked(
          static_cast<std::uint32_t>(questId))) {
    tooltip_system.AddLine(GetTooltipString("QUEST_TOOLTIP_ACTIVE"), 0.1f, 1.0f, 0.1f);
  }

  tooltip_system.AddLine(" ");

  const std::string expanded_objectives =
      openwow::game::ExpandQuestDialogText(quest->objectives, false);
  tooltip_system.AddLine(expanded_objectives, 1.0f, 1.0f, 1.0f, true);

  const auto &progress = quest->objectiveProgress;
  if (!progress.empty()) {
    tooltip_system.AddLine(" ");
    tooltip_system.AddLine(GetTooltipString("QUEST_TOOLTIP_REQUIREMENTS"));

    for (const auto &[text, counts] : progress) {
      const auto &[current, required] = counts;
      std::string line_text;
      if (required > 0) {
        line_text = text + ": " + std::to_string(required);
      } else {
        line_text = text;
      }
      tooltip_system.AddLine(" - " + line_text);
    }
  }

  tooltip_system.Show();
  return true;
}

void BuildGlyphTooltip(TooltipSystem& ts, const std::uint32_t slotId,
                       const std::uint32_t glyphId, const bool isEnabled,
                       const bool canRemove) {
  ts.ClearLines();

  const auto *dbc = ts.GetDbcLoader();
  const auto *slot_entry = dbc ? dbc->glyph_slot().LookupEntry(slotId) : nullptr;
  const auto *glyph_entry = dbc ? dbc->glyph_properties().LookupEntry(glyphId) : nullptr;
  const auto *spell_entry =
      (dbc && glyph_entry) ? dbc->spell().LookupEntry(glyph_entry->spell_id) : nullptr;
  const bool has_active_glyph = spell_entry != nullptr && !spell_entry->spell_name.empty();

  if (has_active_glyph) {
    ts.AddLine(std::string(spell_entry->spell_name));
  } else if (isEnabled) {
    ts.AddLine(GetTooltipString("GLYPH_INACTIVE"), 0.8f, 0.2f, 0.2f);
  } else {
    ts.AddLine(GetTooltipString("GLYPH_LOCKED"), 1.0f, 0.1f, 0.1f);
  }

  if (slot_entry && slot_entry->type != 0) {
    ts.AddLine(GetTooltipString("MINOR_GLYPH"), 1.0f, 0.82f, 0.0f);
  } else {
    ts.AddLine(GetTooltipString("MAJOR_GLYPH"), 1.0f, 0.82f, 0.0f);
  }

  if (has_active_glyph) {
    const auto description =
        openwow::game::ResolveSpellDescriptionForDisplay(
            glyph_entry->spell_id, spell_entry->description);
    if (!description.empty()) {
      ts.AddLine(description, 1.0f, 1.0f, 1.0f, true);
    }
  } else {
    if (isEnabled) {
      ts.AddLine(GetTooltipString("GLYPH_EMPTY_DESC"), 1.0f, 1.0f, 1.0f, true);
    } else if (slot_entry) {
      const std::string key = "GLYPH_SLOT_TOOLTIP" + std::to_string(slot_entry->order);
      ts.AddLine(GetTooltipString(key.c_str()), 1.0f, 1.0f, 1.0f, true);
    }
  }
  if (has_active_glyph && !canRemove) {
    ts.AddLine(GetTooltipString("GLYPH_SLOT_REMOVE_TOOLTIP"), 0.8f, 0.2f, 0.2f);
  }

  ts.Show();
}

int BuildEquipmentSetTooltip(TooltipSystem& tooltip_system, const std::uint32_t setId) {
  tooltip_system.ClearLines();

  const auto* session = ResolveTooltipWorldSession();
  if (session == nullptr) {
    return 0;
  }
  const auto* set =
      session->equipment().find(setId);
  if (set == nullptr) {
    return 0;
  }

  const bool bank_open = session->bank_npc_guid() != 0;
  const auto& inventory = session->inventory_replica();

  std::uint32_t equipped_count = 0;
  std::uint32_t inventory_count = 0;
  std::uint32_t ignored_count = 0;
  std::vector<std::uint32_t> missing_slots;
  missing_slots.reserve(openwow::game::kEquipmentSlotCount);

  for (std::uint32_t slot_index = 0; slot_index < openwow::game::kEquipmentSlotCount;
       ++slot_index) {
    const auto& item = set->items[slot_index];
    if (set->ignored.test(slot_index)) {
      ++ignored_count;
      continue;
    }
    if (!item.has_value()) {
      continue;
    }

    const auto item_guid = item->GetRawValue();

    const auto absolute_slot = inventory.FindSlotByGuid(item_guid);
    if (absolute_slot >= 0 &&
        absolute_slot < static_cast<int>(openwow::game::InventorySlots::kEquipEnd)) {
      ++equipped_count;
      continue;
    }

    if (InventoryContainsEquipmentSetItem(inventory, item_guid, bank_open)) {
      ++inventory_count;
      continue;
    }

    missing_slots.push_back(slot_index);
  }

  const auto total_count =
      equipped_count + inventory_count + static_cast<std::uint32_t>(missing_slots.size());
  const auto total_line = FormatTooltipLine("ITEMS_VARIABLE_QUANTITY", total_count);
  if (!total_line.empty()) {
    tooltip_system.AddDoubleLine(set->name, total_line, 1.0f, 1.0f, 1.0f, kTooltipGoldR,
                                 kTooltipGoldG, kTooltipGoldB);
  } else {
    tooltip_system.AddLine(set->name);
  }

  if (equipped_count != 0) {
    tooltip_system.AddLine(FormatTooltipLine("ITEMS_EQUIPPED", equipped_count), kTooltipGoldR,
                           kTooltipGoldG, kTooltipGoldB);
  }

  if (inventory_count != 0) {
    tooltip_system.AddLine(FormatTooltipLine("ITEMS_IN_INVENTORY", inventory_count));
  }

  if (ignored_count != 0) {
    tooltip_system.AddLine(FormatTooltipLine("ITEM_SLOTS_IGNORED", ignored_count), kTooltipGrayR,
                           kTooltipGrayG, kTooltipGrayB);
  }

  for (auto it = missing_slots.rbegin(); it != missing_slots.rend(); ++it) {
    const char* slot_key = openwow::game::GetShapeshiftSlotName(static_cast<int>(*it));
    tooltip_system.AddLine(
        FormatTooltipLine("ITEM_MISSING", GetTooltipString(slot_key != nullptr ? slot_key : "")
                                              .c_str()),
        kTooltipMissingR, kTooltipMissingG, kTooltipMissingB);
  }

  tooltip_system.Show();
  return 0;
}

}
