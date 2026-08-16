
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace openwow::ui {
class WorldMapSystem;
}

namespace openwow::game {

class ObjectManager;

struct QuestLogObjective {
    std::string text;
    uint32_t current = 0;
    uint32_t required = 0;
    bool finished = false;
    uint8_t type = 0;

    [[nodiscard]] bool IsComplete() const {
        return finished || (required > 0 && current >= required);
    }
};

struct QuestLogSlot {
    uint32_t quest_id = 0;
    std::string title;
    std::string description;
    std::string objectives_text;
    uint32_t level = 0;
    uint32_t suggested_group = 0;
    uint32_t zone_sort = 0;
    bool has_cached_template = true;
    bool is_complete = false;
    bool is_failed = false;
    bool is_daily = false;
    bool is_group = false;
    bool is_raid = false;
    bool is_dungeon = false;
    bool is_pvp = false;
    bool is_tracked = false;
    uint32_t timer = 0;
    std::vector<QuestLogObjective> objectives;
    uint32_t reward_money = 0;
    uint32_t reward_xp = 0;
    std::vector<uint32_t> reward_item_ids;

    [[nodiscard]] bool IsEmpty() const { return quest_id == 0; }
};

struct QuestWatchEntry {
    std::uint32_t quest_id = 0;

    std::int32_t expiration_time = 0;
    std::uint32_t opaque_data = 0;
};

class QuestLog {
 public:
    static QuestLog& Get();
    void BindWorldMapSystem(
        const openwow::ui::WorldMapSystem* world_map) noexcept {
        world_map_ = world_map;
    }

    void SetQuestLog(const std::vector<QuestLogSlot>& entries);
    [[nodiscard]] size_t GetNumQuests() const;
    [[nodiscard]] const QuestLogSlot* GetQuest(size_t logIndex) const;
    [[nodiscard]] const QuestLogSlot* GetQuestById(uint32_t questId) const;
    static constexpr size_t kMaxQuests = 25;

    void SetTracked(uint32_t questId, bool tracked);
    [[nodiscard]] bool IsTracked(uint32_t questId);
    [[nodiscard]] size_t GetNumTracked();
    [[nodiscard]] int GetTrackedIndex(uint32_t questId);
    [[nodiscard]] std::uint32_t GetTrackedQuestId(size_t watchIndex);
    bool AddQuestWatch(const ObjectManager& objects, uint32_t questId,
                       uint32_t durationSeconds = 0);

    bool AddQuestWatchFromLua(const ObjectManager* objects, uint32_t questId,
                              uint32_t durationSeconds = 0);
    bool RemoveQuestWatch(uint32_t questId);
    bool MoveQuestWatch(size_t fromIndex, size_t toIndex);
    bool SortQuestWatches(const ObjectManager& objects);
    bool ExpireTimedWatches();
    void LoadTrackedQuestsFromCVarIfNeeded(const ObjectManager& objects);
    void SaveTrackedQuestsToCVar() const;
    void SignalWatchUpdate();
    [[nodiscard]] std::uint64_t GetWatchUpdateSerial() const;
    static constexpr size_t kMaxTracked = 25;

    void UpdateObjective(uint32_t questId, uint8_t objectiveIndex,
                         uint32_t current);
    void CompleteQuest(uint32_t questId);
    void FailQuest(uint32_t questId);
    void RemoveQuest(uint32_t questId);
    void AddQuest(const QuestLogSlot& entry);

    void SelectQuest(uint32_t questId);
    [[nodiscard]] uint32_t GetSelectedQuestId() const;

    struct QuestHeader {
        std::string name;
        bool collapsed = false;
        std::vector<uint32_t> quest_ids;
    };
    [[nodiscard]] std::vector<QuestHeader> GetHeaders() const;

    [[nodiscard]] uint32_t GetDailyQuestsDone() const;
    void SetDailyQuestsDone(uint32_t count);

    [[nodiscard]] int GetQuestLogIndexById(uint32_t questId) const;

    [[nodiscard]] int GetVisibleIndexByQuestId(uint32_t questId) const;
    [[nodiscard]] uint32_t GetQuestIdByVisibleIndex(int visibleIndex) const;

    [[nodiscard]] bool IsQuestCompleteDetailed(uint32_t questId,
                                                bool checkReputation = true) const;

    [[nodiscard]] bool IsSelectedQuestFailed() const;

    void ShiftQuestWatch(uint32_t questId, int32_t direction);

    void PrepareForLogout();
    void Reset();

 private:
    QuestLog() = default;

    [[nodiscard]] int FindTrackedIndexLocked(uint32_t questId) const;
    [[nodiscard]] bool IsTrackedLocked(uint32_t questId) const;
    enum class WatchMutationMode {
        kGameplay,
        kNativeLua,
    };
    bool AddQuestWatchLocked(const ObjectManager* objects, uint32_t questId,
                             uint32_t durationSeconds, WatchMutationMode mode);
    bool SortQuestWatchesLocked(const ObjectManager& objects);
    bool ExpireTimedWatchesLocked();
    void MarkWatchUpdateLocked();
    void SetEntryTrackedFlagLocked(uint32_t questId, bool tracked);

    std::vector<QuestLogSlot> entries_;
    std::vector<QuestWatchEntry> tracked_;
    uint32_t selected_quest_ = 0;
    uint32_t daily_done_ = 0;
    std::uint64_t watch_update_serial_ = 0;
    bool tracked_quests_loaded_ = false;
    const openwow::ui::WorldMapSystem* world_map_ = nullptr;
    mutable std::mutex mutex_;
};

}
