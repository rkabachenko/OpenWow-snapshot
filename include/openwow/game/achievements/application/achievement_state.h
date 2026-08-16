#pragma once

#include "openwow/game/achievements/application/achievement_metadata_catalog.h"
#include "openwow/game/achievements/model/achievement_state_types.h"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace openwow::game {

class AchievementState final {
 public:
  explicit AchievementState(
      const AchievementMetadataCatalog* metadata_catalog = nullptr)
      : metadata_catalog_(metadata_catalog) {}

  void SetMetadataCatalog(
      const AchievementMetadataCatalog* metadata_catalog) {
    metadata_catalog_ = metadata_catalog;
  }

  void ReplaceLocalData(AchievementDataSnapshot snapshot);
  void RecordEarned(AchievementEarnedInfo earned, ObjectGuid active_player);
  void ApplyCriteriaProgress(CriteriaProgress progress);
  [[nodiscard]] CriteriaDeleteResult RemoveCriteria(
      AchievementCriteriaId criteria_id);
  void RecordServerFirst(ServerFirstInfo server_first);
  [[nodiscard]] InspectApplicationStatus ApplyInspectResult(
      InspectAchievementResult inspect);
  [[nodiscard]] AchievementDeleteResult RemoveAchievement(
      AchievementId achievement_id);

  [[nodiscard]] const std::unordered_map<AchievementId, CompletedAchievement,
                                         AchievementIdHash>&
  completed() const {
    return completed_;
  }

  [[nodiscard]] const std::vector<CompletedAchievement>& completed_list() const {
    return completed_list_;
  }

  [[nodiscard]] const CompletedAchievement* FindCompletedAchievement(
      AchievementId achievement_id) const;
  [[nodiscard]] const CriteriaProgress* FindCriteriaProgress(
      AchievementCriteriaId criteria_id) const;
  [[nodiscard]] const CompletedAchievement* FindComparisonAchievement(
      AchievementId achievement_id) const;

  [[nodiscard]] const std::unordered_map<AchievementCriteriaId,
                                         CriteriaProgress,
                                         AchievementCriteriaIdHash>&
  criteria() const {
    return criteria_;
  }

  [[nodiscard]] const std::vector<AchievementEarnedInfo>& recent_earned() const {
    return recent_earned_;
  }

  [[nodiscard]] const std::vector<ServerFirstInfo>& server_firsts() const {
    return server_firsts_;
  }

  [[nodiscard]] const InspectAchievementResult& last_inspect() const {
    return last_inspect_;
  }

  [[nodiscard]] const std::optional<CriteriaProgress>&
  last_criteria_update() const {
    return last_criteria_update_;
  }

  [[nodiscard]] std::vector<AchievementCriteriaId> latest_updated_stats() const;
  [[nodiscard]] std::vector<AchievementCriteriaId>
  latest_updated_comparison_stats() const;

  [[nodiscard]] ComparisonAchievementDataStatus comparison_data_status() const {
    return comparison_data_status_;
  }

  [[nodiscard]] InspectApplicationStatus last_inspect_status() const {
    return last_inspect_status_;
  }

  [[nodiscard]] ObjectGuid comparison_unit() const {
    return comparison_unit_;
  }

  [[nodiscard]] bool has_comparison_unit() const {
    return !comparison_unit_.IsEmpty();
  }

  [[nodiscard]] AchievementUiReadiness ui_readiness() const {
    return ui_readiness_;
  }

  void MarkUiReady();

  [[nodiscard]] ComparisonAchievementPointsStatus
  comparison_achievement_points_status() const {
    return comparison_achievement_points_status_;
  }

  void LatchComparisonAchievementPointsReady() {
    comparison_achievement_points_status_ =
        ComparisonAchievementPointsStatus::kReady;
  }

  [[nodiscard]] std::optional<AchievementId> last_achievement_deleted() const {
    return last_achievement_deleted_;
  }

  [[nodiscard]] const std::vector<CriteriaProgress>&
  criteria_load_sequence() const {
    return criteria_load_sequence_;
  }

  void RecordRecentUpdatedStat(AchievementCriteriaId criteria_id,
                               AchievementId achievement_id);
  void RecordRecentUpdatedComparisonStat(AchievementCriteriaId criteria_id);
  void SetComparisonUnit(ObjectGuid guid);
  void ClearComparisonAchievementData();
  void ClearComparisonState();
  void Clear();

 private:
  bool UpsertCompletedAchievement(const CompletedAchievement& achievement);
  void UpsertComparisonAchievement(
      InspectAchievementResult& inspect,
      const CompletedAchievement& achievement);
  void ApplyLocalCriteriaProgress(const CriteriaProgress& progress);
  void ClearLocalCriteriaState();
  void ClearComparisonCriteriaState();

  const AchievementMetadataCatalog* metadata_catalog_ = nullptr;
  std::unordered_map<AchievementId, CompletedAchievement, AchievementIdHash>
      completed_;
  std::vector<CompletedAchievement> completed_list_;
  std::unordered_map<AchievementCriteriaId, CriteriaProgress,
                     AchievementCriteriaIdHash>
      criteria_;
  std::vector<CriteriaProgress> criteria_load_sequence_;
  std::vector<AchievementEarnedInfo> recent_earned_;
  std::vector<ServerFirstInfo> server_firsts_;
  ObjectGuid comparison_unit_;
  InspectAchievementResult last_inspect_;
  std::vector<std::pair<AchievementCriteriaId, AchievementId>>
      recent_updated_stats_;
  std::vector<AchievementCriteriaId> recent_updated_comparison_stats_;
  std::optional<CriteriaProgress> last_criteria_update_;
  AchievementUiReadiness ui_readiness_ =
      AchievementUiReadiness::kWaitingForData;
  ComparisonAchievementPointsStatus comparison_achievement_points_status_ =
      ComparisonAchievementPointsStatus::kPending;
  ComparisonAchievementDataStatus comparison_data_status_ =
      ComparisonAchievementDataStatus::kUnavailable;
  InspectApplicationStatus last_inspect_status_ =
      InspectApplicationStatus::kNotAttempted;
  std::optional<AchievementId> last_achievement_deleted_;
};

}
