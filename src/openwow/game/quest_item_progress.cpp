
#include "openwow/game/quest_item_progress.h"

#include <algorithm>
#include <cstdint>

#include "openwow/game/interaction_sender.h"
#include "openwow/game/localization.h"
#include "openwow/game/quest_log.h"
#include "openwow/game/quest_manager.h"
#include "openwow/game/quest_types.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/game/script_event_dispatch.h"

namespace openwow::game {

bool UpdateQuestItemObjectiveProgress(const QuestTemplate &quest,
                                      std::uint32_t item_id,
                                      std::uint32_t count_received,
                                      std::uint32_t total_count_after,
                                      WorldSession &session) {

  int matched_index = -1;
  for (int i = 0; i < kQuestItemObjectivesCount; ++i) {
    if (quest.item_objectives[i].item_id == item_id) {
      matched_index = i;
      break;
    }
  }

  if (matched_index < 0) {
    return false;

  }

  const auto *item_tmpl =
      session.query_cache().GetOrRequestItemTemplate(item_id);
  if (item_tmpl == nullptr) {
    return true;

  }

  auto *player = session.objects().GetActivePlayer();
  if (player == nullptr) {
    return true;

  }

  const std::uint32_t required =
      quest.item_objectives[matched_index].required_count;
  const std::uint32_t old_count = total_count_after - count_received;

  if (old_count >= required) {
    return true;
  }

  const std::uint32_t capped_count = std::min(total_count_after, required);

  ui::game::DisplaySystemMessage(265, item_tmpl->name.c_str(),
                                 capped_count, required);

  const int quest_log_index =
      QuestLog::Get().GetQuestLogIndexById(quest.quest_id);

  auto &dispatch = ui::game::ScriptEventDispatch::Get();
  dispatch.FireEventArgs(ui::game::events::QUEST_LOG_UPDATE,
                         {quest_log_index});

  if (HasFlag(quest.flags, QuestFlags::kAutocomplete)) {
    auto &quest_log = QuestLog::Get();
    const int visible_idx = quest_log.GetQuestLogIndexById(quest.quest_id);
    if (visible_idx >= 0 &&
        quest_log.IsQuestCompleteDetailed(quest.quest_id)) {

      const std::uint64_t player_guid = player->GetGuid().GetRawValue();
      session.interaction().SendQuestGiverCompleteQuest(
          player_guid, quest.quest_id);
    }
  }

  return true;

}

void Player_CheckQuestItemObjectiveProgress(std::uint32_t item_id,
                                             std::uint32_t count_received,
                                             std::uint32_t total_count_after,
                                             WorldSession &session) {
  auto &quest_log = QuestLog::Get();
  auto &quest_mgr = session.quests();

  const std::size_t num_quests = quest_log.GetNumQuests();

  for (std::size_t slot = 0; slot < num_quests && slot < QuestLog::kMaxQuests;
       ++slot) {
    const auto *entry = quest_log.GetQuest(slot);
    if (entry == nullptr || entry->quest_id == 0) {
      continue;
    }

    const std::uint32_t quest_id = entry->quest_id;

    if (entry->is_failed) {
      continue;
    }

    const auto *tmpl = quest_mgr.GetOrRequestTemplate(quest_id);
    if (tmpl != nullptr) {

      if (UpdateQuestItemObjectiveProgress(*tmpl, item_id, count_received,
                                           total_count_after, session)) {
        return;
      }
      continue;
    }

    (void)quest_mgr.GetOrRequestTemplate(
        quest_id,
        AsyncQueryChannel::RequestOptions{
            .callback = [item_id, count_received, total_count_after, quest_id,
                         &session](bool success) {
              if (!success) {
                return;
              }

              const auto *resolved_tmpl =
                  session.quests().GetOrRequestTemplate(quest_id);
              if (resolved_tmpl != nullptr) {
                UpdateQuestItemObjectiveProgress(
                    *resolved_tmpl, item_id, count_received,
                    total_count_after, session);
              }

            }});
  }
}

}
