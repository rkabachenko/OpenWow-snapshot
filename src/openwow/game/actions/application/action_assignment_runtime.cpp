#include "openwow/game/actions/application/action_assignment_runtime.h"
#include "openwow/game/actions/macros/application/macro_catalog.h"
#include "openwow/game/spell_learning_reference.h"
#include "openwow/game/spell_query_bridge.h"

#include <algorithm>
#include <utility>

namespace openwow::game {

namespace {

bool IsMacroActionButtonType(ActionPresentationKind type) {
  return type == ActionPresentationKind::kMacro ||
         type == ActionPresentationKind::kCompanionMacro;
}

void AdjustMacroActionBarLinks(MacroCatalog& macros,
                               const ActionPresentationEntry& button, int delta) {
  if (!IsMacroActionButtonType(button.type) || button.action == 0 ||
      delta == 0) {
    return;
  }

  if (delta > 0) {
    macros.IncrementActionBarLinks(actions::macros::MacroId(button.action));
  } else {
    macros.DecrementActionBarLinks(actions::macros::MacroId(button.action));
  }
}

std::uint32_t ResolveButtonSpellLikeActionId(
    MacroCatalog& macros,
    const ActionAssignmentRuntime::ItemSpellResolver& item_spell_resolver,
    const ActionPresentationEntry& button) {
  switch (button.type) {
    case ActionPresentationKind::kSpell:
      return button.action;
    case ActionPresentationKind::kItem:
      return item_spell_resolver ? item_spell_resolver(button.action) : 0;
    case ActionPresentationKind::kMacro:
    case ActionPresentationKind::kCompanionMacro: {
      if (button.action == 0) {
        return 0;
      }

      const auto macro =
          macros.FindMacro(actions::macros::MacroId(button.action));
      if (!macro) {
        return 0;
      }

      if (macro->resolved_spell_id > 0) {
        return static_cast<std::uint32_t>(macro->resolved_spell_id);
      }

      if (macro->resolved_item_id != 0) {
        return item_spell_resolver
                   ? item_spell_resolver(macro->resolved_item_id)
                   : 0;
      }

      return 0;
    }
    default:
      return 0;
  }
}

}

actions::Action ActionPresentationEntry::ToAssignedAction() const {
  if (IsEmpty()) {
    return actions::Action::Empty();
  }
  return actions::Action::Create(type, action)
      .value_or(actions::Action::Empty());
}

ActionPresentationEntry ActionPresentationEntry::FromAssignedAction(
    const actions::Action& action) {
  if (action.empty()) {
    return ActionPresentationEntry{};
  }

  return {.action = action.identifier(), .type = action.kind()};
}

ActionAssignmentRuntime::ActionAssignmentRuntime(
    MacroCatalog& macros, ItemSpellResolver item_spell_resolver)
    : item_spell_resolver_(std::move(item_spell_resolver)),
      macros_(macros) {
  macros_.SetActionBarMacroSlotProvider(
      [this] { return CollectMacroReferenceIds(); });
}

ActionAssignmentRuntime::~ActionAssignmentRuntime() {
  macros_.SetActionBarMacroSlotProvider({});
}

void ActionAssignmentRuntime::SetSlotValidator(SlotValidator validator) {
  slot_validator_ = std::move(validator);
}

void ActionAssignmentRuntime::BeginServerSync() {
  assignments_.BeginServerSync();
}

void ActionAssignmentRuntime::ApplyServerSnapshot(
    ActionBarState state,
    const actions::ActionAssignments::Storage& server_assignments) {
  auto values = server_assignments;
  if (state == actions::ActionAssignmentSyncState::kUpdate &&
      slot_validator_) {
    for (auto& action : values) {
      const auto button =
          ActionPresentationEntry::FromAssignedAction(action);
      if (!button.IsEmpty() &&
          !slot_validator_(button, action.Encode())) {
        action = actions::Action::Empty();
      }
    }
  }

  ResetUsabilityStates();
  (void)assignments_.ApplyServerSnapshot(state, values);
  macros_.RebuildActionBarReferences();
}

ActionPresentationEntry ActionAssignmentRuntime::GetPresentationEntry(std::size_t slot) const {
  const auto typed_slot = actions::ActionSlot::FromZeroBased(slot);
  return typed_slot
             ? ActionPresentationEntry::FromAssignedAction(
                   assignments_.Get(*typed_slot))
             : ActionPresentationEntry{};
}

std::uint32_t ActionAssignmentRuntime::ResolveSpellLikeActionId(
    const std::size_t slot) const {
  return ResolveButtonSpellLikeActionId(
      macros_, item_spell_resolver_, GetPresentationEntry(slot));
}

bool ActionAssignmentRuntime::SlotHasMacroReference(std::size_t slot) const {
  if (slot >= kMaxActionButtons) {
    return false;
  }

  const auto button = GetPresentationEntry(slot);
  return button.action != 0 &&
         (static_cast<std::uint8_t>(button.type) & 0xF0u) == 0x40u;
}

std::uint32_t ActionAssignmentRuntime::GetMacroReferenceId(std::size_t slot) const {
  if (!SlotHasMacroReference(slot)) {
    return 0;
  }

  return GetPresentationEntry(slot).action;
}

std::vector<actions::macros::MacroId>
ActionAssignmentRuntime::CollectMacroReferenceIds() const {
  std::vector<actions::macros::MacroId> macro_ids;
  macro_ids.reserve(kMaxActionButtons);

  for (std::size_t slot = 0; slot < kMaxActionButtons; ++slot) {
    const auto macro_id = GetMacroReferenceId(slot);
    if (macro_id != 0) {
      macro_ids.emplace_back(macro_id);
    }
  }

  return macro_ids;
}

std::vector<actions::ActionSlot>
ActionAssignmentRuntime::ClearMacroReferences(
    const actions::macros::MacroId macro_id) {
  std::vector<actions::ActionSlot> cleared_slots;
  for (const auto kind : {actions::ActionKind::kMacro,
                          actions::ActionKind::kCompanionMacro}) {
    for (const auto slot :
         assignments_.SlotsReferencing(kind, macro_id.value())) {
      ClearAssignment(slot.zero_based());
      cleared_slots.push_back(slot);
    }
  }
  return cleared_slots;
}

void ActionAssignmentRuntime::SetPresentationEntry(std::size_t slot, const ActionPresentationEntry& button) {
  const auto typed_slot = actions::ActionSlot::FromZeroBased(slot);
  if (!typed_slot) return;
  const auto previous = GetPresentationEntry(slot);
  if (previous == button) return;
  AdjustMacroActionBarLinks(macros_, previous, -1);
  (void)assignments_.Assign(*typed_slot, button.ToAssignedAction());
  usability_states_[slot] = {};
  AdjustMacroActionBarLinks(macros_, button, 1);
}

void ActionAssignmentRuntime::ClearAssignment(std::size_t slot) {
  const auto typed_slot = actions::ActionSlot::FromZeroBased(slot);
  if (!typed_slot) return;
  const auto previous = GetPresentationEntry(slot);
  AdjustMacroActionBarLinks(macros_, previous, -1);
  (void)assignments_.Clear(*typed_slot);
  usability_states_[slot] = {};
}

void ActionAssignmentRuntime::ClearAll() {
  for (const auto& action : assignments_.values()) {
    AdjustMacroActionBarLinks(
        macros_, ActionPresentationEntry::FromAssignedAction(action), -1);
  }
  (void)assignments_.ClearAll();
  ResetUsabilityStates();
}

std::vector<std::size_t> ActionAssignmentRuntime::ReplaceSpellActionReferences(
    const std::uint32_t old_spell_id,
    const std::uint32_t new_spell_id) {
  std::vector<std::size_t> changed_slots;
  if (new_spell_id == 0 || old_spell_id == new_spell_id) {
    return changed_slots;
  }

  const auto replacement_spell = SpellQueryBridge::Get().Query(new_spell_id);

  for (std::size_t slot = 0; slot < kMaxActionButtons; ++slot) {
    auto button = GetPresentationEntry(slot);

    if (button.type == ActionPresentationKind::kSpell) {
      if (!ShouldReplaceLearnedSpellReference(button.action, old_spell_id,
                                              replacement_spell)) {
        continue;
      }

      SetPresentationEntry(slot, {.action = new_spell_id,
                       .type = ActionPresentationKind::kSpell});
      changed_slots.push_back(slot);
      continue;
    }

    if (!IsMacroActionButtonType(button.type) || button.action == 0) {
      continue;
    }

    const actions::macros::MacroId macro_id(button.action);
    const auto macro = macros_.FindMacro(macro_id);
    if (!macro) {
      continue;
    }

    std::uint32_t spell_like_id = 0;
    if (macro->resolved_spell_id > 0) {
      spell_like_id = static_cast<std::uint32_t>(macro->resolved_spell_id);
    } else if (macro->resolved_item_id != 0) {
      spell_like_id = item_spell_resolver_
                          ? item_spell_resolver_(macro->resolved_item_id)
                          : 0;
    }

    if (!ShouldReplaceLearnedSpellReference(spell_like_id, old_spell_id,
                                            replacement_spell)) {
      continue;
    }

    macros_.UpdateMacro(macro_id, [new_spell_id](MacroDocument& document) {
      document.resolved_spell_id = static_cast<std::int32_t>(new_spell_id);
    });
    usability_states_[slot] = {};
    changed_slots.push_back(slot);
  }

  if (!changed_slots.empty()) {
    assignments_.MarkReferencedContentChanged();
  }

  return changed_slots;
}

std::vector<std::size_t> ActionAssignmentRuntime::ClearSpellActionReferences(
    const std::uint32_t spell_id) {
  std::vector<std::size_t> changed_slots;
  if (spell_id == 0 || assignments_.server_sync_pending()) {
    return changed_slots;
  }

  for (std::size_t slot = 0; slot < kMaxActionButtons; ++slot) {
    if (ResolveButtonSpellLikeActionId(
            macros_, item_spell_resolver_, GetPresentationEntry(slot)) !=
        spell_id) {
      continue;
    }

    ClearAssignment(slot);
    changed_slots.push_back(slot);
  }

  return changed_slots;
}

const ActionButtonUsabilityState& ActionAssignmentRuntime::GetUsabilityState(
    std::size_t slot) const {
  static const ActionButtonUsabilityState kEmptyState{};
  if (slot >= kMaxActionButtons) return kEmptyState;
  return usability_states_[slot];
}

bool ActionAssignmentRuntime::UpdateUsabilityState(
    std::size_t slot,
    const ActionButtonUsabilityState& state) {
  if (slot >= kMaxActionButtons) return false;
  const bool changed = usability_states_[slot] != state;
  usability_states_[slot] = state;
  return changed;
}

void ActionAssignmentRuntime::ResetUsabilityStates() {
  usability_states_.fill(ActionButtonUsabilityState{});
}

ActionPresentationEntry ActionAssignmentRuntime::GetBarSlot(std::size_t bar,
                                                std::size_t slot) const {
  if (bar >= kNumBars || slot >= kSlotsPerBar) return {};
  return GetPresentationEntry(bar * kSlotsPerBar + slot);
}

void ActionAssignmentRuntime::SetBarSlot(std::size_t bar, std::size_t slot,
                            const ActionPresentationEntry& button) {
  if (bar >= kNumBars || slot >= kSlotsPerBar) return;
  SetPresentationEntry(bar * kSlotsPerBar + slot, button);
}

bool ActionAssignmentRuntime::IsBarSlotEmpty(std::size_t bar, std::size_t slot) const {
  if (bar >= kNumBars || slot >= kSlotsPerBar) return true;
  return GetPresentationEntry(bar * kSlotsPerBar + slot).IsEmpty();
}

std::size_t ActionAssignmentRuntime::GetNonEmptyCount() const {
  std::size_t count = 0;
  for (const auto& action : assignments_.values()) {
    if (!action.empty()) ++count;
  }
  return count;
}

std::size_t ActionAssignmentRuntime::GetNonEmptyCountForBar(std::size_t bar) const {
  if (bar >= kNumBars) return 0;
  std::size_t count = 0;
  std::size_t base = bar * kSlotsPerBar;
  for (std::size_t i = 0; i < kSlotsPerBar; ++i) {
    if (!GetPresentationEntry(base + i).IsEmpty()) ++count;
  }
  return count;
}

}
