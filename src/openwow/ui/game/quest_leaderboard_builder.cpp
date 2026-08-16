#include "openwow/ui/game/quest_leaderboard_builder.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/debug/diagnostics/debug_console.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/localization.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/quest_dialog_text.h"
#include "openwow/game/quest_manager.h"
#include "openwow/game/reputation_info.h"
#include "openwow/game/template_name_variant.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/script_event_dispatch.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace openwow::ui::game {
namespace {

const openwow::game::QuestTemplate *GetOrRequestQuestTemplate(
    openwow::game::WorldSession &session, const std::uint32_t quest_id,
    const QuestRequirementQueryCallbacks &callbacks) {
  return session.quests().GetOrRequestTemplate(
      quest_id, {.dedupe_callbacks = false, .callback = callbacks.on_quest_template_query});
}

bool IsQuestComplete(const openwow::game::WorldSession &session, const std::uint32_t quest_id) {
  const auto *quest_entry = session.quests().FindQuestLogEntry(quest_id);
  return quest_entry != nullptr &&
         (quest_entry->status == openwow::game::QuestStatus::kComplete ||
          quest_entry->status == openwow::game::QuestStatus::kRewarded);
}

std::optional<openwow::game::CGPlayer_C::QuestLogEntry>
FindLocalPlayerQuestLogSlot(const openwow::game::WorldSession &session,
                            const std::uint32_t quest_id) {
  const auto *player = session.objects().GetLocalPlayerTyped();
  if (player == nullptr || quest_id == 0) {
    return std::nullopt;
  }

  for (std::uint8_t slot = 0; slot < openwow::game::kMaxQuestLogEntries; ++slot) {
    const auto entry = player->GetQuestLog(slot);
    if (entry.quest_id == quest_id) {
      return entry;
    }
  }

  return std::nullopt;
}

bool IsVisibleItemObjective(const openwow::game::QuestTemplate &quest_template, const int slot) {
  const auto &objective = quest_template.item_objectives[slot];
  if (objective.item_id == 0 || objective.required_count == 0) {
    return false;
  }

  return objective.item_id != quest_template.src_item_id || objective.required_count > 1;
}

template <std::size_t N>
std::string ResolvePluralizedName(const std::string &base_name,
                                  const std::array<std::string, N> &alternate_names,
                                  const std::uint32_t count) {
  return std::string(
      openwow::game::GetQuestObjectiveNameVariantOrBase(base_name, alternate_names, count));
}

std::string ResolveCreatureObjectiveName(openwow::game::WorldSession &session,
                                         const std::int32_t creature_entry,
                                         const std::uint32_t required_count,
                                         const QuestRequirementQueryCallbacks &callbacks) {
  if (creature_entry <= 0) {
    return " ";
  }

  const auto *template_info = session.query_cache().GetOrRequestCreatureTemplate(
      static_cast<std::uint32_t>(creature_entry),
      {.dedupe_callbacks = false, .callback = callbacks.on_creature_template_query});
  if (template_info == nullptr) {
    return " ";
  }

  return ResolvePluralizedName(template_info->name, template_info->alternate_names,
                               required_count);
}

std::string ResolveGameObjectObjectiveName(openwow::game::WorldSession &session,
                                           const std::int32_t object_entry,
                                           const std::uint32_t required_count,
                                           const QuestRequirementQueryCallbacks &callbacks) {
  const auto raw_entry = static_cast<std::uint32_t>(-object_entry);
  if (raw_entry == 0) {
    return " ";
  }

  const auto *template_info = session.query_cache().GetOrRequestGameObjectTemplate(
      raw_entry, {.dedupe_callbacks = false, .callback = callbacks.on_gameobject_template_query});
  if (template_info == nullptr) {
    return " ";
  }

  return ResolvePluralizedName(template_info->name, template_info->alternate_names,
                               required_count);
}

std::string ResolveItemObjectiveName(openwow::game::WorldSession &session,
                                     const std::uint32_t item_id,
                                     const std::uint32_t ,
                                     const QuestRequirementQueryCallbacks &callbacks) {
  if (item_id == 0) {
    return " ";
  }

  if (const auto *query_item = session.query_cache().GetOrRequestItemTemplate(
          item_id, {.dedupe_callbacks = false, .callback = callbacks.on_item_template_query});
      query_item != nullptr) {
    return query_item->name.empty() ? " " : query_item->name;
  }
  return " ";
}

std::string FormatQuestLeaderboardText(const std::string &global_string_key,
                                       std::vector<std::string> args) {
  auto &localization = openwow::game::Localization::Get();
  return localization.FormatString(localization.GetString(global_string_key, global_string_key),
                                   args);
}

int ResolveRequiredStandingLevel(const std::int32_t required_value) {
  int required_level = 0;
  for (int standing = static_cast<int>(openwow::game::kStandingMin.size()) - 1;
       standing >= 0;
       --standing) {
    if (required_value >=
        openwow::game::kStandingMin[static_cast<std::size_t>(standing)]) {
      required_level = standing;
      break;
    }
  }

  return required_level;
}

std::string GetLocalizedStandingLabel(const int standing_level) {
  auto &localization = openwow::game::Localization::Get();
  const auto key = "FACTION_STANDING_LABEL" + std::to_string(standing_level + 1);
  return localization.GetString(key, key);
}

}

openwow::game::AsyncQueryChannel::Callback
BuildQuestRequirementQueryCallback(std::string failure_message) {
  return [failure_message = std::move(failure_message)](const bool success) {
    if (success) {
      ScriptEventDispatch::Get().FireQuestQueryComplete();
      return;
    }

    if (!failure_message.empty()) {
      openwow::debug::DebugConsole::Get().Write(failure_message);
    }
  };
}

int CountQuestLeaderboardObjectives(openwow::game::WorldSession &session,
                                    const std::uint32_t quest_id,
                                    const QuestRequirementQueryCallbacks &callbacks) {
  const auto *quest_template = GetOrRequestQuestTemplate(session, quest_id, callbacks);
  if (quest_template == nullptr) {
    return 0;
  }

  int count = quest_template->area_description.empty() ? 0 : 1;

  for (const auto &objective : quest_template->npc_or_go_objectives) {
    if (objective.creature_or_go != 0) {
      ++count;
    }
  }

  for (int item_slot = 0; item_slot < openwow::game::kQuestItemObjectivesCount; ++item_slot) {
    if (IsVisibleItemObjective(*quest_template, item_slot)) {
      ++count;
    }
  }

  if (quest_template->required_reputation_faction != 0) {
    ++count;
  }
  if (quest_template->required_player_kills != 0) {
    ++count;
  }

  if (count != 0) {
    return count;
  }

  return IsQuestComplete(session, quest_id) ? 1 : 0;
}

bool BuildQuestLeaderboardLine(openwow::game::WorldSession &session, const std::uint32_t quest_id,
                               int objective_index, const bool include_progress,
                               const QuestRequirementQueryCallbacks &callbacks,
                               QuestLeaderboardLine *out) {
  if (out == nullptr || objective_index < 0) {
    return false;
  }

  *out = {};

  const auto *quest_template = GetOrRequestQuestTemplate(session, quest_id, callbacks);
  const auto player_slot = FindLocalPlayerQuestLogSlot(session, quest_id);
  if (quest_template == nullptr || !player_slot.has_value()) {
    return false;
  }

  if (!quest_template->area_description.empty()) {
    if (objective_index == 0) {
      out->text = quest_template->area_description;
      out->type = "event";
      out->finished = (player_slot->state & 0x1u) != 0;
      return true;
    }
    --objective_index;
  }

  for (int objective_slot = 0; objective_slot < openwow::game::kQuestObjectivesCount;
       ++objective_slot) {
    const auto &objective = quest_template->npc_or_go_objectives[objective_slot];
    if (objective.creature_or_go == 0) {
      continue;
    }

    if (objective_index-- != 0) {
      continue;
    }

    const std::uint32_t progress = player_slot->counts[objective_slot];
    const bool has_explicit_text = !objective.text.empty();
    std::string label = objective.text;
    if (label.empty()) {
      label = objective.creature_or_go >= 0
                  ? ResolveCreatureObjectiveName(session, objective.creature_or_go,
                                                 objective.required_count, callbacks)
                  : ResolveGameObjectObjectiveName(session, objective.creature_or_go,
                                                   objective.required_count, callbacks);
    }

    const std::string format_key =
        has_explicit_text || objective.creature_or_go < 0
            ? (include_progress ? "QUEST_OBJECTS_FOUND" : "QUEST_OBJECTS_FOUND_NOPROGRESS")
            : (include_progress ? "QUEST_MONSTERS_KILLED"
                                : "QUEST_MONSTERS_KILLED_NOPROGRESS");
    out->text = include_progress
                    ? FormatQuestLeaderboardText(format_key, {label, std::to_string(progress),
                                                              std::to_string(objective.required_count)})
                    : FormatQuestLeaderboardText(format_key,
                                                 {label, std::to_string(objective.required_count)});
    out->type = objective.creature_or_go >= 0 ? "monster" : "object";
    out->finished = progress >= objective.required_count;
    return true;
  }

  for (int item_slot = 0; item_slot < openwow::game::kQuestItemObjectivesCount; ++item_slot) {
    if (!IsVisibleItemObjective(*quest_template, item_slot)) {
      continue;
    }

    if (objective_index-- != 0) {
      continue;
    }

    const auto &objective = quest_template->item_objectives[item_slot];
    const std::uint32_t item_count =
        session.inventory_replica().GetItemCount(objective.item_id);
    const std::uint32_t progress = std::min(item_count, objective.required_count);
    const auto item_name =
        ResolveItemObjectiveName(session, objective.item_id, objective.required_count, callbacks);

    out->text = include_progress
                    ? FormatQuestLeaderboardText("QUEST_ITEMS_NEEDED",
                                                 {item_name, std::to_string(progress),
                                                  std::to_string(objective.required_count)})
                    : FormatQuestLeaderboardText("QUEST_ITEMS_NEEDED_NOPROGRESS",
                                                 {item_name,
                                                  std::to_string(objective.required_count)});
    out->type = "item";
    out->finished = progress >= objective.required_count;
    return true;
  }

  const bool quest_complete = IsQuestComplete(session, quest_id);
  if (!quest_complete) {
    for (int item_slot = 0; item_slot < openwow::game::kQuestRewardsCount; ++item_slot) {
      const auto &objective = quest_template->item_drop_objectives[item_slot];
      if (objective.item_id == 0) {
        continue;
      }

      if (objective_index-- != 0) {
        continue;
      }

      const std::uint32_t progress =
          session.inventory_replica().GetItemCount(objective.item_id);
      const auto item_name = ResolveItemObjectiveName(session, objective.item_id, 1, callbacks);

      out->text = FormatQuestLeaderboardText("QUEST_INTERMEDIATE_ITEMS_NEEDED",
                                             {item_name, std::to_string(progress)});
      out->type = "item";
      out->finished = progress >= 1;
      return true;
    }
  }

  if (quest_template->required_player_kills != 0) {
    const std::uint32_t progress = player_slot->counts[0];
    out->text = include_progress
                    ? FormatQuestLeaderboardText("QUEST_PLAYERS_KILLED",
                                                 {std::to_string(progress),
                                                  std::to_string(quest_template->required_player_kills)})
                    : FormatQuestLeaderboardText("QUEST_PLAYERS_KILLED_NOPROGRESS",
                                                 {std::to_string(quest_template->required_player_kills)});
    out->type = "player";
    out->finished = progress >= quest_template->required_player_kills;
    return true;
  }

  if (quest_template->required_reputation_faction != 0) {
    const auto *faction_entry =
        session.GetDbcLoader() != nullptr
            ? session.GetDbcLoader()->faction().LookupEntry(
                  quest_template->required_reputation_faction)
            : nullptr;
    const std::string faction_name =
        faction_entry != nullptr && !faction_entry->name.empty()
            ? std::string(faction_entry->name)
            : " ";
    const auto current_value = openwow::game::ReputationInfo::Get().GetCurrentStanding(
        static_cast<std::int32_t>(quest_template->required_reputation_faction));
    const auto current_level = openwow::game::ReputationInfo::Get().GetStandingLevel(
        static_cast<std::int32_t>(quest_template->required_reputation_faction));
    const auto required_level =
        ResolveRequiredStandingLevel(quest_template->required_reputation_value);

    out->text = include_progress
                    ? FormatQuestLeaderboardText("QUEST_FACTION_NEEDED",
                                                 {faction_name,
                                                  GetLocalizedStandingLabel(current_level),
                                                  GetLocalizedStandingLabel(required_level)})
                    : FormatQuestLeaderboardText("QUEST_FACTION_NEEDED_NOPROGRESS",
                                                 {faction_name,
                                                  GetLocalizedStandingLabel(required_level)});
    out->type = "reputation";
    out->finished = current_value >= quest_template->required_reputation_value;
    return true;
  }

  if (quest_complete) {
    out->text = openwow::game::ExpandQuestDialogText(session, quest_template->objectives, false);
    out->type = "log";
    out->finished = true;
    return true;
  }

  return false;
}

}
