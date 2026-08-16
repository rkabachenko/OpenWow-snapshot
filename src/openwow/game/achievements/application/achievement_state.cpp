#include "openwow/game/achievements/application/achievement_state.h"

#include "openwow/game/achievements/model/achievement_completion_order.h"

#include <algorithm>

namespace openwow::game {
namespace {

constexpr std::size_t kLatestUpdatedStatsLimit = 5;

template <typename T, typename Predicate>
void EraseIf(std::vector<T>& values, Predicate predicate) {
  values.erase(
      std::remove_if(values.begin(), values.end(), predicate), values.end());
}

[[nodiscard]] bool CriteriaUpdateRemovesLocalState(
    const CriteriaProgress& progress) {
  return progress.flags.Contains(
      CriteriaProgressFlag::kTimedFailureRemovesLocalState);
}

}

void AchievementState::ReplaceLocalData(AchievementDataSnapshot snapshot) {
  completed_.clear();
  completed_list_.clear();
  for (const auto& achievement : snapshot.achievements) {
    UpsertCompletedAchievement(achievement);
  }

  ClearLocalCriteriaState();
  criteria_load_sequence_ = snapshot.criteria;
  for (const auto& progress : snapshot.criteria) {
    ApplyLocalCriteriaProgress(progress);
  }
}

void AchievementState::MarkUiReady() {
  ui_readiness_ = AchievementUiReadiness::kReady;
}

void AchievementState::RecordEarned(AchievementEarnedInfo earned,
                                    const ObjectGuid active_player) {
  earned.owner_relation = earned.player_guid == active_player
                              ? AchievementOwnerRelation::kActivePlayer
                              : AchievementOwnerRelation::kOtherPlayer;
  if (earned.owner_relation == AchievementOwnerRelation::kActivePlayer) {
    UpsertCompletedAchievement(
        CompletedAchievement{earned.achievement_id, earned.earn_date});
  }
  recent_earned_.push_back(std::move(earned));
}

void AchievementState::ApplyCriteriaProgress(CriteriaProgress progress) {
  ApplyLocalCriteriaProgress(progress);
  last_criteria_update_ = std::move(progress);
}

CriteriaDeleteResult AchievementState::RemoveCriteria(
    const AchievementCriteriaId criteria_id) {
  const auto removed_from_map = criteria_.erase(criteria_id);
  const auto original_size = criteria_load_sequence_.size();
  EraseIf(criteria_load_sequence_,
          [criteria_id](const CriteriaProgress& progress) {
            return progress.criteria_id == criteria_id;
          });
  return {
      .criteria_id = criteria_id,
      .outcome =
          removed_from_map != 0 ||
                  criteria_load_sequence_.size() != original_size
              ? AchievementRemovalOutcome::kRemoved
              : AchievementRemovalOutcome::kNotPresent,
  };
}

void AchievementState::RecordServerFirst(ServerFirstInfo server_first) {
  server_firsts_.push_back(std::move(server_first));
}

InspectApplicationStatus AchievementState::ApplyInspectResult(
    InspectAchievementResult inspect) {
  last_inspect_status_ = InspectApplicationStatus::kIgnoredWrongTarget;
  if (inspect.target != comparison_unit_) {
    return last_inspect_status_;
  }

  ClearComparisonAchievementData();
  last_inspect_.target = inspect.target;
  last_inspect_.achievements.reserve(inspect.achievements.size());
  for (const auto& achievement : inspect.achievements) {
    UpsertComparisonAchievement(last_inspect_, achievement);
  }
  last_inspect_.criteria = std::move(inspect.criteria);
  comparison_data_status_ = ComparisonAchievementDataStatus::kAvailable;
  last_inspect_status_ = InspectApplicationStatus::kApplied;
  return last_inspect_status_;
}

AchievementDeleteResult AchievementState::RemoveAchievement(
    const AchievementId achievement_id) {
  last_achievement_deleted_ = achievement_id;
  const auto removed_from_map = completed_.erase(achievement_id);
  const auto original_size = completed_list_.size();
  EraseIf(completed_list_,
          [achievement_id](const CompletedAchievement& achievement) {
            return achievement.id == achievement_id;
          });
  return {
      .achievement_id = achievement_id,
      .outcome = removed_from_map != 0 ||
                         completed_list_.size() != original_size
                     ? AchievementRemovalOutcome::kRemoved
                     : AchievementRemovalOutcome::kNotPresent,
  };
}

bool AchievementState::UpsertCompletedAchievement(
    const CompletedAchievement& achievement) {
  if (metadata_catalog_ == nullptr ||
      !metadata_catalog_->Contains(achievement.id)) {
    return false;
  }
  const auto new_order_in_group =
      metadata_catalog_->OrderInGroup(achievement.id);

  completed_.insert_or_assign(achievement.id, achievement);
  const auto existing = std::find_if(
      completed_list_.begin(), completed_list_.end(),
      [achievement_id = achievement.id](
          const CompletedAchievement& current) {
        return current.id == achievement_id;
      });
  if (existing != completed_list_.end()) {
    existing->completion_date = achievement.completion_date;
    return true;
  }

  const auto insert_position = std::find_if(
      completed_list_.begin(), completed_list_.end(),
      [this, &achievement,
       new_order_in_group](const CompletedAchievement& current) {
        return metadata_catalog_->Contains(current.id) &&
               CompletedAchievementSortsBefore(
                   achievement.completion_date.ToWireValue(),
                   new_order_in_group,
                   current.completion_date.ToWireValue(),
                   metadata_catalog_->OrderInGroup(current.id));
      });
  completed_list_.insert(insert_position, achievement);
  return true;
}

void AchievementState::UpsertComparisonAchievement(
    InspectAchievementResult& inspect,
    const CompletedAchievement& achievement) {
  const auto existing = std::find_if(
      inspect.achievements.begin(), inspect.achievements.end(),
      [achievement_id = achievement.id](
          const CompletedAchievement& current) {
        return current.id == achievement_id;
      });
  if (existing != inspect.achievements.end()) {
    existing->completion_date = achievement.completion_date;
    return;
  }

  const auto new_order_in_group =
      metadata_catalog_ == nullptr
          ? 0
          : metadata_catalog_->OrderInGroup(achievement.id);
  const auto insert_position = std::find_if(
      inspect.achievements.begin(), inspect.achievements.end(),
      [this, &achievement,
       new_order_in_group](const CompletedAchievement& current) {
        return CompletedAchievementSortsBefore(
            achievement.completion_date.ToWireValue(), new_order_in_group,
            current.completion_date.ToWireValue(),
            metadata_catalog_ == nullptr
                ? 0
                : metadata_catalog_->OrderInGroup(current.id));
      });
  inspect.achievements.insert(insert_position, achievement);
}

void AchievementState::ApplyLocalCriteriaProgress(
    const CriteriaProgress& progress) {
  if (CriteriaUpdateRemovesLocalState(progress)) {
    criteria_.erase(progress.criteria_id);
    return;
  }
  criteria_.insert_or_assign(progress.criteria_id, progress);
}

void AchievementState::ClearLocalCriteriaState() {
  criteria_.clear();
  criteria_load_sequence_.clear();
  recent_updated_stats_.clear();
  last_criteria_update_.reset();
}

void AchievementState::ClearComparisonCriteriaState() {
  last_inspect_.criteria.clear();
  recent_updated_comparison_stats_.clear();
}

const CompletedAchievement* AchievementState::FindCompletedAchievement(
    const AchievementId achievement_id) const {
  const auto it = completed_.find(achievement_id);
  return it == completed_.end() ? nullptr : &it->second;
}

const CriteriaProgress* AchievementState::FindCriteriaProgress(
    const AchievementCriteriaId criteria_id) const {
  const auto it = criteria_.find(criteria_id);
  return it == criteria_.end() ? nullptr : &it->second;
}

const CompletedAchievement* AchievementState::FindComparisonAchievement(
    const AchievementId achievement_id) const {
  const auto& achievements = last_inspect_.achievements;
  const auto it = std::find_if(
      achievements.begin(), achievements.end(),
      [achievement_id](const CompletedAchievement& achievement) {
        return achievement.id == achievement_id;
      });
  return it == achievements.end() ? nullptr : &*it;
}

void AchievementState::SetComparisonUnit(const ObjectGuid guid) {
  comparison_unit_ = guid;
  last_inspect_status_ = InspectApplicationStatus::kNotAttempted;
}

void AchievementState::ClearComparisonAchievementData() {
  last_inspect_.achievements.clear();
  ClearComparisonCriteriaState();
  comparison_data_status_ = ComparisonAchievementDataStatus::kUnavailable;
  last_inspect_status_ = InspectApplicationStatus::kNotAttempted;
}

void AchievementState::ClearComparisonState() {
  comparison_unit_ = ObjectGuid{};
  last_inspect_.target = ObjectGuid{};
  ClearComparisonAchievementData();
}

std::vector<AchievementCriteriaId> AchievementState::latest_updated_stats()
    const {
  std::vector<AchievementCriteriaId> ids;
  ids.reserve(recent_updated_stats_.size());
  for (const auto& entry : recent_updated_stats_) {
    ids.push_back(entry.first);
  }
  return ids;
}

std::vector<AchievementCriteriaId>
AchievementState::latest_updated_comparison_stats() const {
  return recent_updated_comparison_stats_;
}

void AchievementState::RecordRecentUpdatedStat(
    const AchievementCriteriaId criteria_id,
    const AchievementId achievement_id) {
  EraseIf(recent_updated_stats_,
          [criteria_id, achievement_id](const auto& entry) {
            return entry.first == criteria_id ||
                   entry.second == achievement_id;
          });
  recent_updated_stats_.insert(
      recent_updated_stats_.begin(), {criteria_id, achievement_id});
  if (recent_updated_stats_.size() > kLatestUpdatedStatsLimit) {
    recent_updated_stats_.resize(kLatestUpdatedStatsLimit);
  }
}

void AchievementState::RecordRecentUpdatedComparisonStat(
    const AchievementCriteriaId criteria_id) {
  EraseIf(recent_updated_comparison_stats_,
          [criteria_id](const AchievementCriteriaId existing_criteria_id) {
            return existing_criteria_id == criteria_id;
          });
  recent_updated_comparison_stats_.insert(
      recent_updated_comparison_stats_.begin(), criteria_id);
  if (recent_updated_comparison_stats_.size() > kLatestUpdatedStatsLimit) {
    recent_updated_comparison_stats_.resize(kLatestUpdatedStatsLimit);
  }
}

void AchievementState::Clear() {
  completed_.clear();
  completed_list_.clear();
  ClearLocalCriteriaState();
  recent_earned_.clear();
  server_firsts_.clear();
  ClearComparisonState();
  ui_readiness_ = AchievementUiReadiness::kWaitingForData;
  comparison_achievement_points_status_ =
      ComparisonAchievementPointsStatus::kPending;
  last_achievement_deleted_.reset();
}

}
