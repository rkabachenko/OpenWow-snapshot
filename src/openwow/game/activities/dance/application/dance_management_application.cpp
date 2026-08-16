#include "openwow/game/activities/dance/application/dance_management_application.h"

#include "openwow/game/activities/dance/application/dance_cache_coordinator.h"
#include "openwow/game/activities/dance/application/known_dance_catalog.h"

namespace openwow::game {

DanceManagementApplication::DanceManagementApplication(
    KnownDanceCatalog& known_dances, DanceCacheCoordinator& cache)
    : known_dances_(known_dances), cache_(cache) {}

std::optional<DanceManagementError> DanceManagementApplication::Apply(
    const DanceManagementNotification& notification) {
  if (const auto* failure =
          std::get_if<DanceManagementFailure>(&notification.payload)) {
    return failure->error;
  }

  const auto& change = std::get<DanceManagementChange>(notification.payload);
  if (change.operations.Contains(DanceManagementOperation::kCreate)) {
    known_dances_.Add(change.name, change.dance_id, change.sequence_id);
  }
  if (change.operations.Contains(DanceManagementOperation::kUpdate)) {
    known_dances_.Update(change.name, change.dance_id, change.sequence_id);
    cache_.Invalidate(change.dance_id);
  }
  if (change.operations.Contains(DanceManagementOperation::kRemove)) {
    known_dances_.Remove(change.dance_id);
    cache_.Invalidate(change.dance_id);
  }
  return std::nullopt;
}

}
