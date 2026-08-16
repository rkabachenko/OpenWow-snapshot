
#include "openwow/game/quest_query_bridge.h"

#include "openwow/game/quest_log.h"

namespace openwow::game {

QuestQueryBridge& QuestQueryBridge::Get() {
  static QuestQueryBridge instance;
  return instance;
}

static QuestQueryResult ResultFromSlot(const QuestLogSlot& slot) {
  QuestQueryResult r;
  r.questId = slot.quest_id;
  r.title = slot.title;
  r.description = slot.description;
  r.objectives = slot.objectives_text;
  r.level = slot.level;
  r.requiredLevel = 0;
  r.rewardXP = slot.reward_xp;
  r.rewardMoney = slot.reward_money;
  r.isDaily = slot.is_daily;
  r.isComplete = slot.is_complete;
  r.isTracked = slot.is_tracked;

  for (const auto& obj : slot.objectives) {
    r.objectiveProgress.emplace_back(
        obj.text,
        std::make_pair(obj.current, obj.required));
  }
  return r;
}

std::optional<QuestQueryResult> QuestQueryBridge::Query(std::uint32_t questId) const {
  if (questId == 0) return std::nullopt;

  {
    std::lock_guard lock(mutex_);
    auto it = cache_.find(questId);
    if (it != cache_.end()) return it->second;
  }

  const auto* slot = QuestLog::Get().GetQuestById(questId);
  if (!slot || slot->IsEmpty()) return std::nullopt;

  return ResultFromSlot(*slot);
}

std::string QuestQueryBridge::GetQuestName(std::uint32_t questId) const {
  auto r = Query(questId);
  return r ? r->title : std::string{};
}

std::uint32_t QuestQueryBridge::GetQuestLevel(std::uint32_t questId) const {
  auto r = Query(questId);
  return r ? r->level : 0;
}

bool QuestQueryBridge::IsQuestComplete(std::uint32_t questId) const {
  auto r = Query(questId);
  return r ? r->isComplete : false;
}

std::uint32_t QuestQueryBridge::GetNumQuestLogEntries() const {

  {
    std::lock_guard lock(mutex_);
    if (!cache_.empty()) {
      return static_cast<std::uint32_t>(cache_.size());
    }
  }

  return static_cast<std::uint32_t>(QuestLog::Get().GetNumQuests());
}

std::optional<QuestQueryResult> QuestQueryBridge::GetQuestLogEntry(
    std::uint32_t index) const {

  if (index == 0) return std::nullopt;

  const auto* slot = QuestLog::Get().GetQuest(static_cast<std::size_t>(index - 1));
  if (!slot || slot->IsEmpty()) return std::nullopt;

  {
    std::lock_guard lock(mutex_);
    auto it = cache_.find(slot->quest_id);
    if (it != cache_.end()) return it->second;
  }

  return ResultFromSlot(*slot);
}

bool QuestQueryBridge::IsQuestTracked(std::uint32_t questId) const {

  {
    std::lock_guard lock(mutex_);
    auto it = cache_.find(questId);
    if (it != cache_.end()) return it->second.isTracked;
  }

  return QuestLog::Get().IsTracked(questId);
}

void QuestQueryBridge::SetQuestData(std::uint32_t questId, QuestQueryResult data) {
  std::lock_guard lock(mutex_);
  data.questId = questId;
  cache_[questId] = std::move(data);
}

void QuestQueryBridge::Reset() {
  std::lock_guard lock(mutex_);
  cache_.clear();
}

}
