
#include "openwow/game/quest_log.h"

#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/quest_poi.h"
#include "openwow/game/versioned_base93_cvar_codec.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/world_map_system.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <limits>
#include <map>
#include <span>

namespace openwow::game {

namespace {

constexpr std::string_view kTrackedQuestsCVarName = "trackedQuests";
constexpr std::string_view kTrackerSortingCVarName = "trackerSorting";

using VisibleQuestGroups = std::map<std::uint32_t, std::vector<std::size_t>>;

bool HasVisibleQuestTemplate(const QuestLogSlot& entry) {
    return entry.quest_id != 0 && entry.has_cached_template;
}

struct QuestWatchDistanceSortKey {
    std::int32_t floor_delta = std::numeric_limits<std::int32_t>::max();
    float distance_squared = std::numeric_limits<float>::max();
};

VisibleQuestGroups BuildVisibleQuestGroups(
    const std::vector<QuestLogSlot>& entries) {
    VisibleQuestGroups groups;
    for (std::size_t index = 0; index < entries.size(); ++index) {
        if (!HasVisibleQuestTemplate(entries[index])) {
            continue;
        }
        groups[entries[index].zone_sort].push_back(index);
    }
    return groups;
}

std::vector<std::uint32_t> CollectPersistentTrackedQuestIds(
    const std::vector<QuestWatchEntry>& tracked) {
    std::vector<std::uint32_t> quest_ids;
    quest_ids.reserve(tracked.size());
    for (const auto& watch : tracked) {
        if (watch.quest_id == 0 || watch.expiration_time != 0) {
            continue;
        }
        quest_ids.push_back(watch.quest_id);
    }
    return quest_ids;
}

const QuestLogSlot* FindQuestLogEntryById(const std::vector<QuestLogSlot>& entries,
                                          const std::uint32_t questId) {
    const auto it = std::find_if(entries.begin(), entries.end(),
                                 [questId](const QuestLogSlot& entry) {
                                     return entry.quest_id == questId;
                                 });
    return it != entries.end() ? &*it : nullptr;
}

std::int32_t BuildQuestWatchExpiration(const std::uint32_t duration_seconds) {
    if (duration_seconds == 0) {
        return 0;
    }

    const auto now = static_cast<std::uint32_t>(std::time(nullptr));
    return static_cast<std::int32_t>(now + duration_seconds);
}

std::uint32_t GetTrackedQuestLevel(const std::vector<QuestLogSlot>& entries,
                                   const std::uint32_t questId) {
    if (const auto* entry = FindQuestLogEntryById(entries, questId); entry != nullptr) {
        return entry->level;
    }
    return 0;
}

bool QuestPoiMatchesTrackedObjective(const QuestLogSlot& entry,
                                     const QuestPOIEntry& poi) {
    if (entry.is_failed) {
        return false;
    }
    if (entry.is_complete) {
        return poi.objectiveIndex == -1;
    }

    if (poi.objectiveIndex < 0 ||
        poi.objectiveIndex >= static_cast<std::int32_t>(entry.objectives.size())) {
        return false;
    }

    return !entry.objectives[static_cast<std::size_t>(poi.objectiveIndex)].finished;
}

QuestWatchDistanceSortKey ComputeQuestWatchDistanceSortKey(
    const std::vector<QuestLogSlot>& entries,
    const openwow::ui::WorldMapSystem* world_map,
    const ObjectManager& objects,
    const std::uint32_t questId) {
    const auto* entry = FindQuestLogEntryById(entries, questId);
    if (entry == nullptr) {
        return {};
    }

    const auto* local_player = objects.GetLocalPlayerTyped();
    if (local_player == nullptr) {
        return {};
    }

    const auto current_map_id = objects.GetMapId();
    if (current_map_id == 0) {
        return {};
    }

    const auto current_floor =
        world_map != nullptr
            ? std::max(0, world_map->GetCurrentDungeonFloorIndex())
            : 0;
    QuestWatchDistanceSortKey best_key;

    for (const auto& poi : QuestPOIData::Get().GetPOIsForQuest(questId)) {
        if (poi.points.empty() || poi.mapId != current_map_id ||
            !QuestPoiMatchesTrackedObjective(*entry, poi)) {
            continue;
        }

        const auto anchor = QuestPOIData::Get().GetCentroid(poi);
        const float dx = anchor.x - local_player->GetX();
        const float dy = anchor.y - local_player->GetY();
        const auto floor_delta = std::abs(static_cast<int>(poi.floorId) - current_floor);
        const float distance_squared = dx * dx + dy * dy;
        if (floor_delta < best_key.floor_delta ||
            (floor_delta == best_key.floor_delta &&
             distance_squared < best_key.distance_squared)) {
            best_key.floor_delta = floor_delta;
            best_key.distance_squared = distance_squared;
        }
    }

    return best_key;
}

}

QuestLog& QuestLog::Get() {
    static QuestLog instance;
    return instance;
}

void QuestLog::SetQuestLog(const std::vector<QuestLogSlot>& entries) {
    std::lock_guard lock(mutex_);
    entries_ = entries;
    if (entries_.size() > kMaxQuests) {
        entries_.resize(kMaxQuests);
    }

    ExpireTimedWatchesLocked();

    tracked_.erase(
        std::remove_if(
            tracked_.begin(), tracked_.end(),
            [this](const QuestWatchEntry& watch) {
                return watch.quest_id == 0 ||
                       std::none_of(entries_.begin(), entries_.end(),
                                    [&watch](const QuestLogSlot& entry) {
                                        return entry.quest_id == watch.quest_id;
                                    });
            }),
        tracked_.end());

    for (const auto& entry : entries_) {
        if (!entry.is_tracked || entry.quest_id == 0) {
            continue;
        }
        if (FindTrackedIndexLocked(entry.quest_id) >= 0) {
            continue;
        }
        if (tracked_.size() >= kMaxTracked) {
            break;
        }
        tracked_.push_back(QuestWatchEntry{.quest_id = entry.quest_id});
    }

    for (auto& entry : entries_) {
        entry.is_tracked = IsTrackedLocked(entry.quest_id);
    }

    MarkWatchUpdateLocked();
}

size_t QuestLog::GetNumQuests() const {
    std::lock_guard lock(mutex_);
    return entries_.size();
}

const QuestLogSlot* QuestLog::GetQuest(size_t logIndex) const {
    std::lock_guard lock(mutex_);
    if (logIndex >= entries_.size()) return nullptr;
    return &entries_[logIndex];
}

const QuestLogSlot* QuestLog::GetQuestById(uint32_t questId) const {
    std::lock_guard lock(mutex_);
    for (const auto& e : entries_) {
        if (e.quest_id == questId) return &e;
    }
    return nullptr;
}

void QuestLog::SetTracked(uint32_t questId, bool tracked) {
    std::lock_guard lock(mutex_);
    ExpireTimedWatchesLocked();
    if (questId == 0) {
        return;
    }

    const int existing_index = FindTrackedIndexLocked(questId);
    if (tracked) {
        if (existing_index < 0 && tracked_.size() < kMaxTracked) {
            tracked_.push_back(QuestWatchEntry{.quest_id = questId});
            MarkWatchUpdateLocked();
        }
    } else if (existing_index >= 0) {
        tracked_.erase(tracked_.begin() + existing_index);
        MarkWatchUpdateLocked();
    }

    SetEntryTrackedFlagLocked(questId, IsTrackedLocked(questId));
}

bool QuestLog::IsTracked(uint32_t questId) {
    std::lock_guard lock(mutex_);
    ExpireTimedWatchesLocked();
    return IsTrackedLocked(questId);
}

size_t QuestLog::GetNumTracked() {
    std::lock_guard lock(mutex_);
    ExpireTimedWatchesLocked();
    return tracked_.size();
}

int QuestLog::GetTrackedIndex(uint32_t questId) {
    std::lock_guard lock(mutex_);
    ExpireTimedWatchesLocked();
    return FindTrackedIndexLocked(questId);
}

std::uint32_t QuestLog::GetTrackedQuestId(size_t watchIndex) {
    std::lock_guard lock(mutex_);
    ExpireTimedWatchesLocked();
    if (watchIndex >= tracked_.size()) {
        return 0;
    }
    return tracked_[watchIndex].quest_id;
}

bool QuestLog::AddQuestWatch(const ObjectManager& objects, const uint32_t questId,
                             const uint32_t durationSeconds) {
    std::lock_guard lock(mutex_);
    return AddQuestWatchLocked(&objects, questId, durationSeconds,
                               WatchMutationMode::kGameplay);
}

bool QuestLog::AddQuestWatchFromLua(const ObjectManager* const objects,
                                    const uint32_t questId,
                                    const uint32_t durationSeconds) {
    std::lock_guard lock(mutex_);
    return AddQuestWatchLocked(objects, questId, durationSeconds,
                               WatchMutationMode::kNativeLua);
}

bool QuestLog::AddQuestWatchLocked(const ObjectManager* const objects,
                                   const uint32_t questId,
                                   const uint32_t durationSeconds,
                                   const WatchMutationMode mode) {
    const bool is_native_lua = mode == WatchMutationMode::kNativeLua;
    if (!is_native_lua) {
        ExpireTimedWatchesLocked();
    }
    if (questId == 0) {
        return false;
    }
    if (std::none_of(entries_.begin(), entries_.end(),
                     [questId](const QuestLogSlot& entry) {
                         return entry.quest_id == questId;
                     })) {
        return false;
    }

    const int existing_index = FindTrackedIndexLocked(questId);
    if (existing_index >= 0) {
        auto& watch = tracked_[static_cast<size_t>(existing_index)];
        if (watch.expiration_time > 0) {
            watch.expiration_time = BuildQuestWatchExpiration(durationSeconds);
            if (!is_native_lua) {
                MarkWatchUpdateLocked();
            }
            return true;
        }
        return false;
    }

    if (tracked_.size() >= kMaxTracked) {
        return false;
    }

    tracked_.push_back(QuestWatchEntry{
        .quest_id = questId,
        .expiration_time = BuildQuestWatchExpiration(durationSeconds),
    });
    SetEntryTrackedFlagLocked(questId, true);
    if (objects != nullptr) {
        SortQuestWatchesLocked(*objects);
    }
    if (!is_native_lua) {
        MarkWatchUpdateLocked();
    }
    return true;
}

bool QuestLog::RemoveQuestWatch(uint32_t questId) {
    std::lock_guard lock(mutex_);
    ExpireTimedWatchesLocked();
    const int index = FindTrackedIndexLocked(questId);
    if (index < 0) {
        return false;
    }

    tracked_.erase(tracked_.begin() + index);
    SetEntryTrackedFlagLocked(questId, false);
    MarkWatchUpdateLocked();
    return true;
}

bool QuestLog::MoveQuestWatch(size_t fromIndex, size_t toIndex) {
    std::lock_guard lock(mutex_);
    ExpireTimedWatchesLocked();
    if (fromIndex >= tracked_.size() || toIndex >= tracked_.size() ||
        fromIndex == toIndex) {
        return false;
    }

    if (fromIndex < toIndex) {
        std::rotate(tracked_.begin() + static_cast<ptrdiff_t>(fromIndex),
                    tracked_.begin() + static_cast<ptrdiff_t>(fromIndex + 1),
                    tracked_.begin() + static_cast<ptrdiff_t>(toIndex + 1));
    } else {
        std::rotate(tracked_.begin() + static_cast<ptrdiff_t>(toIndex),
                    tracked_.begin() + static_cast<ptrdiff_t>(fromIndex),
                    tracked_.begin() + static_cast<ptrdiff_t>(fromIndex + 1));
    }
    MarkWatchUpdateLocked();
    return true;
}

bool QuestLog::SortQuestWatches(const ObjectManager& objects) {
    std::lock_guard lock(mutex_);
    ExpireTimedWatchesLocked();
    if (!SortQuestWatchesLocked(objects)) {
        return false;
    }
    MarkWatchUpdateLocked();
    return true;
}

void QuestLog::UpdateObjective(uint32_t questId, uint8_t objectiveIndex,
                                uint32_t current) {
    std::lock_guard lock(mutex_);
    for (auto& e : entries_) {
        if (e.quest_id == questId) {
            if (objectiveIndex < e.objectives.size()) {
                auto& obj = e.objectives[objectiveIndex];
                obj.current = current;
                obj.finished = obj.IsComplete();
            }

            bool all_done = !e.objectives.empty();
            for (const auto& o : e.objectives) {
                if (!o.IsComplete()) { all_done = false; break; }
            }
            if (e.objectives.empty()) all_done = false;
            e.is_complete = all_done;
            return;
        }
    }
}

void QuestLog::CompleteQuest(uint32_t questId) {
    std::lock_guard lock(mutex_);
    for (auto& e : entries_) {
        if (e.quest_id == questId) {
            e.is_complete = true;
            for (auto& o : e.objectives) o.finished = true;
            return;
        }
    }
}

void QuestLog::FailQuest(uint32_t questId) {
    std::lock_guard lock(mutex_);
    for (auto& e : entries_) {
        if (e.quest_id == questId) {
            e.is_failed = true;
            return;
        }
    }
}

void QuestLog::RemoveQuest(uint32_t questId) {
    std::lock_guard lock(mutex_);
    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(),
                        [questId](const QuestLogSlot& e) {
                            return e.quest_id == questId;
                        }),
        entries_.end());
    if (const int tracked_index = FindTrackedIndexLocked(questId);
        tracked_index >= 0) {
        tracked_.erase(tracked_.begin() + tracked_index);
    }
    if (selected_quest_ == questId) selected_quest_ = 0;
}

void QuestLog::AddQuest(const QuestLogSlot& entry) {
    std::lock_guard lock(mutex_);
    if (entries_.size() >= kMaxQuests) return;

    for (const auto& e : entries_) {
        if (e.quest_id == entry.quest_id) return;
    }
    entries_.push_back(entry);
    if (entry.is_tracked && entry.quest_id != 0 &&
        FindTrackedIndexLocked(entry.quest_id) < 0 &&
        tracked_.size() < kMaxTracked) {
        tracked_.push_back(QuestWatchEntry{.quest_id = entry.quest_id});
    }
    entries_.back().is_tracked = IsTrackedLocked(entry.quest_id);
}

void QuestLog::SelectQuest(uint32_t questId) {
    std::lock_guard lock(mutex_);
    selected_quest_ = questId;
}

uint32_t QuestLog::GetSelectedQuestId() const {
    std::lock_guard lock(mutex_);
    return selected_quest_;
}

std::vector<QuestLog::QuestHeader> QuestLog::GetHeaders() const {
    std::lock_guard lock(mutex_);

    const auto groups = BuildVisibleQuestGroups(entries_);
    std::vector<QuestHeader> result;
    result.reserve(groups.size());
    for (const auto& [zone, indices] : groups) {
        QuestHeader header;
        header.name = zone == 0 ? "Miscellaneous"
                                : ("Zone " + std::to_string(zone));
        header.quest_ids.reserve(indices.size());
        for (const auto quest_log_index : indices) {
            header.quest_ids.push_back(entries_[quest_log_index].quest_id);
        }
        result.push_back(std::move(header));
    }
    return result;
}

uint32_t QuestLog::GetDailyQuestsDone() const {
    std::lock_guard lock(mutex_);
    return daily_done_;
}

void QuestLog::SetDailyQuestsDone(uint32_t count) {
    std::lock_guard lock(mutex_);
    daily_done_ = count;
}

int QuestLog::GetQuestLogIndexById(uint32_t questId) const {
    std::lock_guard lock(mutex_);
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].quest_id == questId)
            return static_cast<int>(i);
    }
    return -1;
}

int QuestLog::GetVisibleIndexByQuestId(uint32_t questId) const {
    std::lock_guard lock(mutex_);
    if (questId == 0) return -1;

    const auto groups = BuildVisibleQuestGroups(entries_);
    int position = 0;
    bool first_group = true;
    for (const auto& [zone, indices] : groups) {
        if (!first_group) {
            ++position;
        }
        first_group = false;
        for (const auto entry_index : indices) {
            if (entries_[entry_index].quest_id == questId) {
                return position + 1;
            }
            ++position;
        }
    }
    return -1;
}

uint32_t QuestLog::GetQuestIdByVisibleIndex(int visibleIndex) const {
    std::lock_guard lock(mutex_);
    if (visibleIndex < 1) return 0;

    const auto groups = BuildVisibleQuestGroups(entries_);
    int position = 0;
    bool first_group = true;
    for (const auto& [zone, indices] : groups) {
        if (!first_group) {
            if (position + 1 == visibleIndex) {
                return 0;
            }
            ++position;
        }
        first_group = false;
        for (const auto entry_index : indices) {
            if (position + 1 == visibleIndex) {
                return entries_[entry_index].quest_id;
            }
            ++position;
        }
    }
    return 0;
}

bool QuestLog::IsQuestCompleteDetailed(uint32_t questId,
                                         bool checkReputation) const {
    std::lock_guard lock(mutex_);
    for (const auto& e : entries_) {
        if (e.quest_id != questId) continue;
        if (e.is_complete) return true;
        if (e.is_failed) return false;

        for (const auto& obj : e.objectives) {
            if (obj.finished) continue;

            if (!checkReputation && obj.type == 3) continue;
            if (obj.required > 0 && obj.current < obj.required)
                return false;
        }

        return !e.objectives.empty();
    }
    return false;
}

bool QuestLog::IsSelectedQuestFailed() const {
    std::lock_guard lock(mutex_);
    if (selected_quest_ == 0) return false;
    for (const auto& e : entries_) {
        if (e.quest_id == selected_quest_)
            return e.is_failed;
    }
    return false;
}

void QuestLog::ShiftQuestWatch(uint32_t questId, int32_t direction) {
    std::lock_guard lock(mutex_);
    ExpireTimedWatchesLocked();
    const int tracked_index = FindTrackedIndexLocked(questId);
    if (tracked_index < 0) {
        return;
    }

    const int target_index = tracked_index + direction;
    if (target_index < 0 ||
        target_index >= static_cast<int>(tracked_.size())) {
        return;
    }

    std::swap(tracked_[static_cast<size_t>(tracked_index)],
              tracked_[static_cast<size_t>(target_index)]);
    MarkWatchUpdateLocked();
}

void QuestLog::PrepareForLogout() {
    std::lock_guard lock(mutex_);
    entries_.clear();
    selected_quest_ = 0;
    daily_done_ = 0;
}

void QuestLog::Reset() {
    std::lock_guard lock(mutex_);
    const bool had_watches = !tracked_.empty();
    entries_.clear();
    tracked_.clear();
    selected_quest_ = 0;
    daily_done_ = 0;
    tracked_quests_loaded_ = false;
    if (had_watches) {
        MarkWatchUpdateLocked();
    }
}

int QuestLog::FindTrackedIndexLocked(uint32_t questId) const {
    for (size_t i = 0; i < tracked_.size(); ++i) {
        if (tracked_[i].quest_id == questId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool QuestLog::IsTrackedLocked(uint32_t questId) const {
    return FindTrackedIndexLocked(questId) >= 0;
}

bool QuestLog::ExpireTimedWatches() {
    std::lock_guard lock(mutex_);
    return ExpireTimedWatchesLocked();
}

void QuestLog::LoadTrackedQuestsFromCVarIfNeeded(const ObjectManager& objects) {
    {
        std::lock_guard lock(mutex_);
        if (tracked_quests_loaded_ || entries_.empty()) {
            return;
        }
        tracked_quests_loaded_ = true;
    }

    auto& cvars = openwow::ui::game::CVarSystem::Instance();
    const std::string encoded =
        cvars.GetCVar(std::string(kTrackedQuestsCVarName));
    if (detail::VersionedBase93NeedsCanonicalRewrite(encoded)) {
        const auto decoded = detail::DecodeVersionedBase93Payload(
            detail::GetVersionedBase93Payload(encoded));
        cvars.SetCVar(std::string(kTrackedQuestsCVarName),
                      detail::EncodeVersionedBase93Values(decoded), true);
    }

    const auto decoded = detail::DecodeVersionedBase93Payload(
        detail::GetVersionedBase93Payload(encoded));

    bool added_any = false;
    {
        std::lock_guard lock(mutex_);
        for (const auto quest_id : decoded) {
            if (quest_id == 0 || FindTrackedIndexLocked(quest_id) >= 0) {
                continue;
            }
            if (std::none_of(entries_.begin(), entries_.end(),
                             [quest_id](const QuestLogSlot& entry) {
                                 return entry.quest_id == quest_id;
                             })) {
                continue;
            }
            if (tracked_.size() >= kMaxTracked) {
                break;
            }

            tracked_.push_back(QuestWatchEntry{.quest_id = quest_id});
            SetEntryTrackedFlagLocked(quest_id, true);
            added_any = true;
        }
        if (added_any) {
            SortQuestWatchesLocked(objects);
            MarkWatchUpdateLocked();
        }
    }
}

void QuestLog::SaveTrackedQuestsToCVar() const {
    std::vector<std::uint32_t> tracked_quest_ids;
    {
        std::lock_guard lock(mutex_);
        tracked_quest_ids = CollectPersistentTrackedQuestIds(tracked_);
    }

    openwow::ui::game::CVarSystem::Instance().SetCVar(
        std::string(kTrackedQuestsCVarName),
        detail::EncodeVersionedBase93Values(
            std::span<const std::uint32_t>(tracked_quest_ids)),
        true);
}

void QuestLog::SignalWatchUpdate() {
    std::lock_guard lock(mutex_);
    MarkWatchUpdateLocked();
}

std::uint64_t QuestLog::GetWatchUpdateSerial() const {
    std::lock_guard lock(mutex_);
    return watch_update_serial_;
}

bool QuestLog::ExpireTimedWatchesLocked() {
    bool removed_any = false;
    for (auto it = tracked_.begin(); it != tracked_.end(); ) {
        if (it->expiration_time > 0 &&
            static_cast<std::int32_t>(std::time(nullptr)) >= it->expiration_time) {
            SetEntryTrackedFlagLocked(it->quest_id, false);
            it = tracked_.erase(it);
            removed_any = true;
            continue;
        }
        ++it;
    }
    if (removed_any) {
        MarkWatchUpdateLocked();
    }
    return removed_any;
}

bool QuestLog::SortQuestWatchesLocked(const ObjectManager& objects) {
    if (tracked_.size() <= 1) {
        return false;
    }

    const int sort_mode = openwow::ui::game::CVarSystem::Instance().GetCVarInt(
        std::string(kTrackerSortingCVarName));
    if (sort_mode == 0) {
        return false;
    }

    if (sort_mode == 1) {
        std::vector<QuestWatchDistanceSortKey> keys;
        keys.reserve(tracked_.size());
        for (const auto& watch : tracked_) {
            keys.push_back(ComputeQuestWatchDistanceSortKey(
                entries_, world_map_, objects, watch.quest_id));
        }

        bool already_sorted = true;
        for (std::size_t index = 1; index < keys.size(); ++index) {
            const auto& previous = keys[index - 1];
            const auto& current = keys[index];
            if (previous.floor_delta > current.floor_delta ||
                (previous.floor_delta == current.floor_delta &&
                 previous.distance_squared > current.distance_squared)) {
                already_sorted = false;
                break;
            }
        }
        if (already_sorted) {
            return false;
        }

        std::stable_sort(tracked_.begin(), tracked_.end(),
                         [this, &objects](const QuestWatchEntry& lhs, const QuestWatchEntry& rhs) {
                             const auto lhs_key =
                                 ComputeQuestWatchDistanceSortKey(
                                     entries_, world_map_, objects, lhs.quest_id);
                             const auto rhs_key =
                                 ComputeQuestWatchDistanceSortKey(
                                     entries_, world_map_, objects, rhs.quest_id);
                             if (lhs_key.floor_delta != rhs_key.floor_delta) {
                                 return lhs_key.floor_delta < rhs_key.floor_delta;
                             }
                             return lhs_key.distance_squared < rhs_key.distance_squared;
                         });
        return true;
    }

    std::sort(tracked_.begin(), tracked_.end(),
              [this, sort_mode](const QuestWatchEntry& lhs, const QuestWatchEntry& rhs) {
                  if (sort_mode == 2 || sort_mode == 3) {
                      const auto lhs_level = GetTrackedQuestLevel(entries_, lhs.quest_id);
                      const auto rhs_level = GetTrackedQuestLevel(entries_, rhs.quest_id);
                      if (lhs_level != rhs_level) {
                          return sort_mode == 2 ? lhs_level > rhs_level
                                                : lhs_level < rhs_level;
                      }
                  }
                  return lhs.quest_id < rhs.quest_id;
              });
    return true;
}

void QuestLog::MarkWatchUpdateLocked() {
    ++watch_update_serial_;
}

void QuestLog::SetEntryTrackedFlagLocked(uint32_t questId, bool tracked) {
    for (auto& entry : entries_) {
        if (entry.quest_id == questId) {
            entry.is_tracked = tracked;
            return;
        }
    }
}

}
