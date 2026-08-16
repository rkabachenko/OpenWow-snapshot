
#include "openwow/net/wotlk/quests.h"

namespace openwow::net::wotlk {

QuestAcceptRequest BuildQuestAcceptRequest(std::uint32_t quest_id) {
  return {.quest_id = quest_id};
}

QuestCompleteRequest BuildQuestCompleteRequest(std::uint64_t giver_guid,
                                                std::uint32_t quest_id) {
  QuestCompleteRequest r;
  r.quest_giver_guid = giver_guid;
  r.quest_id = quest_id;
  return r;
}

QuestAbandonRequest BuildQuestAbandonRequest(std::uint32_t quest_id) {
  return {.quest_id = quest_id};
}

QuestQueryRequest BuildQuestQueryRequest(std::uint32_t quest_id) {
  return {.quest_id = quest_id};
}

QuestRewardChoiceRequest BuildQuestRewardChoiceRequest(
    std::uint64_t giver_guid,
    std::uint32_t quest_id,
    std::uint32_t reward_index) {
  QuestRewardChoiceRequest r;
  r.quest_giver_guid = giver_guid;
  r.quest_id = quest_id;
  r.reward_index = reward_index;
  return r;
}

QuestConfirmAcceptRequest BuildQuestConfirmAcceptRequest(
    std::uint32_t quest_id) {
  return {.quest_id = quest_id};
}

QuestLogRemoveRequest BuildQuestLogRemoveRequest(std::uint8_t slot) {
  return {.slot = slot};
}

QuestPushResultRequest BuildQuestPushResultRequest(std::uint64_t receiver,
                                                    std::uint8_t result) {
  QuestPushResultRequest r;
  r.receiver_guid = receiver;
  r.result = result;
  return r;
}

const char* GetQuestStatusName(QuestStatus status) {
  switch (status) {
    case QuestStatus::None:              return "None";
    case QuestStatus::Available:         return "Available";
    case QuestStatus::Unavailable:       return "Unavailable";
    case QuestStatus::Incomplete:        return "Incomplete";
    case QuestStatus::RewardRep:         return "RewardRep";
    case QuestStatus::AvailableRep:      return "AvailableRep";
    case QuestStatus::LowLevelAvailable: return "LowLevelAvailable";
    case QuestStatus::Complete:          return "Complete";
  }
  return "Unknown";
}

bool IsQuestStatusComplete(QuestStatus status) {
  return status == QuestStatus::Complete;
}

bool IsQuestStatusAvailable(QuestStatus status) {
  switch (status) {
    case QuestStatus::Available:
    case QuestStatus::AvailableRep:
    case QuestStatus::LowLevelAvailable:
      return true;
    default:
      return false;
  }
}

}
