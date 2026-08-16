#include "openwow/game/actions/model/action_assignments.h"

#include <algorithm>

namespace openwow::game::actions {

const Action& ActionAssignments::Get(ActionSlot slot) const noexcept {
  return values_[slot.zero_based()];
}

bool ActionAssignments::Assign(ActionSlot slot, Action action) noexcept {
  auto& current = values_[slot.zero_based()];
  if (current == action) {
    return false;
  }
  current = action;
  ++revision_;
  return true;
}

bool ActionAssignments::Clear(ActionSlot slot) noexcept {
  return Assign(slot, Action::Empty());
}

bool ActionAssignments::ClearAll() noexcept {
  const bool changed = std::ranges::any_of(
      values_, [](const Action& action) { return !action.empty(); });
  if (!changed) {
    return false;
  }
  values_.fill(Action::Empty());
  ++revision_;
  return true;
}

void ActionAssignments::Reset() noexcept {
  const bool changed = std::ranges::any_of(
      values_, [](const Action& action) { return !action.empty(); });
  values_.fill(Action::Empty());
  sync_state_ = ActionAssignmentSyncState::kInitial;
  server_sync_pending_ = false;
  if (changed) {
    ++revision_;
  }
}

bool ActionAssignments::ApplyServerSnapshot(
    ActionAssignmentSyncState state,
    std::span<const std::uint32_t, ActionSlot::kCount> packed_actions) noexcept {
  Storage replacement;
  std::ranges::transform(packed_actions, replacement.begin(), Action::Decode);
  return ApplyServerSnapshot(state, replacement);
}

bool ActionAssignments::ApplyServerSnapshot(
    ActionAssignmentSyncState state, const Storage& actions) noexcept {
  const bool changed = actions != values_;
  values_ = actions;
  sync_state_ = state;
  if (state == ActionAssignmentSyncState::kUpdate) {
    server_sync_pending_ = false;
  }
  if (changed) {
    ++revision_;
  }
  return changed;
}

void ActionAssignments::BeginServerSync() noexcept {
  sync_state_ = ActionAssignmentSyncState::kBeginSync;
  server_sync_pending_ = true;
}

std::vector<ActionSlot> ActionAssignments::SlotsReferencing(
    ActionKind kind, std::uint32_t identifier) const {
  std::vector<ActionSlot> slots;
  for (std::size_t index = 0; index < values_.size(); ++index) {
    const auto& action = values_[index];
    if (action.kind() != kind || action.identifier() != identifier) {
      continue;
    }
    slots.push_back(*ActionSlot::FromZeroBased(index));
  }
  return slots;
}

std::vector<std::uint32_t> ActionAssignments::MacroIdentifiers() const {
  std::vector<std::uint32_t> identifiers;
  for (const auto& action : values_) {
    if (action.kind() == ActionKind::kMacro && !action.empty()) {
      identifiers.push_back(action.identifier());
    }
  }
  return identifiers;
}

}
