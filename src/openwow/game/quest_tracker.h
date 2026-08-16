#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

struct TrackedObjective {
    std::string text;
    uint32_t current = 0;
    uint32_t required = 0;
    bool isComplete = false;
};

struct TrackedQuest {
    uint32_t questId = 0;
    std::string title;
    std::vector<TrackedObjective> objectives;
    bool isComplete = false;
    bool isAutoTracked = false;
    int32_t sortOrder = 0;
};

class QuestTracker {
 public:
    static constexpr uint32_t kMaxTracked = 25;

    QuestTracker() = default;

    bool TrackQuest(uint32_t questId, const std::string& title);
    bool UntrackQuest(uint32_t questId);
    [[nodiscard]] bool IsTracked(uint32_t questId) const;

    [[nodiscard]] std::vector<TrackedQuest> GetTrackedQuests() const;
    [[nodiscard]] uint32_t GetTrackedCount() const;
    [[nodiscard]] uint32_t GetMaxTracked() const { return kMaxTracked; }

    void SetObjectives(uint32_t questId,
                       const std::vector<TrackedObjective>& objectives);
    [[nodiscard]] std::vector<TrackedObjective> GetObjectives(
        uint32_t questId) const;

    void SetQuestComplete(uint32_t questId, bool complete);
    [[nodiscard]] bool IsQuestComplete(uint32_t questId) const;
    [[nodiscard]] uint32_t GetCompletedTrackedCount() const;

    bool MoveUp(uint32_t questId);
    bool MoveDown(uint32_t questId);

    void SetAutoTrack(bool enabled) { autoTrack_ = enabled; }
    [[nodiscard]] bool IsAutoTrack() const { return autoTrack_; }

    void SetMinimized(bool minimized) { minimized_ = minimized; }
    [[nodiscard]] bool IsMinimized() const { return minimized_; }

    void Clear();
    void Reset();

 private:
    std::vector<TrackedQuest> tracked_;
    bool autoTrack_ = true;
    bool minimized_ = false;

    [[nodiscard]] int FindIndex(uint32_t questId) const;
};

}
