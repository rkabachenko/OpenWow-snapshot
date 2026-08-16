#include "openwow/game/quest_turnin_state.h"

#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/quest_types.h"
#include "openwow/game/reputation_info.h"
#include "openwow/game/world_session.h"

#include <optional>

namespace openwow::game {

std::int32_t DecodeQuestMoneyRequirement(const std::uint32_t raw_reward_money) {
  const auto signed_money = static_cast<std::int32_t>(raw_reward_money);
  if (signed_money >= 0) {
    return 0;
  }

  return static_cast<std::int32_t>(0u - raw_reward_money);
}

bool IsQuestTurnInReady(const WorldSession &session, const std::uint32_t quest_id) {
  if (quest_id == 0) {
    return false;
  }

  const auto *player = session.objects().GetLocalPlayerTyped();
  if (player == nullptr) {
    return false;
  }

  const auto *quest_log_entry = session.quests().FindQuestLogEntry(quest_id);
  if (quest_log_entry == nullptr) {
    return false;
  }

  std::optional<CGPlayer_C::QuestLogEntry> player_slot;
  for (std::uint8_t slot = 0; slot < kMaxQuestLogEntries; ++slot) {
    const auto candidate = player->GetQuestLog(slot);
    if (candidate.quest_id == quest_id) {
      player_slot = candidate;
      break;
    }
  }

  if (!player_slot.has_value()) {
    return false;
  }

  if (quest_log_entry->status == QuestStatus::kComplete ||
      quest_log_entry->status == QuestStatus::kRewarded || (player_slot->state & 0x10000u) != 0) {
    return true;
  }

  if (quest_log_entry->status == QuestStatus::kFailed || (player_slot->state & 0x02u) != 0) {
    return false;
  }

  const auto *quest_template = session.quests().GetTemplate(quest_id);
  if (quest_template == nullptr) {
    return false;
  }

  const auto required_money = DecodeQuestMoneyRequirement(quest_template->reward_money);
  if (required_money > 0 && player->GetMoney() < static_cast<std::uint32_t>(required_money)) {
    return false;
  }

  const auto &reputation = ReputationInfo::Get();
  if (quest_template->required_reputation_faction != 0 &&
      reputation.GetCurrentStanding(
          static_cast<std::int32_t>(quest_template->required_reputation_faction)) <
          quest_template->required_reputation_value) {
    return false;
  }

  if (quest_template->required_reputation_faction_max != 0 &&
      reputation.GetCurrentStanding(
          static_cast<std::int32_t>(quest_template->required_reputation_faction_max)) >
          quest_template->required_reputation_value_max) {
    return false;
  }

  bool has_explicit_requirements = false;
  for (int objective_index = 0; objective_index < kQuestObjectivesCount; ++objective_index) {
    const auto &objective = quest_template->npc_or_go_objectives[objective_index];
    if (objective.creature_or_go == 0) {
      continue;
    }

    has_explicit_requirements = true;
    if (quest_log_entry->kill_counts[objective_index] < objective.required_count) {
      return false;
    }
  }

  for (const auto &item_objective : quest_template->item_objectives) {
    if (item_objective.item_id == 0) {
      continue;
    }

    has_explicit_requirements = true;
    if (session.inventory_replica().GetItemCount(item_objective.item_id) <
        item_objective.required_count) {
      return false;
    }
  }

  if (has_explicit_requirements) {
    return true;
  }

  if (HasFlag(quest_template->flags, QuestFlags::kPartyAccept) ||
      HasFlag(quest_template->flags, QuestFlags::kExploration) ||
      quest_template->required_reputation_faction != 0 ||
      quest_template->required_reputation_faction_max != 0 || required_money > 0) {
    return false;
  }

  for (const auto &objective : quest_template->npc_or_go_objectives) {
    if (objective.creature_or_go == 0) {
      continue;
    }

    if (quest_template->src_item_id == 0 ||
        objective.creature_or_go != static_cast<std::int32_t>(quest_template->src_item_id) ||
        objective.required_count > 1) {
      return false;
    }
  }

  return true;
}

}
