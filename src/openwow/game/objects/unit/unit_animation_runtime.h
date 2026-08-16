#pragma once

#include "openwow/game/character_animation.h"
#include "openwow/game/objects/unit/unit_dance.h"
#include "openwow/render/m2/m2_public_types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace openwow::data::dbc {
class DbcLoader;
struct EmotesEntry;
}

namespace openwow::world {
class WorldCamera;
}

namespace openwow::game {

class CGUnit_C;
class ObjectManager;
class WorldSession;

[[nodiscard]] CharacterLocomotionState BuildLocomotionState(
    const CGUnit_C &unit, std::uint32_t movement_flags, bool dead,
    std::uint32_t emote_internal_flags);

inline constexpr std::uint32_t kConstructedEmoteInternalFlags = 0x70u;

inline constexpr std::uint16_t kNoAnimationRow = 0xFFFFu;

class UnitAnimationRuntime final {
public:

  struct PendingProtectedPlayback {
    std::uint16_t animation_id{0};
    bool looping{false};
    bool bypass_alias_resolution{false};
  };

  struct PlaybackRequest {
    std::uint16_t animation_id{0};
    bool looping{true};

    bool bypass_alias_resolution{false};
    std::uint64_t serial{1};

    std::uint16_t base_animation_id{kNoAnimationRow};
    bool base_looping{true};

    bool base_bypass_alias_resolution{false};

    bool upper_body_only{false};

    bool zero_blend{false};

    [[nodiscard]] bool operator==(const PlaybackRequest &) const = default;
  };

  explicit UnitAnimationRuntime(CGUnit_C &owner) noexcept : owner_(owner) {}
  UnitAnimationRuntime(const UnitAnimationRuntime &) = delete;
  UnitAnimationRuntime &operator=(const UnitAnimationRuntime &) = delete;
  UnitAnimationRuntime(UnitAnimationRuntime &&) = delete;
  UnitAnimationRuntime &operator=(UnitAnimationRuntime &&) = delete;

  [[nodiscard]] UnitDanceComponent &Dance() noexcept { return dance_; }
  [[nodiscard]] const UnitDanceComponent &Dance() const noexcept { return dance_; }

  static void AssignAnimationSlotsByFlags(
      const data::dbc::DbcLoader &dbc_loader);
  static void ClearAnimationSlots();
  static void AnimationEventCallback(
      WorldSession &session, ObjectManager &objects, std::uint64_t unit_guid,
      std::uint32_t event_type, std::uint32_t fourcc, std::int32_t event_data,
      const float *position, std::int32_t bone_index);

  void ProcessGroundContactAnimationEvent(const WorldSession &session,
                                          const float *position,
                                          bool right_side);
  void HandleAnimationEvent(WorldSession &session, std::uint32_t event_type,
                            std::uint32_t fourcc, std::int32_t event_data,
                            const float *position, std::int32_t bone_index);
  void ApplyValuesUpdateSessionEffects(WorldSession &session,
                                       std::uint8_t previous_stand_state,
                                       bool stand_state_changed,
                                       bool emote_state_changed);
  void TrySetStandStateAndNotifyServer(WorldSession &session,
                                       std::uint8_t stand_state);
  void MaybeStandUpIfPlayer(WorldSession &session, std::uint8_t stand_state);
  [[nodiscard]] bool ShouldUseTargetFrame(const WorldSession &session) const;
  [[nodiscard]] bool ShouldUseHoverStandAnimation(
      const WorldSession &session) const;
  [[nodiscard]] bool HasTurnDrivenStandSelectorGate(
      const WorldSession &session) const;
  bool TryReplaceAnimSlotWithHoverStand(std::int32_t &inout_anim_id,
                                        std::int32_t &inout_sub_anim_id);

  void SeedCachedSheatheStateFromDescriptor();
  void TransitionWeaponSheatheState();
  void PlayWeaponSheatheAnimation(std::int32_t old_state);
  [[nodiscard]] render::m2::M2OperationSummary SetAnimationRecursive(
      std::uint32_t instance_id, std::int32_t anim_group,
      std::uint32_t anim_id, std::int32_t sub_variant, std::int32_t loop,
      float speed, std::int32_t blend_in, std::int32_t blend_out, bool force);
  int StopAnimAndPropagateToPassengers(bool clear_primary_channel, bool force);
  [[nodiscard]] bool PlayMainhandSheatheAnimation();
  [[nodiscard]] bool PlayOffhandSheatheAnimation();
  void ChangeSheatheStateAndNotifyServer(std::int32_t new_state, bool animate,
                                         bool silent);
  void SetIdleWeaponEnchantVisuals(bool enabled);

  void SelectEmoteAnimation(bool suppress_sheathe_update);

  void TryPlaySpeechEmoteSlot(std::uint32_t slot_index);
  void SendTextEmote(const std::uint32_t *emote_entry,
                     std::uint64_t target_guid);
  [[nodiscard]] std::uint32_t GetEmoteState() const;
  [[nodiscard]] std::uint8_t GetStandState() const;
  [[nodiscard]] std::uint8_t GetShapeshiftForm() const;
  [[nodiscard]] bool EmoteStateCheck(
      std::uint16_t animation_flags,
      std::uint32_t *out_animation_id = nullptr) const;
  [[nodiscard]] std::optional<std::uint16_t> GetCurrentAnimationId() const;
  [[nodiscard]] const PlaybackRequest &GetPlaybackRequest() const noexcept {
    return playback_request_;
  }

  [[nodiscard]] std::uint16_t GetResolvedPlaybackAnimationId() const;

  [[nodiscard]] std::uint16_t GetResolvedBasePlaybackAnimationId() const;

  void HandleMovementAnimation(std::uint32_t previous_movement_flags,
                               std::uint32_t current_movement_flags,
                               bool suppress_land_animation = false);

  void PlayAttackAnimation(std::uint32_t hit_info, std::uint32_t melee_spell_id);

  void PlayWoundReaction(const WorldSession &session, bool critical);

  void ApplyAttackerStateRecordToVictim(const WorldSession &session,
                                        std::uint32_t hit_info);

  void PlayMeleeContactReaction(std::uint8_t victim_state, std::uint32_t damage);
  void QueueCombatAudioResult(std::uint64_t victim_guid,
                              std::uint32_t hit_info,
                              std::uint32_t damage,
                              std::uint32_t overkill,
                              std::uint8_t victim_state);
  void HandlePlaybackCompletion(const WorldSession &session,
                                std::uint64_t request_serial,
                                std::uint16_t animation_id);
  [[nodiscard]] bool IsPlayingUsingAnimation() const;
  [[nodiscard]] std::uint32_t ResolveAnimationId(
      std::uint32_t anim_id, std::uint32_t override_instance_id = 0) const;
  void TryPlayPendingFallAnimation();
  [[nodiscard]] bool IsAnimationUpdateSuppressed() const;
  [[nodiscard]] bool IsRangedAttackOrSitSleepBehavior(
      std::uint32_t animation_id) const;

  [[nodiscard]] bool AnimationSequenceLoops(std::uint32_t animation_id) const;
  [[nodiscard]] bool IsLoopingCombatOrReadyStanceBehavior(
      std::uint32_t animation_id) const;
  [[nodiscard]] bool IsEmoteDance(std::uint32_t animation_id) const;

  [[nodiscard]] bool IsRestPoseStaleForFlags(std::uint32_t movement_flags) const;
  [[nodiscard]] bool IsEmoteTalk(std::uint32_t animation_id) const;
  void UpdateMountAndPassengerAnimations();
  void ApplySplineAnimationTier(std::uint8_t tier);
  void UpdatePendingFallAnimation(std::uint32_t previous_movement_flags,
                                  std::uint32_t current_movement_flags);
  void PlayDeadTransitionAnimation(const WorldSession &session,
                                   bool force_replay);
  void ApplySpellVisualKitAnimation(const WorldSession &session,
                                    std::uint32_t kit_id,
                                    std::uint32_t dispatch_type,
                                    std::uint32_t spell_id = 0u);

  [[nodiscard]] bool HasStandSelectionInteractionState() const;
  void ApplyRequestedStandState(WorldSession &session, std::uint8_t stand_state);
  void RefreshSpellVisualStandAnimationState(const WorldSession &session);
  void EndSpellVisualStandAnimation(const WorldSession &session);
  void ResetAuraAnimationVisualState(const WorldSession &session);
  void RestoreStandAnimationAfterEffect(const WorldSession &session);
  void SetMovementAnimData(std::uint8_t alpha, std::uint32_t anim_id,
                           std::int32_t start, std::int32_t duration,
                           std::int32_t flags);
  void SetLootTargetAndPlayLootAnimation(std::uint64_t target_guid);
  void SetStandSelectionInteractionTarget(std::uint64_t target_guid);
  void ClearStandSelectionInteractionTargetAndRefresh(
      const WorldSession &session);
  void UpdateStandAnimation(const WorldSession &session,
                            std::int32_t anim_group,
                            std::uint32_t requested_animation_id);

  void RunPendingStandSelectorRefresh(const WorldSession &session);

  void RequestStandSelectorRefresh() noexcept {
    stand_selector_refresh_pending_ = true;
  }
  void HandleMovementOpcodeAnimationSideEffects(
      const WorldSession &session, std::uint32_t movement_opcode);
  void PlayEmoteAnimation(std::int32_t emote_anim_id,
                          std::uint32_t animation_flags);
  void PlayEmoteOnUnit(std::int32_t emote_dbc_id);

  void EmoteSequencePlayer();
  void RefreshSelectedStandAnimation(const WorldSession &session,
                                     std::uint32_t animation_flags,
                                     std::uint32_t selector_flags);
  void ClearSelectedStandAnimationState();
  void HandleAnimSequenceEnd(const WorldSession &session,
                             std::uint32_t animation_group,
                             std::uint32_t animation_id,
                             std::uint32_t emote_state, bool has_remaining);
  void ResetEmoteState();
  void ResetDeathPlaybackForAliveTransition(WorldSession &session);
  void EmoteQueueHandler(const std::uint32_t *emote_pairs, std::int32_t count);
  [[nodiscard]] bool HasQueuedEmote(std::uint32_t emote_id) const noexcept {
    for (std::size_t index = 1; index < std::size(emote_slots_); index += 2) {
      if (emote_slots_[index] == emote_id) return true;
    }
    return false;
  }
  [[nodiscard]] bool IsLootTargetAnimatable() const;

  [[nodiscard]] bool IsEmoteAnimationStateBlocked() const;

  [[nodiscard]] bool CanPlayEmoteAnimationNow(const WorldSession &session) const;

  [[nodiscard]] std::int32_t GetCachedSheatheState() const noexcept {
    return sheathe_state_;
  }
  void SetAutoRepeatActive(bool active) noexcept;
  [[nodiscard]] bool IsAutoRepeatActive() const noexcept;
  void SetChannelingActionLock(bool locked) noexcept;
  [[nodiscard]] bool HasChannelingActionLock() const noexcept;
  [[nodiscard]] std::uint32_t GetEmoteInternalFlags() const noexcept {
    return emote_internal_flags_;
  }
  [[nodiscard]] bool HasSpellVisualAnimationLatch() const noexcept {
    return spell_visual_persist_anim_id_ != -1;
  }
  void ClearEmoteInternalFlags(std::uint32_t flags) noexcept {
    emote_internal_flags_ &= ~flags;
  }
  void ResetInternalEmoteStorage() noexcept;
  [[nodiscard]] std::optional<std::uint16_t>
  GetSelectedStandAnimationId() const noexcept {
    return selected_stand_animation_id_;
  }
  [[nodiscard]] std::uint32_t GetSelectedStandAnimationFlags() const noexcept {
    return selected_stand_animation_flags_;
  }
  [[nodiscard]] std::int32_t GetCurrentAnimationGroup() const noexcept {
    return current_anim_group_;
  }
  [[nodiscard]] std::int32_t GetAnimationBoneIndex() const noexcept {
    return animation_bone_index_;
  }
  [[nodiscard]] bool AreIdleWeaponEnchantVisualsEnabled() const noexcept {
    return idle_weapon_item_visuals_enabled_;
  }
  [[nodiscard]] std::uint64_t StandSelectionInteractionTargetGuid() const noexcept {
    return stand_selection_interaction_target_guid_;
  }
  void SetAnimationBoneAvailability(bool has_bone_4, bool has_bone_6) noexcept;

  static constexpr std::uint32_t kEmoteFlagTurnInPlaceLeft = 0x800u;
  static constexpr std::uint32_t kEmoteFlagTurnInPlaceRight = 0x1000u;

  void SetBodyYawTurnLatches(bool left, bool right) noexcept;
  [[nodiscard]] bool UpdateCachedAnimationTier(std::uint8_t tier) noexcept;

private:
  struct PendingCombatAudioResult {
    std::uint64_t victim_guid{0};
    std::uint32_t hit_info{0};
    std::uint32_t damage{0};
    std::uint32_t overkill{0};
    std::uint8_t victim_state{0};
    bool active{false};
  };

  struct SpellVisualStandAnimationRecord {
    std::uint16_t animation_id{0};
    std::uint32_t kit_flags{0};
  };

  enum class SequenceEndFollowUpKind {
    kRunSelector,
    kPlayRow,
    kRawSequence,
    kIdleResolver,
    kNothing,
  };
  struct SequenceEndFollowUp {
    SequenceEndFollowUpKind kind{SequenceEndFollowUpKind::kRunSelector};
    std::uint16_t animation_id{0};
  };
  [[nodiscard]] SequenceEndFollowUp ResolveSequenceEndFollowUp(
      std::uint32_t finished_behavior_id) const;

  [[nodiscard]] bool IsUpperBodyOnlyAnimation(
      std::uint32_t incoming_animation_id,
      std::uint32_t current_animation_id) const;

  [[nodiscard]] bool IsAirborneForAnimationSplit() const;

  [[nodiscard]] bool IsAnimationResolutionModelReady(
      std::uint32_t target_instance_id) const;

  [[nodiscard]] std::uint32_t WalkAnimationDataFallback(
      std::uint32_t anim_id, std::uint32_t target_instance_id) const;

  void SetSelectedStandAnimationState(std::uint16_t animation_id,
                                      std::uint32_t animation_flags);
  void ApplySelectedStandAnimation(std::uint16_t animation_id,
                                   std::uint32_t animation_flags);
  void ApplyRequestedStandStateSideEffects(WorldSession &session,
                                           std::uint8_t stand_state);
  [[nodiscard]] bool ShouldSuppressStandStateTransitionAnimation() const;
  void RefreshCameraBoundModelDisplayIfTargeted(
      openwow::world::WorldCamera *camera) const;
  void HandleStandStateTransition(WorldSession &session,
                                  std::uint8_t previous_stand_state);
  [[nodiscard]] bool HasActiveSpellVisualStandAnimationSource() const;

  [[nodiscard]] std::uint32_t ResolveSelectorMovementFlags() const;

  [[nodiscard]] bool ResolveDirectionalLocomotionAnimation(
      std::uint32_t movement_flags, std::uint32_t selector_flags,
      std::uint16_t *out_animation_id) const;

  [[nodiscard]] bool ResolveTurnInPlaceStandAnimation(
      const WorldSession &session, std::uint32_t selector_flags,
      std::uint16_t *out_animation_id) const;

  [[nodiscard]] bool ResolveIdleStandAnimation(
      const WorldSession &session, std::uint16_t *out_animation_id,
      bool keep_settle = true) const;

  bool ResolveLootStandAnimationOverride(
      const WorldSession &session, std::uint32_t selector_flags,
      std::uint16_t *out_animation_id) const;

  bool TryResolveCachedTargetStandAnimation(
      std::uint32_t selector_flags, std::uint32_t *out_animation_id,
      const std::uint8_t *anim_flags_ptr) const;

  bool ResolveRangedAutoRepeatStandAnimation(
      std::uint32_t selector_flags, std::uint16_t *out_animation_id) const;
  [[nodiscard]] std::uint32_t GetWeaponBasedReadyAnimationId() const;

  [[nodiscard]] std::optional<std::uint16_t>
  GetWeaponBasedParryAnimationId() const;
  [[nodiscard]] std::optional<SpellVisualStandAnimationRecord>
  ResolveActiveSpellVisualStandAnimationRecord() const;
  bool ResolveStandStateTransitionAnimationOverride(
      std::uint32_t selector_flags, std::uint16_t *out_animation_id) const;
  bool ResolveSpellVisualStandAnimationOverride(
      std::uint32_t selector_flags, std::uint32_t *inout_animation_flags,
      std::uint16_t *out_animation_id) const;
  [[nodiscard]] bool HasMovementDrivenStandAnimationOverride(
      const WorldSession &session) const;
  bool ApplyMovementDrivenStandAnimationOverride(const WorldSession &session);
  [[nodiscard]] const data::dbc::EmotesEntry *
  LookupEmoteStateEntry(std::uint32_t emote_state) const;
  [[nodiscard]] const data::dbc::EmotesEntry *
  LookupQueuedEmoteRow(std::uint32_t emote_id) const;
  [[nodiscard]] std::uint32_t ResolveAnimationDurationMs(
      std::uint32_t animation_id) const;
  [[nodiscard]] std::uint32_t ResolveStandAnimationRequestId() const;
  bool ResolveStandStateCategoryAnimation(const WorldSession &session,
                                          std::uint16_t *out_animation_id);

  bool RequestPlayback(std::uint16_t animation_id, bool looping,
                       bool restart = false,
                       bool bypass_alias_resolution = false);

  void SubmitRawPlayback(std::uint16_t animation_id, bool looping,
                         bool upper_body_only, bool zero_blend);

  void CommitPlaybackRequest(std::uint16_t animation_id, bool looping,
                             bool upper_body_only, bool bypass_alias_resolution,
                             bool zero_blend);

  void ClearSequenceEndFlagBits(std::uint32_t finished_behavior,
                                std::uint16_t finished_animation_id);

  void ApplySubmitFunnelFlagBits(std::uint32_t submitted_behavior);

  [[nodiscard]] static bool IsMountModelBehavior(std::uint32_t behavior) noexcept;
  void HandleCombatAudioAnimationEvent(WorldSession &session,
                                       std::uint32_t fourcc,
                                       const float *position);

  CGUnit_C &owner_;
  UnitDanceComponent dance_;
  bool idle_weapon_item_visuals_enabled_{false};
  std::uint64_t stand_selection_interaction_target_guid_{0};
  std::uint8_t move_anim_alpha_{0};
  std::uint32_t move_anim_id_{0};
  std::uint32_t move_anim_timestamp_{0};
  std::uint32_t move_anim_end_time_{0};
  std::int32_t move_anim_flags_{0};

  std::uint32_t emote_slots_[8]{0u, 0xFFFFFFFFu};

  std::uint32_t emote_internal_flags_{kConstructedEmoteInternalFlags};
  std::int32_t prev_sheathe_state_{0};
  std::int32_t sheathe_state_{0};

  std::uint8_t cached_anim_tier_{0};

  std::int32_t current_anim_group_{-1};
  std::int32_t animation_bone_index_{-1};

  std::uint8_t previous_stand_state_category_{0u};

  std::optional<std::uint16_t> selected_stand_animation_id_;
  std::optional<std::uint16_t> previous_selected_stand_animation_id_;

  std::int32_t pending_deferred_animation_id_{-1};

  std::int32_t deferred_animation_satisfied_id_{-1};
  std::uint32_t selected_stand_animation_flags_{0};
  std::int32_t spell_visual_persist_anim_id_{-1};
  PlaybackRequest playback_request_{};
  std::optional<PendingProtectedPlayback> pending_protected_playback_{};
  std::uint32_t playback_movement_flags_{0};

  bool stand_selector_refresh_pending_{false};
  bool death_transition_played_{false};
  PendingCombatAudioResult pending_combat_audio_{};

  mutable std::uint32_t animation_model_ready_instance_id_{0};

  struct ResolvedAnimationMemoEntry {
    bool valid{false};
    std::uint32_t anim_id{0};
    std::uint32_t target_instance_id{0};
    const data::dbc::DbcLoader *dbc{nullptr};
    std::uint8_t tier{0};
    std::uint32_t result{0};
  };

  static constexpr std::size_t kResolvedAnimationMemoSlotCount = 8u;
  static_assert((kResolvedAnimationMemoSlotCount &
                 (kResolvedAnimationMemoSlotCount - 1u)) == 0u,
                "direct-mapped index uses a power-of-two mask");
  mutable std::array<ResolvedAnimationMemoEntry, kResolvedAnimationMemoSlotCount>
      resolved_animation_memo_{};

  static constexpr std::uint32_t kEmoteFlagHasAnimationBone4 = 0x80u;
  static constexpr std::uint32_t kEmoteFlagHasAnimationBone6 = 0x100u;
  static constexpr std::uint32_t kEmoteFlagAutoRepeatActive = 0x200u;
  static constexpr std::uint32_t kEmoteFlagChannelingActionLock = 0x400u;
  static constexpr std::uint32_t kWeaponTransitionMainhand = 0x00100000u;
  static constexpr std::uint32_t kWeaponTransitionOffhand = 0x00200000u;
  static constexpr std::uint32_t kWeaponTransitionMask =
      kWeaponTransitionMainhand | kWeaponTransitionOffhand;
};

}
