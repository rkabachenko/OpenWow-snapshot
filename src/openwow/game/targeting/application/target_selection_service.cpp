#include "openwow/game/targeting/application/target_selection_service.h"

#include "openwow/game/spell_action.h"
#include "openwow/game/spell_cast_runtime.h"
#include "openwow/game/targeting.h"
#include "openwow/game/world_session.h"

namespace openwow::game::targeting {

TargetSelectionService::TargetSelectionService(
    TargetingSystem &targeting) noexcept
    : targeting_(targeting) {}

TargetSelectionResult TargetSelectionService::ClearTarget(
    WorldSession &session, const ObjectGuid expected_target,
    const TargetClearMode mode) {
  (void)session;
  const bool notify_server = mode == TargetClearMode::kNotifyServer;
  targeting_.ClearTarget(expected_target.GetRawValue(), notify_server);
  return TargetSelectionResult::kCleared;
}

TargetSelectionResult TargetSelectionService::SelectTarget(
    WorldSession &session, const ObjectGuid target) {
  if (session.objects().GetActivePlayer() == nullptr) {
    return TargetSelectionResult::kNoActivePlayer;
  }
  if (target.IsEmpty()) {
    return ClearTarget(session);
  }
  const auto* object = session.objects().Get(target);
  if (object == nullptr) {
    return TargetSelectionResult::kObjectNotFound;
  }

  if (session.spells().GetTargeting().GetSpellId() != 0u) {
    (void)SpellAction_TryAssignTargetByGuid(session, target.GetRawValue());
    return TargetSelectionResult::kUnchanged;
  }

  if (!object->IsUnit()) {
    return TargetSelectionResult::kRejected;
  }

  targeting_.SetTarget(target.GetRawValue());
  return targeting_.target_guid() == target.GetRawValue()
             ? TargetSelectionResult::kSelected
             : TargetSelectionResult::kRejected;
}

TargetSelectionResult TargetSelectionService::SelectTargetIfNone(
    WorldSession &session, const ObjectGuid target) {
  if (!session.objects().GetTargetGuid().IsEmpty()) {
    return TargetSelectionResult::kUnchanged;
  }
  return SelectTarget(session, target);
}

}
