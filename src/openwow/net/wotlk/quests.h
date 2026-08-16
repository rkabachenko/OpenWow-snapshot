#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace openwow::net::wotlk {

enum class QuestStatus : std::uint8_t {
  None              = 0,
  Available         = 1,
  Unavailable       = 2,
  Incomplete        = 3,
  RewardRep         = 4,
  AvailableRep      = 5,
  LowLevelAvailable = 6,
  Complete          = 7,
};

struct QuestAcceptRequest {
  std::uint32_t quest_id{0};
};

struct QuestCompleteRequest {
  std::uint64_t quest_giver_guid{0};
  std::uint32_t quest_id{0};
};

struct QuestAbandonRequest {
  std::uint32_t quest_id{0};
};

struct QuestQueryRequest {
  std::uint32_t quest_id{0};
};

struct QuestRewardChoiceRequest {
  std::uint64_t quest_giver_guid{0};
  std::uint32_t quest_id{0};
  std::uint32_t reward_index{0};
};

struct QuestConfirmAcceptRequest {
  std::uint32_t quest_id{0};
};

struct QuestLogRemoveRequest {
  std::uint8_t slot{0};
};

struct QuestPushResultRequest {
  std::uint64_t receiver_guid{0};
  std::uint8_t result{0};
};

struct QuestLogEntry {
  std::uint32_t quest_id{0};
  std::string   title;
  QuestStatus   status{QuestStatus::None};
  std::uint8_t  slot{0};
  std::uint32_t zone_id{0};
  std::uint32_t required_level{0};
  bool          is_complete{false};
  bool          is_failed{false};
  bool          is_timed{false};
  std::uint32_t time_remaining{0};
};

QuestAcceptRequest          BuildQuestAcceptRequest(std::uint32_t quest_id);
QuestCompleteRequest        BuildQuestCompleteRequest(std::uint64_t giver_guid,
                                                      std::uint32_t quest_id);
QuestAbandonRequest         BuildQuestAbandonRequest(std::uint32_t quest_id);
QuestQueryRequest           BuildQuestQueryRequest(std::uint32_t quest_id);
QuestRewardChoiceRequest    BuildQuestRewardChoiceRequest(std::uint64_t giver_guid,
                                                          std::uint32_t quest_id,
                                                          std::uint32_t reward_index);
QuestConfirmAcceptRequest   BuildQuestConfirmAcceptRequest(std::uint32_t quest_id);
QuestLogRemoveRequest       BuildQuestLogRemoveRequest(std::uint8_t slot);
QuestPushResultRequest      BuildQuestPushResultRequest(std::uint64_t receiver,
                                                        std::uint8_t result);

[[nodiscard]] const char* GetQuestStatusName(QuestStatus status);
[[nodiscard]] bool IsQuestStatusComplete(QuestStatus status);
[[nodiscard]] bool IsQuestStatusAvailable(QuestStatus status);

}
