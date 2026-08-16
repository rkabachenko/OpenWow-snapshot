
#include "openwow/game/quest_tracker.h"

namespace openwow::game {

int QuestTracker::FindIndex(uint32_t questId) const {
    for (size_t i = 0; i < tracked_.size(); ++i) {
        if (tracked_[i].questId == questId) return static_cast<int>(i);
    }
    return -1;
}

bool QuestTracker::TrackQuest(uint32_t questId, const std::string& title) {
    if (questId == 0) return false;
    if (IsTracked(questId)) return false;
    if (tracked_.size() >= kMaxTracked) return false;

    TrackedQuest tq;
    tq.questId = questId;
    tq.title = title;
    tq.sortOrder = static_cast<int32_t>(tracked_.size());
    tracked_.push_back(std::move(tq));
    return true;
}

bool QuestTracker::UntrackQuest(uint32_t questId) {
    int idx = FindIndex(questId);
    if (idx < 0) return false;
    tracked_.erase(tracked_.begin() + idx);

    for (size_t i = 0; i < tracked_.size(); ++i) {
        tracked_[i].sortOrder = static_cast<int32_t>(i);
    }
    return true;
}

bool QuestTracker::IsTracked(uint32_t questId) const {
    return FindIndex(questId) >= 0;
}

std::vector<TrackedQuest> QuestTracker::GetTrackedQuests() const {
    auto result = tracked_;
    std::sort(result.begin(), result.end(),
              [](const TrackedQuest& a, const TrackedQuest& b) {
                  return a.sortOrder < b.sortOrder;
              });
    return result;
}

uint32_t QuestTracker::GetTrackedCount() const {
    return static_cast<uint32_t>(tracked_.size());
}

void QuestTracker::SetObjectives(
    uint32_t questId, const std::vector<TrackedObjective>& objectives) {
    int idx = FindIndex(questId);
    if (idx < 0) return;
    tracked_[static_cast<size_t>(idx)].objectives = objectives;
}

std::vector<TrackedObjective> QuestTracker::GetObjectives(
    uint32_t questId) const {
    int idx = FindIndex(questId);
    if (idx < 0) return {};
    return tracked_[static_cast<size_t>(idx)].objectives;
}

void QuestTracker::SetQuestComplete(uint32_t questId, bool complete) {
    int idx = FindIndex(questId);
    if (idx < 0) return;
    tracked_[static_cast<size_t>(idx)].isComplete = complete;
}

bool QuestTracker::IsQuestComplete(uint32_t questId) const {
    int idx = FindIndex(questId);
    if (idx < 0) return false;
    return tracked_[static_cast<size_t>(idx)].isComplete;
}

uint32_t QuestTracker::GetCompletedTrackedCount() const {
    uint32_t count = 0;
    for (const auto& tq : tracked_) {
        if (tq.isComplete) ++count;
    }
    return count;
}

bool QuestTracker::MoveUp(uint32_t questId) {
    int idx = FindIndex(questId);
    if (idx <= 0) return false;
    std::swap(tracked_[static_cast<size_t>(idx)],
              tracked_[static_cast<size_t>(idx - 1)]);

    for (size_t i = 0; i < tracked_.size(); ++i) {
        tracked_[i].sortOrder = static_cast<int32_t>(i);
    }
    return true;
}

bool QuestTracker::MoveDown(uint32_t questId) {
    int idx = FindIndex(questId);
    if (idx < 0 || static_cast<size_t>(idx) >= tracked_.size() - 1)
        return false;
    std::swap(tracked_[static_cast<size_t>(idx)],
              tracked_[static_cast<size_t>(idx + 1)]);
    for (size_t i = 0; i < tracked_.size(); ++i) {
        tracked_[i].sortOrder = static_cast<int32_t>(i);
    }
    return true;
}

void QuestTracker::Clear() {
    tracked_.clear();
}

void QuestTracker::Reset() {
    tracked_.clear();
    autoTrack_ = true;
    minimized_ = false;
}

}
