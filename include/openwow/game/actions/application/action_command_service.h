#pragma once

#include "openwow/game/actions/model/action_assignments.h"

#include <cstdint>

namespace openwow::game::actions {

enum class ActionMutationSource : std::uint8_t {
  kServer,
  kPickup,
  kPlace,
  kInternal,
};

enum class ActionMutationResult : std::uint8_t {
  kApplied,
  kUnchanged,
  kProtected,
  kServerSyncPending,
};

class ActionReferenceBookkeeping {
 public:
  virtual ~ActionReferenceBookkeeping() = default;
  virtual void BeforeAssignmentChanges(ActionSlot slot,
                                       const Action& previous,
                                       const Action& replacement) = 0;
};

class ActionStateRecomputer {
 public:
  virtual ~ActionStateRecomputer() = default;
  virtual void Recompute(ActionSlot slot, const Action& action) = 0;
};

class ActionTransport {
 public:
  virtual ~ActionTransport() = default;
  virtual void SendAssignment(ActionSlot slot, const Action& action) = 0;
};

class ActionEvents {
 public:
  virtual ~ActionEvents() = default;
  virtual void AssignmentChanged(std::uint16_t lua_slot) = 0;
};

class ActionProtectionPolicy {
 public:
  virtual ~ActionProtectionPolicy() = default;
  [[nodiscard]] virtual bool CanMutate(ActionSlot slot,
                                       ActionMutationSource source) const = 0;
};

class RetailActionProtectionPolicy final : public ActionProtectionPolicy {
 public:
  [[nodiscard]] bool CanMutate(ActionSlot slot,
                               ActionMutationSource source) const override;
};

class ActionResolver {
 public:
  virtual ~ActionResolver() = default;
  [[nodiscard]] virtual std::uint32_t ResolveSpellLike(
      const Action& action) const = 0;
  [[nodiscard]] virtual std::uint32_t ResolveItem(
      const Action& action) const = 0;
};

struct ActionMutation {
  ActionSlot slot;
  Action replacement;
  ActionMutationSource source{ActionMutationSource::kInternal};
  bool notify_server{false};
  bool notify_ui{true};
};

class ActionCommandService {
 public:
  ActionCommandService(ActionAssignments& assignments,
                       ActionReferenceBookkeeping& bookkeeping,
                       ActionStateRecomputer& recomputer,
                       ActionTransport& transport,
                       ActionEvents& events,
                       const ActionProtectionPolicy& protection) noexcept;

  [[nodiscard]] ActionMutationResult Apply(const ActionMutation& mutation);

 private:
  ActionAssignments& assignments_;
  ActionReferenceBookkeeping& bookkeeping_;
  ActionStateRecomputer& recomputer_;
  ActionTransport& transport_;
  ActionEvents& events_;
  const ActionProtectionPolicy& protection_;
};

}
