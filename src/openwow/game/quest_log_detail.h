#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::game {

enum class QuestDetailObjectiveType : uint8_t {
    Kill        = 0,
    Collect     = 1,
    Interact    = 2,
    Area        = 3,
    Event       = 4,
    Reputation  = 5,
    PlayerKill  = 6,
};

struct QuestDetailObjective {
    QuestDetailObjectiveType type = QuestDetailObjectiveType::Kill;
    std::string text;
    uint32_t current    = 0;
    uint32_t required   = 0;
    bool isComplete     = false;
};

struct QuestLogReward {
    uint32_t itemId     = 0;
    std::string itemName;
    uint32_t iconId     = 0;
    uint32_t count      = 1;
    uint32_t quality    = 0;
    bool isChoice       = false;
};

struct QuestLogDetail {
    uint32_t questId         = 0;
    std::string title;
    std::string description;
    std::string objectives;
    std::string completionText;
    uint32_t level           = 0;
    uint32_t suggestedPlayers = 0;
    uint32_t rewardXP        = 0;
    uint32_t rewardMoney     = 0;
    std::vector<QuestDetailObjective> objectiveList;
    std::vector<QuestLogReward> rewards;
    std::vector<QuestLogReward> choiceRewards;
    float zoneX              = 0.0f;
    float zoneY              = 0.0f;
    uint32_t zoneId          = 0;
    bool isDaily             = false;
    bool isComplete          = false;
    bool isFailed            = false;
    bool isShareable         = true;
};

class QuestLogDetailDisplay {
public:
    QuestLogDetailDisplay() = default;

    static constexpr size_t kMaxQuests = 25;

    void SetQuest(QuestLogDetail detail);
    [[nodiscard]] std::optional<QuestLogDetail> GetQuest(uint32_t questId) const;

    [[nodiscard]] std::vector<QuestLogDetail> GetAllQuests() const;
    [[nodiscard]] size_t GetQuestCount() const;
    [[nodiscard]] size_t GetMaxQuests() const;

    [[nodiscard]] std::vector<QuestLogDetail> GetCompleteQuests() const;
    [[nodiscard]] std::vector<QuestLogDetail> GetByZone(uint32_t zoneId) const;
    [[nodiscard]] std::vector<QuestLogDetail> GetDailyQuests() const;

    bool RemoveQuest(uint32_t questId);
    void UpdateObjective(uint32_t questId, size_t objIndex, uint32_t current);

    [[nodiscard]] std::optional<uint32_t> GetSelectedQuest() const;
    void SelectQuest(uint32_t questId);

    [[nodiscard]] bool IsQuestLogFull() const;

    [[nodiscard]] std::vector<QuestLogDetail> Search(
        const std::string& query) const;

    void Reset();

private:
    std::unordered_map<uint32_t, QuestLogDetail> quests_;
    std::optional<uint32_t> selectedQuestId_;
};

}
