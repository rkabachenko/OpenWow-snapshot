#pragma once

#include "openwow/game/object_guid.h"
#include "openwow/game/unit_defines.h"

#include <array>
#include <cstdint>

namespace openwow::game {

class CGCorpse_C;
class CGGameObject_C;
class CGUnit_C;
class ObjectManager;
class TargetingSystem;
class WorldSession;

inline constexpr std::uint32_t kAutoAttackTypeIdle = 13u;

class UnitInteractionRuntime {
public:
  explicit UnitInteractionRuntime(CGUnit_C &owner) noexcept : owner_(owner) {}

  void RightClickInteract(WorldSession *session, TargetingSystem *targeting) const;
  void CompleteRightClickInteraction(WorldSession &session) const;
  [[nodiscard]] std::uint32_t DetermineCursorInteractionBits(std::uint32_t filter) const;
  [[nodiscard]] bool IsSelectableOrOwnedPet(const WorldSession &session) const;
  [[nodiscard]] bool GetInteractionRangeSquared(const WorldSession &session,
                                                ObjectGuid target_guid,
                                                int action_type,
                                                float *out_range_sq) const;
  void SetCachedInteractRange(float range) noexcept;

  [[nodiscard]] ReactionType GetReaction(const CGUnit_C &other) const;

  struct ReactionMemo {
    bool resolved{false};
    ReactionType reaction{ReactionType::kNeutral};
  };
  [[nodiscard]] ReactionType GetReaction(const CGUnit_C &other,
                                         ReactionMemo &memo) const;
  [[nodiscard]] bool IsFriendlyTo(const CGUnit_C &other) const;
  [[nodiscard]] bool IsHostileTo(const CGUnit_C &other) const;
  [[nodiscard]] bool IsNeutralOrCivilian(const CGUnit_C &other) const;
  [[nodiscard]] int GetCorpseReactionLevel(const CGCorpse_C &corpse) const;
  [[nodiscard]] bool IsFriendlyCorpseTarget(const CGCorpse_C &corpse) const;
  [[nodiscard]] bool IsNeutralGameObjectTarget(const CGGameObject_C &object) const;
  [[nodiscard]] bool IsPlayerControlled() const;
  [[nodiscard]] bool MatchesImmediateControllerGuid(ObjectGuid guid) const;
  [[nodiscard]] ObjectGuid GetControllingPlayerGuid() const;
  [[nodiscard]] CGUnit_C *ResolveControllingPlayer() const;
  [[nodiscard]] bool IsInSamePartyOrControlledParty(const CGUnit_C &other) const;
  [[nodiscard]] bool IsInSameRaidOrControlledRaid(const CGUnit_C &other) const;

  struct GroupRelation {
    bool same_party{false};
    bool same_raid{false};
  };
  [[nodiscard]] GroupRelation ResolveGroupRelation(const CGUnit_C &other) const;
  [[nodiscard]] bool CanAssistSpellTarget(const CGUnit_C &target,
                                          bool ignore_flag_check) const;
  [[nodiscard]] bool CanAssistSpellTarget(const CGUnit_C &target,
                                          bool ignore_flag_check,
                                          ReactionMemo &memo) const;
  [[nodiscard]] bool CanAttackSpellTarget(const CGUnit_C &target) const;
  [[nodiscard]] bool CanAttackSpellTarget(const CGUnit_C &target,
                                          ReactionMemo &memo) const;
  [[nodiscard]] bool CanInitiateAutoAttack(const CGUnit_C &target) const;
  [[nodiscard]] bool CanInteractWithFriendlyPlayerTarget(const CGUnit_C &target) const;
  [[nodiscard]] bool IsAttackingOrLatched() const;
  [[nodiscard]] bool IsSpellClickAccessible() const;
  [[nodiscard]] bool IsInMeleeRange(const CGUnit_C &other) const;
  [[nodiscard]] bool IsControlledPet() const;

  void RefreshFactionDependentState(WorldSession &session,
                                    bool refresh_linked_visible_units) const;
  void RefreshLinkedVisibleUnitFactionState() const;
  void HandleAliveStateTransition(WorldSession &session,
                                  bool suppress_player_alive_event);
  void CancelSpellCastsOnUnitDeath(WorldSession &session);
  void HandleDeathStateTransition(WorldSession &session);

  void ApplyActivePlayerDeathSideEffects(WorldSession &session);

  void DrainAttachedEffectNodesForDeath();
  void OnNPCInteractionFlagsChanged(WorldSession &session,
                                    std::uint32_t new_flags);

  [[nodiscard]] bool IsAutoAttacking() const noexcept;
  [[nodiscard]] bool IsActivePlayerAutoAttacking() const;
  [[nodiscard]] std::uint32_t AutoAttackType() const noexcept;
  [[nodiscard]] ObjectGuid AutoAttackTarget() const noexcept;
  [[nodiscard]] const std::array<float, 3> &AutoAttackTargetPosition() const noexcept;
  void BeginAutoAttack(std::uint32_t type, ObjectGuid target,
                       std::array<float, 3> target_position,
                       float facing) noexcept;
  void CompleteAutoAttackInteraction(bool stop_facing, bool send_stop);
  [[nodiscard]] bool MatchesActiveMoverInteractionMask(
      std::uint32_t interaction_mask) const;
  [[nodiscard]] bool SpellResumesAutoAttackOnCompletion(
      std::uint32_t spell_id) const;
  void RebaseAutoAttackForTransportChange(const float *matrix4x4,
                                          float facing_delta);
  void HandleMovementArrival();
  void CancelAutoAttackAndCheckLootClose(WorldSession &session,
                                         bool cancel_auto_attack,
                                         bool skip_loot_check);
  void SetCachedUpdateTarget(ObjectGuid target) noexcept;
  [[nodiscard]] bool HasCachedUpdateTarget() const noexcept;
  void ProcessPendingInteraction();

  [[nodiscard]] bool CurrentShapeshiftFormRequiresTurnSensitiveUse() const;
  [[nodiscard]] bool IsInCancelableShapeshiftForm() const;
  [[nodiscard]] bool SuppressesAttackActionShapeshiftAutoCancel() const;
  [[nodiscard]] bool CanMoveInCurrentForm() const;
  [[nodiscard]] bool CanAutoCancelShapeshiftFormForAction() const;

private:
  CGUnit_C &owner_;
  std::uint32_t auto_attack_type_{kAutoAttackTypeIdle};
  ObjectGuid auto_attack_target_;
  std::array<float, 3> auto_attack_target_position_{};
  float auto_attack_facing_{0.0f};
  ObjectGuid pending_follow_target_;
  ObjectGuid pending_object_interact_target_;
  ObjectGuid pending_loot_target_;
  ObjectGuid pending_spell_target_;
  ObjectGuid pending_attack_target_;
  ObjectGuid cached_update_target_guid_;
  bool death_state_active_{false};
  std::uint32_t cached_npc_interaction_flags_{0};
  static inline float cached_interact_range_{0.0f};
};

}
