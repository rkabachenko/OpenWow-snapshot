
#pragma once

#include <cstdint>

namespace openwow::game {

inline constexpr int kQuestObjectivesCount = 4;
inline constexpr int kQuestItemObjectivesCount = 6;
inline constexpr int kQuestRewardChoicesCount = 6;
inline constexpr int kQuestRewardsCount = 4;
inline constexpr int kQuestReputationsCount = 5;
inline constexpr int kQuestEmoteCount = 4;
inline constexpr int kMaxQuestLogEntries = 25;

enum class QuestStatus : std::uint8_t {
  kNone = 0,
  kComplete = 1,
  kUnavailable = 2,
  kIncomplete = 3,
  kAvailable = 4,
  kFailed = 5,
  kRewarded = 6,
};

enum class QuestGiverStatus : std::uint8_t {
  kNone = 0,
  kUnavailable = 1,
  kLowLevelAvailable = 2,
  kLowLevelRewardRep = 3,
  kLowLevelAvailableRep = 4,
  kIncomplete = 5,
  kRewardRep = 6,
  kAvailableRep = 7,
  kAvailable = 8,
  kReward2 = 9,
  kReward = 10,
};

enum class QuestFlags : std::uint32_t {
  kNone = 0x00000000,
  kStayAlive = 0x00000001,
  kPartyAccept = 0x00000002,
  kExploration = 0x00000004,
  kSharable = 0x00000008,
  kHasCondition = 0x00000010,
  kHideRewardPoi = 0x00000020,
  kRaid = 0x00000040,
  kTBC = 0x00000080,
  kNoMoneyFromXP = 0x00000100,
  kHiddenRewards = 0x00000200,
  kTracking = 0x00000400,
  kDeprecateReputation = 0x00000800,
  kDaily = 0x00001000,
  kPvp = 0x00002000,
  kUnavailable = 0x00004000,
  kWeekly = 0x00008000,
  kAutocomplete = 0x00010000,
  kDisplayItemInTracker = 0x00020000,
  kObjText = 0x00040000,
  kAutoAccept = 0x00080000,
};

inline QuestFlags operator|(QuestFlags a, QuestFlags b) {
  return static_cast<QuestFlags>(
      static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline QuestFlags operator&(QuestFlags a, QuestFlags b) {
  return static_cast<QuestFlags>(
      static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline bool HasFlag(QuestFlags val, QuestFlags flag) {
  return (static_cast<uint32_t>(val) & static_cast<uint32_t>(flag)) != 0;
}

}
