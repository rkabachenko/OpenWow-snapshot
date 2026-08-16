#pragma once

#include "openwow/game/actions/macros/model/macro_id.h"
#include "openwow/game/actions/model/action_assignments.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace openwow::game {

class MacroCatalog;

using ActionPresentationKind = actions::ActionKind;

struct ActionPresentationEntry {
  std::uint32_t action{0};
  ActionPresentationKind type{ActionPresentationKind::kSpell};

  [[nodiscard]] bool IsEmpty() const { return action == 0 && type == ActionPresentationKind::kSpell; }
  bool operator==(const ActionPresentationEntry&) const = default;

  [[nodiscard]] actions::Action ToAssignedAction() const;
  [[nodiscard]] static ActionPresentationEntry FromAssignedAction(
      const actions::Action& action);
};

struct ActionButtonUsabilityState {
  bool is_usable{false};
  bool not_enough_power{false};

  [[nodiscard]] bool operator==(const ActionButtonUsabilityState& other) const {
    return is_usable == other.is_usable &&
           not_enough_power == other.not_enough_power;
  }

  [[nodiscard]] bool operator!=(const ActionButtonUsabilityState& other) const {
    return !(*this == other);
  }
};

using ActionBarState = actions::ActionAssignmentSyncState;

class ActionAssignmentRuntime {
 public:
  static constexpr std::size_t kMaxActionButtons = 144;
  using ItemSpellResolver = std::function<std::uint32_t(std::uint32_t item_id)>;

  explicit ActionAssignmentRuntime(
      MacroCatalog& macros, ItemSpellResolver item_spell_resolver = {});
  ~ActionAssignmentRuntime();

  void BeginServerSync();
  void ApplyServerSnapshot(
      ActionBarState state,
      const actions::ActionAssignments::Storage& assignments);

  using SlotValidator = std::function<bool(const ActionPresentationEntry&, std::uint32_t)>;
  void SetSlotValidator(SlotValidator validator);

  [[nodiscard]] ActionPresentationEntry GetPresentationEntry(std::size_t slot) const;

  [[nodiscard]] std::uint32_t ResolveSpellLikeActionId(std::size_t slot) const;
  [[nodiscard]] bool SlotHasMacroReference(std::size_t slot) const;
  [[nodiscard]] std::uint32_t GetMacroReferenceId(std::size_t slot) const;
  [[nodiscard]] std::vector<actions::macros::MacroId>
  CollectMacroReferenceIds() const;
  [[nodiscard]] std::vector<actions::ActionSlot> ClearMacroReferences(
      actions::macros::MacroId macro_id);
  void SetPresentationEntry(std::size_t slot, const ActionPresentationEntry& button);
  void ClearAssignment(std::size_t slot);
  void ClearAll();

  [[nodiscard]] std::vector<std::size_t> ReplaceSpellActionReferences(
      std::uint32_t old_spell_id,
      std::uint32_t new_spell_id);

  [[nodiscard]] std::vector<std::size_t> ClearSpellActionReferences(
      std::uint32_t spell_id);
  [[nodiscard]] const ActionButtonUsabilityState& GetUsabilityState(
      std::size_t slot) const;
  [[nodiscard]] bool UpdateUsabilityState(
      std::size_t slot,
      const ActionButtonUsabilityState& state);
  void ResetUsabilityStates();

  static constexpr std::size_t kSlotsPerBar = 12;
  static constexpr std::size_t kNumBars = kMaxActionButtons / kSlotsPerBar;

  [[nodiscard]] ActionPresentationEntry GetBarSlot(std::size_t bar, std::size_t slot) const;
  void SetBarSlot(std::size_t bar, std::size_t slot, const ActionPresentationEntry& button);
  [[nodiscard]] bool IsBarSlotEmpty(std::size_t bar, std::size_t slot) const;
  [[nodiscard]] std::size_t GetNonEmptyCount() const;
  [[nodiscard]] std::size_t GetNonEmptyCountForBar(std::size_t bar) const;

  [[nodiscard]] ActionBarState last_state() const {
    return static_cast<ActionBarState>(assignments_.sync_state());
  }
  [[nodiscard]] bool IsServerSyncPending() const {
    return assignments_.server_sync_pending();
  }
  [[nodiscard]] std::uint64_t revision() const noexcept {
    return assignments_.revision();
  }
  [[nodiscard]] const actions::ActionAssignments& assignments() const noexcept {
    return assignments_;
  }

 private:
  actions::ActionAssignments assignments_;
  std::array<ActionButtonUsabilityState, kMaxActionButtons> usability_states_{};
  SlotValidator slot_validator_;
  ItemSpellResolver item_spell_resolver_;
  MacroCatalog& macros_;
};

}
