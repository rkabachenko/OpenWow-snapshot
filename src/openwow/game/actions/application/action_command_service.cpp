#include "openwow/game/actions/application/action_command_service.h"

namespace openwow::game::actions {

bool RetailActionProtectionPolicy::CanMutate(
    ActionSlot slot, ActionMutationSource source) const {
  if (source != ActionMutationSource::kPickup &&
      source != ActionMutationSource::kPlace) {
    return true;
  }

  return slot.zero_based() < 120 || slot.zero_based() > 131;
}

ActionCommandService::ActionCommandService(
    ActionAssignments& assignments,
    ActionReferenceBookkeeping& bookkeeping,
    ActionStateRecomputer& recomputer,
    ActionTransport& transport,
    ActionEvents& events,
    const ActionProtectionPolicy& protection) noexcept
    : assignments_(assignments),
      bookkeeping_(bookkeeping),
      recomputer_(recomputer),
      transport_(transport),
      events_(events),
      protection_(protection) {}

ActionMutationResult ActionCommandService::Apply(
    const ActionMutation& mutation) {
  if (mutation.source != ActionMutationSource::kServer &&
      assignments_.server_sync_pending()) {
    return ActionMutationResult::kServerSyncPending;
  }
  if (!protection_.CanMutate(mutation.slot, mutation.source)) {
    return ActionMutationResult::kProtected;
  }

  const Action previous = assignments_.Get(mutation.slot);
  if (previous == mutation.replacement) {
    return ActionMutationResult::kUnchanged;
  }

  bookkeeping_.BeforeAssignmentChanges(mutation.slot, previous,
                                       mutation.replacement);
  (void)assignments_.Assign(mutation.slot, mutation.replacement);
  recomputer_.Recompute(mutation.slot, mutation.replacement);
  if (mutation.notify_server) {
    transport_.SendAssignment(mutation.slot, mutation.replacement);
  }
  if (mutation.notify_ui) {
    events_.AssignmentChanged(mutation.slot.lua_index());
  }
  return ActionMutationResult::kApplied;
}

}
