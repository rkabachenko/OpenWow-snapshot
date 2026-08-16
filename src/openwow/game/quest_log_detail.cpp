
#include "openwow/game/quest_log_detail.h"

namespace openwow::game {

void QuestLogDetailDisplay::SetQuest(QuestLogDetail detail) {
    if (quests_.size() >= kMaxQuests &&
        quests_.find(detail.questId) == quests_.end()) {
        return;
    }
    uint32_t id = detail.questId;
    quests_.insert_or_assign(id, std::move(detail));
}

std::optional<QuestLogDetail> QuestLogDetailDisplay::GetQuest(
    uint32_t questId) const {
    auto it = quests_.find(questId);
    if (it != quests_.end()) return it->second;
    return std::nullopt;
}

std::vector<QuestLogDetail> QuestLogDetailDisplay::GetAllQuests() const {
    std::vector<QuestLogDetail> result;
    result.reserve(quests_.size());
    for (const auto& [id, q] : quests_) result.push_back(q);
    return result;
}

size_t QuestLogDetailDisplay::GetQuestCount() const { return quests_.size(); }
size_t QuestLogDetailDisplay::GetMaxQuests() const { return kMaxQuests; }

std::vector<QuestLogDetail> QuestLogDetailDisplay::GetCompleteQuests() const {
    std::vector<QuestLogDetail> result;
    for (const auto& [id, q] : quests_) {
        if (q.isComplete) result.push_back(q);
    }
    return result;
}

std::vector<QuestLogDetail> QuestLogDetailDisplay::GetByZone(
    uint32_t zoneId) const {
    std::vector<QuestLogDetail> result;
    for (const auto& [id, q] : quests_) {
        if (q.zoneId == zoneId) result.push_back(q);
    }
    return result;
}

std::vector<QuestLogDetail> QuestLogDetailDisplay::GetDailyQuests() const {
    std::vector<QuestLogDetail> result;
    for (const auto& [id, q] : quests_) {
        if (q.isDaily) result.push_back(q);
    }
    return result;
}

bool QuestLogDetailDisplay::RemoveQuest(uint32_t questId) {
    auto erased = quests_.erase(questId);
    if (selectedQuestId_.has_value() && *selectedQuestId_ == questId) {
        selectedQuestId_.reset();
    }
    return erased > 0;
}

void QuestLogDetailDisplay::UpdateObjective(uint32_t questId, size_t objIndex,
                                            uint32_t current) {
    auto it = quests_.find(questId);
    if (it == quests_.end()) return;
    auto& objectives = it->second.objectiveList;
    if (objIndex >= objectives.size()) return;
    objectives[objIndex].current = current;
    objectives[objIndex].isComplete = (current >= objectives[objIndex].required);
}

std::optional<uint32_t> QuestLogDetailDisplay::GetSelectedQuest() const {
    return selectedQuestId_;
}

void QuestLogDetailDisplay::SelectQuest(uint32_t questId) {
    selectedQuestId_ = questId;
}

bool QuestLogDetailDisplay::IsQuestLogFull() const {
    return quests_.size() >= kMaxQuests;
}

std::vector<QuestLogDetail> QuestLogDetailDisplay::Search(
    const std::string& query) const {
    if (query.empty()) return GetAllQuests();

    std::string lowerQuery;
    lowerQuery.reserve(query.size());
    for (char c : query)
        lowerQuery.push_back(
            static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

    auto containsCI = [&](const std::string& haystack) -> bool {
        std::string lower;
        lower.reserve(haystack.size());
        for (char c : haystack)
            lower.push_back(
                static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        return lower.find(lowerQuery) != std::string::npos;
    };

    std::vector<QuestLogDetail> result;
    for (const auto& [id, q] : quests_) {
        if (containsCI(q.title) || containsCI(q.description) ||
            containsCI(q.objectives)) {
            result.push_back(q);
        }
    }
    return result;
}

void QuestLogDetailDisplay::Reset() {
    quests_.clear();
    selectedQuestId_.reset();
}

}
