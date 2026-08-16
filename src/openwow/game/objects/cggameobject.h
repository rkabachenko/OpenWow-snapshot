#pragma once

#include "openwow/game/objects/cgobject.h"
#include "openwow/game/mo_transport_path_state.h"
#include "openwow/game/passenger_movement.h"
#include "openwow/game/player_name_desc.h"
#include "openwow/game/world_session_fwd.h"
#include "openwow/render/resources/render_asset_readiness.h"

#include <array>
#include <cstdint>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace openwow::data::dbc {
struct LockEntry;
}

namespace openwow::world {
class WorldCamera;
}

namespace openwow::game {

class PlayerInventoryReplica;

enum class GameObjectType : std::uint8_t {
  Door                 = 0,
  Button               = 1,
  QuestGiver           = 2,
  Chest                = 3,
  Binder               = 4,
  Generic              = 5,
  Trap                 = 6,
  Chair                = 7,
  SpellFocus           = 8,
  Text                 = 9,
  Goober               = 10,
  Transport            = 11,
  AreaDamage           = 12,
  Camera               = 13,
  MapObject            = 14,
  MOTransport          = 15,
  DuelArbiter          = 16,
  FishingNode          = 17,
  Ritual               = 18,
  Mailbox              = 19,
  DoNotUse             = 20,
  GuardPost            = 21,
  SpellCaster          = 22,
  MeetingStone         = 23,
  FlagStand            = 24,
  FishingHole          = 25,
  FlagDrop             = 26,
  MiniGame             = 27,
  DoNotUse2            = 28,
  CapturePoint         = 29,
  AuraGenerator        = 30,
  DungeonDifficulty    = 31,
  BarberChair          = 32,
  DestructibleBuilding = 33,
  GuildBank            = 34,
  Trapdoor             = 35,
};

inline constexpr std::uint8_t kMaxGameObjectType = 36;

[[nodiscard]] inline constexpr bool IsGuidStampedTransportModelGameObject(
    const GameObjectType type) {
  return type == GameObjectType::Transport ||
         type == GameObjectType::MOTransport ||
         type == GameObjectType::Trapdoor;
}

enum class GOState : std::uint8_t {
  Active            = 0,
  Ready             = 1,
  ActiveAlternative = 2,
};

struct WorldModelPlacementMetadata {
  bool wait_for_loaded_model{false};
  std::uint32_t lookup_key{0};
  std::uint8_t doodad_set_count{0};
  std::array<std::uint16_t, 3> doodad_sets{};
  std::optional<float> fallback_half_extent{};
};

struct GameObjectModelLoadMetadata {
  std::uint32_t owner_guid_low{0};
  std::uint32_t owner_guid_high{0};

  std::uint32_t loader_arg3{0};
  std::uint32_t loader_arg4{0};
  std::uint32_t loader_arg5{0};

  WorldModelPlacementMetadata world_model{};
};

struct GameObjectLoadedModelState {
  std::string display_path;
  std::array<float, 3> world_position{};
  float facing{0.0f};
  GameObjectModelLoadMetadata metadata{};
  bool is_wmo{false};
  WorldModelPlacementMetadata world_model{};
  bool transport_flag_enabled{false};
  bool destructible_proxy_current_state_enabled{false};
  std::optional<std::uint16_t> animation_request_id{};
};

enum GameObjectFlags : std::uint32_t {
  GO_FLAG_IN_USE         = 0x00000001,
  GO_FLAG_LOCKED         = 0x00000002,
  GO_FLAG_INTERACT_COND  = 0x00000004,
  GO_FLAG_TRANSPORT      = 0x00000008,
  GO_FLAG_NOT_SELECTABLE = 0x00000010,
  GO_FLAG_NODESPAWN      = 0x00000020,
  GO_FLAG_TRIGGERED      = 0x00000040,
  GO_FLAG_ANIM_CUSTOM_REPEAT = 0x00000080,

  GO_FLAG_DAMAGED        = 0x00000200,
  GO_FLAG_DESTROYED      = 0x00000400,
};

enum GameObjectDynFlags : std::uint16_t {
  GO_DYNFLAG_LO_ACTIVATE   = 0x0001,
  GO_DYNFLAG_LO_ANIMATE    = 0x0002,
  GO_DYNFLAG_LO_NO_INTERACT= 0x0004,
  GO_DYNFLAG_LO_SPARKLE    = 0x0008,
};

class CGPlayer_C;
class CGUnit_C;
class CObjectEffect;
class SpellCastRuntime;
struct GameObjectDisplayInfoEntry;
struct GameObjectTemplateInfo;

struct GameObjectAttachmentNode {

  std::uint64_t parent_guid{0};

  PassengerPose pose{};

  std::uint64_t target_guid{0};

  static constexpr std::uint32_t kAutoPlayParticleBit = 0x1;
  std::uint32_t flags{0};

  static constexpr std::uint32_t kParticleStopFlag = 0x08000000u;
  std::uint32_t movement_flags{0};

  bool is_transport_passenger{false};

  [[nodiscard]] bool HasAutoPlayParticle() const {
    return (flags & kAutoPlayParticleBit) != 0;
  }
  void SetParticleStopFlag() { movement_flags |= kParticleStopFlag; }
  void ClearParticleStopFlag() { movement_flags &= ~kParticleStopFlag; }
  [[nodiscard]] bool HasParticleStopFlag() const {
    return (movement_flags & kParticleStopFlag) != 0;
  }

  void ResetParentToWorld(const ObjectManager& objects);
};

[[nodiscard]] float GetDefaultInteractDistance(GameObjectType type);

class CGGameObject_C : public CGObject_C {
 public:
  explicit CGGameObject_C(PlayerInventoryReplica& inventory);
  CGGameObject_C(PlayerInventoryReplica& inventory, ObjectGuid guid);
  CGGameObject_C(ObjectManager& objects, PlayerInventoryReplica& inventory,
                 ObjectGuid guid);
  ~CGGameObject_C() override;

  [[nodiscard]] const data::dbc::LockEntry* GetLockEntry() const;

  void PrepareForWorldRemoval() override;

  std::vector<std::uint16_t> ApplyCreateUpdate(
      const CreateObjectUpdate& upd) override;
  void FinalizeCreateUpdate(const CreateObjectUpdate& upd) override;
  void FinalizeWorldPublication() override;
  void FinalizePacketUpdatePromotion() override;
  bool ApplyMovementUpdate(const MovementOnlyUpdate& upd) override;
  bool UpdateModelNodeTransform(float dt, std::uint32_t current_tick_ms) override;

  void SynchronizeModelSpatialBounds(const std::array<float, 6>& world_bounds);

  void SynchronizeRenderAssetReadiness(bool ready);

  void SynchronizeMOTransportModelReadiness(bool ready);

  std::vector<std::uint16_t> ApplyValuesUpdate(const ValuesUpdate& upd) override;

  [[nodiscard]] float GetModelOpacity() const override;

  [[nodiscard]] bool CanBeTransportParent() const override;

  void AttachOverlayModelToBone() override;

  [[nodiscard]] ObjectGuid GetCreatedBy() const;

  [[nodiscard]] std::uint32_t GetDisplayId() const override;

  [[nodiscard]] std::uint32_t GetFlags() const;

  [[nodiscard]] std::uint32_t GetFaction() const;

  [[nodiscard]] int GetReactionLevel(const CGUnit_C &unit) const;

  [[nodiscard]] std::uint32_t GetLevel() const override;

  [[nodiscard]] std::uint32_t GetDynamic() const;

  [[nodiscard]] Position GetRawPosition() const;

  [[nodiscard]] Position GetNamePlatePosition() const override;

  void SynchronizeDestructibleNameplateModelHeight(
      std::optional<float> height) noexcept;

  void SynchronizeModelLocalBounds(
      const std::optional<std::array<float, 6>>& local_bounds);

  void SynchronizeModelConvexVolumePlanes(
      std::optional<std::vector<std::array<float, 4>>> planes);

  [[nodiscard]] bool ContainsLocalPoint(
      const std::array<float, 3>& local_point) const;

  [[nodiscard]] float GetFacing() const override;
  [[nodiscard]] std::tuple<float, float, float, float>
  GetWorldRotation() const override;

  [[nodiscard]] GameObjectType GetGoType() const;
  [[nodiscard]] GOState GetGoState() const;
  [[nodiscard]] std::uint8_t GetGoArtKit() const;
  [[nodiscard]] std::uint8_t GetGoAnimProgress() const;

  [[nodiscard]] std::uint32_t GetGoBytes1() const;

  void SetGoState(GOState state);
  void SetGoAnimProgress(std::uint8_t progress);

  void ApplyTransientGoStateByte(std::uint8_t state_byte);

  void HandleResetStatePacket();

  void HandleServerCustomAnimation(std::uint32_t anim_id);

  void HandleServerDespawnAnimation();

  [[nodiscard]] bool HasFlag(GameObjectFlags flag) const;
  void SetFlag(GameObjectFlags flag);
  void RemoveFlag(GameObjectFlags flag);

  [[nodiscard]] std::uint16_t GetDynFlags() const;
  [[nodiscard]] bool IsActivated() const;
  [[nodiscard]] bool HasQuestSparkle() const;

  struct Rotation {
    float x{0.0f}, y{0.0f}, z{0.0f}, w{0.0f};
  };
  [[nodiscard]] Rotation GetParentRotation() const;

  [[nodiscard]] bool IsDoor() const;
  [[nodiscard]] bool IsButton() const;
  [[nodiscard]] bool IsQuestGiver() const;
  [[nodiscard]] bool IsChest() const;
  [[nodiscard]] bool IsTrap() const;
  [[nodiscard]] bool IsChair() const;
  [[nodiscard]] bool IsSpellFocus() const;
  [[nodiscard]] bool IsGoober() const;
  [[nodiscard]] bool IsTransport() const;
  [[nodiscard]] bool IsMOTransport() const;
  [[nodiscard]] bool IsAnyTransport() const;

  void ApplyTransportSequenceEffect(std::uint32_t sequence_id);

  [[nodiscard]] bool IsFishingNode() const;
  [[nodiscard]] bool IsMailbox() const;
  [[nodiscard]] bool IsMeetingStone() const;
  [[nodiscard]] bool IsFlagStand() const;
  [[nodiscard]] bool IsCapturePoint() const;
  [[nodiscard]] bool IsDestructibleBuilding() const;
  [[nodiscard]] bool IsBarberChair() const;
  [[nodiscard]] bool IsGuildBank() const;

  [[nodiscard]] float GetInteractDistance() const;

  [[nodiscard]] bool ShouldHighlight() const;

  [[nodiscard]] bool IsHighlightableBaseHandler() const;

  [[nodiscard]] bool IsSeatedOnThisChair() const;

  [[nodiscard]] bool IsLocked() const;

  void Interact(WorldSession* session) const;
  void OnRightClickInteract(WorldSession* session,
                            TargetingSystem* targeting) const override;

  void SetTemplateInfo(const GameObjectTemplateInfo* info);
  [[nodiscard]] const GameObjectTemplateInfo* GetTemplateInfo() const {
    return template_info_;
  }

  [[nodiscard]] const char* GetStatsName() const;

  [[nodiscard]] const char* GetStatsCastBarCaption() const;

  [[nodiscard]] const char* GetStatsUnk1Text() const;

  [[nodiscard]] std::uint32_t GetTypeSpecificData(std::uint32_t index) const;

  [[nodiscard]] std::uint32_t GetReadablePageTextId() const;

  [[nodiscard]] std::uint32_t GetReadableLanguageId() const;

  [[nodiscard]] std::uint32_t GetReadablePageMaterialId() const;

  [[nodiscard]] std::uint32_t GetRequiredInstanceMapId() const;

  [[nodiscard]] std::uint32_t GetRequiredInstanceDifficultyIndex() const;

  [[nodiscard]] std::uint32_t GetMeetingStoneAreaId() const;

  [[nodiscard]] std::uint32_t GetTemplateFieldValue(std::uint32_t field_id) const;

  [[nodiscard]] bool IsTemplateFieldZero(std::uint32_t field_id) const;

  [[nodiscard]] bool HasTemplateFieldValue(std::uint32_t field_id) const;

  [[nodiscard]] bool AllowsMountedInteraction() const;

  [[nodiscard]] bool AllowsUseWhileInCombat() const;

  [[nodiscard]] bool HasQuestConditionalOwner() const;

  [[nodiscard]] bool HasFloatingTooltip() const;

  [[nodiscard]] bool IsLevelInRange(std::uint32_t level) const;

  [[nodiscard]] bool PassesMeetingStoneUseGates(const WorldSession &session,
                                                const CGPlayer_C &active_player) const;

  [[nodiscard]] bool IsQuestGiverType() const;

  [[nodiscard]] bool IsSpellFocusTargetEligible(
      const SpellCastRuntime& spell_cast_runtime) const;
  [[nodiscard]] bool MeetsSpellFocusConditions(std::uint32_t spell_id,
                                                 std::uint32_t focus_id) const;

  [[nodiscard]] bool PassesPlayerRequirementTypeGate(
      const WorldSession& session) const;

  [[nodiscard]] bool PlayerMeetsRequirements(const WorldSession& session) const;

  [[nodiscard]] int GetCursorType(const WorldSession& session) const;

  [[nodiscard]] std::string_view GetLockTypeCursorStem() const;

  [[nodiscard]] bool GetCustomCursorPath(const WorldSession& session,
                                         char *buffer,
                                         std::uint32_t buffer_size) const;

  [[nodiscard]] std::uint32_t GetInteractionValue(std::uint16_t flags) const;

  [[nodiscard]] static const char* GetTypeName(GameObjectType type);

  [[nodiscard]] std::string GetTooltipText() const;

  struct LootArtVisualControlState {
    bool requested{false};
    bool quest_sparkle{false};
    bool tracked_resource_match{false};
    std::uint32_t effect_id{0};
    std::uint32_t sync_serial{0};
  };

  [[nodiscard]] const LootArtVisualControlState& GetLootArtVisualControlState() const {
    return loot_art_visual_control_;
  }

  void RefreshLootArtVisualControlState();

  struct ModelAnimationControlState {
    std::uint8_t request_code{0};
    std::uint16_t animation_id{0};
    std::uint32_t sync_serial{0};
  };

  [[nodiscard]] const ModelAnimationControlState& GetModelAnimationControlState() const {
    return model_animation_control_;
  }

  static constexpr std::int8_t kGoAnimStateInvalid = -1;

  static constexpr std::int8_t kGoAnimStateDoorSolid = 1;

  static constexpr std::array<std::uint16_t, 13> kGoAnimIdByStateIndex = {
      145,
      147,
      148,
      149,
      146,
      150,
      151,
      152,
      153,
      154,
      155,
      156,
      157,
  };

  struct M2GoAnimationControlState {

    std::int8_t state_index{kGoAnimStateInvalid};

    std::int8_t previous_state_index{kGoAnimStateInvalid};

    std::uint16_t animation_id{0};

    bool uses_direct_animation_id{false};
    std::uint32_t direct_animation_id{0};

    bool looping{false};

    float playback_speed{1.0f};

    bool has_progress_override{false};

    bool use_sequence_repeat_count{false};

    std::uint32_t sync_serial{0};
  };

  [[nodiscard]] const M2GoAnimationControlState&
  GetM2GoAnimationControlState() const {
    return m2_go_animation_control_;
  }

  [[nodiscard]] std::optional<MOTransportMovePhase>
  GetMOTransportMovePhase() const {
    return mo_transport_published_move_phase_;
  }

  [[nodiscard]] std::uint32_t MapMOTransportMovementTimestamp(
      std::uint32_t absolute_timestamp_ms) const;

  [[nodiscard]] static std::int8_t ResolveGoAnimAutoAdvanceTarget(
      std::int8_t current_state_index);

  [[nodiscard]] static bool IsGoAnimStateLooping(std::int8_t state_index);

  static constexpr std::uint32_t kInvalidAnimCompletionTick = 0xFFFF'FFFFu;

  bool PollAnimCompletionForLifetimeRelease(std::uint32_t current_tick);

  void SetAppliedModelAnimState(std::int8_t state_index);

  void SynchronizeModelAnimationCompletion(std::optional<std::uint32_t> duration_ms,
                                           std::uint32_t current_tick);

  bool AdvanceCompletedModelAnimState(std::uint32_t current_tick);

  void SetAnimCompletionTick(std::uint32_t tick);

  [[nodiscard]] std::int8_t GetAppliedModelAnimState() const {
    return applied_model_anim_state_;
  }
  [[nodiscard]] std::uint32_t GetAnimCompletionTick() const {
    return animation_completion_tick_;
  }

  struct FlagVisualControlState {
    std::uint32_t current_flags{0};
    std::uint32_t changed_mask{0};
    std::uint32_t sync_serial{0};
  };

  [[nodiscard]] const FlagVisualControlState& GetFlagVisualControlState() const {
    return flag_visual_control_;
  }

  struct ArtKitVisualControlState {
    std::uint8_t art_kit{0};
    std::uint32_t sync_serial{0};
  };

  [[nodiscard]] const ArtKitVisualControlState& GetArtKitVisualControlState() const {
    return art_kit_visual_control_;
  }

  struct DestructibleStateVisual {
    std::uint32_t source_display_id{0};
    std::uint32_t render_display_id{0};
    std::uint16_t destruction_or_init_doodad_set{0};
    std::uint16_t impact_effect_doodad_set{0};
    std::uint16_t ambient_doodad_set{0};

    bool operator==(const DestructibleStateVisual&) const = default;
  };

  struct DestructibleVisualControlState {
    bool initialized{false};
    std::uint8_t active_state_index{0};

    std::int8_t previous_active_state_index{-1};
    std::uint32_t active_render_display_id{0};
    std::uint32_t rebuild_effect_display_id{0};
    std::uint32_t rebuild_transition_mode{4};
    std::uint32_t rebuild_transition_speed{0};

    bool impact_effect_enabled{false};
    std::array<DestructibleStateVisual, 4> states{};

    std::uint32_t transition_serial{0};
    std::uint32_t sync_serial{0};
  };

  [[nodiscard]] const DestructibleVisualControlState&
  GetDestructibleVisualControlState() const {
    return destructible_visual_control_;
  }

  [[nodiscard]] std::uint32_t GetRenderDisplayId() const;

  struct TransportPathProgressControlState {
    std::uint16_t path_progress{0};
    std::uint32_t sync_serial{0};
  };

  [[nodiscard]] const TransportPathProgressControlState&
  GetTransportPathProgressControlState() const {
    return transport_path_progress_control_;
  }

  struct DifficultyVisibilityControlState {
    bool visible{true};
    std::uint32_t sync_serial{0};
  };

  [[nodiscard]] const DifficultyVisibilityControlState&
  GetDifficultyVisibilityControlState() const {
    return difficulty_visibility_control_;
  }

  void RefreshDifficultyVisibilityControlState(const WorldSession& session);
  [[nodiscard]] bool IsVisibleForCurrentInstanceDifficulty(
      const WorldSession& session) const;

  void OnGoStateByteChanged(std::uint8_t previous_state_byte);

  [[nodiscard]] bool LoadModel(GameObjectModelLoadMetadata metadata = {});
  [[nodiscard]] const std::optional<GameObjectLoadedModelState>&
  GetLoadedModelState() const noexcept {
    return loaded_model_state_;
  }

  [[nodiscard]] bool PassesInteractionFlagGate() const;

  [[nodiscard]] bool PassesInteractionPointRangeTest(const WorldSession &session) const;

  [[nodiscard]] bool PassesSeatPointRangeTest(const CGPlayer_C &active_player) const;

  bool TryUse(const WorldSession& session, std::uint32_t* error_out,
              float* range_out,
              std::uint32_t* spell_out) const;

  bool OnActivation(WorldSession& session);

  void OnNpcInteractionClosed();

  void InitModelByType();

  void RemoveAttachment(GameObjectAttachmentNode* node, bool stop_particles);

  void ClearAttachmentsForWorldRemoval();

  [[nodiscard]] const std::list<GameObjectAttachmentNode*>&
  GetActiveAttachments() const {
    return active_attachment_list_;
  }

  [[nodiscard]] const std::list<GameObjectAttachmentNode*>&
  GetActiveAttachmentListForTesting() const {
    return active_attachment_list_;
  }
  [[nodiscard]] const std::list<GameObjectAttachmentNode*>&
  GetFreeAttachmentListForTesting() const {
    return free_attachment_list_;
  }

  void AddAttachmentNode(GameObjectAttachmentNode* node);

  void BeginTransportAttachmentDestruction() noexcept {
    transport_attachment_destruction_in_progress_ = true;
  }

  void UpsertTransportPassengerAttachment(std::uint64_t target_guid);
  void RemoveTransportPassengerAttachment(std::uint64_t target_guid);

  void SetTransportContextId(std::uint32_t id) { transport_context_id_ = id; }
  [[nodiscard]] std::uint32_t GetTransportContextId() const {
    return transport_context_id_;
  }

  void InitVisuals();

  void AdvanceMOTransportPathStateForFrame(std::uint64_t frame_stamp);

  bool CheckUseRange(const WorldSession& session, std::uint32_t* error_out,
                     float* range_out,
                     std::uint32_t* spell_out);

  [[nodiscard]] float GetTypeHandlerAnimTime() const override;

  void SetTypeHandlerAnimTime(float value);

  [[nodiscard]] std::uint16_t GetTransportPathProgress() const;

  struct MOTransportAnimationControlState {
    bool enabled{false};
    bool ready_state{false};
    std::uint32_t sync_serial{0};
  };

  [[nodiscard]] const MOTransportAnimationControlState&
  GetMOTransportAnimationControlState() const {
    return mo_transport_animation_control_;
  }

  struct TrapdoorRenderSyncState {
    bool initialized{false};
    std::int32_t committed_state{-1};
  };

  [[nodiscard]] bool UpdateTrapdoorRenderSync(bool asset_ready);
  [[nodiscard]] bool UpdateTrapdoorRenderSync(
      const render::RuntimeRenderAssetReadinessState& readiness) {
    return UpdateTrapdoorRenderSync(render::IsRuntimeRenderAssetReady(readiness));
  }

  [[nodiscard]] const TrapdoorRenderSyncState&
  GetTrapdoorRenderSyncState() const {
    return trapdoor_render_sync_;
  }

  void ResetTrapdoorHandlerCommittedState() {
    trapdoor_render_sync_.committed_state = -1;
    trapdoor_render_sync_.initialized = false;
  }

  [[nodiscard]] bool AttachTrackedPositionalSound(std::uint32_t handle_id);
  void ClearTrackedPositionalSound();
  [[nodiscard]] bool RefreshTrackedPositionalSound();

  [[nodiscard]] bool HandlePrimaryModelSoundEvent(
      openwow::world::WorldCamera* camera, std::uint32_t event_fourcc,
      std::uint32_t data, const float* position);
  [[nodiscard]] std::uint32_t GetTrackedPositionalSoundHandleForTesting() const {
    return tracked_positional_sound_handle_;
  }
  void SetLoadedModelStateForTesting(GameObjectLoadedModelState state =
                                         GameObjectLoadedModelState{}) {
    mo_transport_model_ready_latched_ = false;
    loaded_model_state_ = state;
  }

 private:
  void FinalizeGameObjectCreateState(const CreateObjectUpdate &upd);
  void RefreshTemplateBoundTypeHandlerState();

  bool HandleAnimCompletionHoldRelease();

  void ClearLoadedModelState();
  static std::uint16_t ResolveModelAnimationId(std::uint8_t request_code);
  void QueueModelAnimationRequest(std::uint8_t request_code);
  void RefreshFlagVisualControlState(std::uint32_t previous_flags);
  void RefreshArtKitVisualControlState(std::uint8_t previous_art_kit);
  void RefreshDestructibleVisualControlState(
      std::uint32_t flags_changed_mask = 0u);

  void RefreshPlayerNameDescriptor(bool allow_without_template = false);
  void ReleasePlayerNameDescriptor();
  void RefreshTransportPathProgressControlState(std::uint16_t previous_path_progress);
  void DispatchGoStateByteCallback(std::uint8_t previous_state_byte,
                                   std::uint8_t current_state_byte);

  void OnTrapdoorStateByteChanged(std::uint8_t previous_state_byte,
                                  std::uint8_t current_state_byte);
  [[nodiscard]] bool HasMOTransportAnimationData() const;
  void RefreshMOTransportAnimationControl(std::uint8_t previous_state_byte,
                                          std::uint8_t current_state_byte);

  void SynchronizeMOTransportPathState(bool reseed_from_progress,
                                       bool force_state_reapply = false);
  void AdvanceMOTransportPathState();
  void PropagateMOTransportAttachments(float* parent_matrix);
  void RequestMOTransportMovePhaseAnimation(MOTransportMovePhase phase);

  void BackfillMOTransportPassengers(ObjectManager& objects);

  [[nodiscard]] std::int8_t ResolveGoAnimationStateIndex() const;

  [[nodiscard]] std::int8_t ResolveGoAnimStateForTransition(
      std::uint8_t old_go_state, std::uint8_t new_go_state) const;

  static constexpr std::uint16_t kDynamicAnimProgressNoOverride = 0xFFFFu;
  [[nodiscard]] bool HasDynamicAnimProgressOverride() const;
  void ClearDynamicAnimProgressOverride();

  void TransitionM2GoAnimationState(std::int8_t new_state_index);

  bool TryPlayGoAnimState(std::int8_t state_index);

  [[nodiscard]] std::uint32_t GetLockId() const;
  [[nodiscard]] const data::dbc::LockEntry* LookupLockEntry() const;

  struct LockInteractionInfo {
    std::uint32_t spell_id{0};
    std::uint32_t current_skill{0};
    std::uint32_t required_skill{0};
    std::int16_t  item_bag_slot{-1};
    std::uint32_t lock_slot{0};
  };

  [[nodiscard]] bool GetLockInteractionInfo(
      SpellCastRuntime& spells, LockInteractionInfo* info = nullptr) const;

  [[nodiscard]] bool MeetsTrackedLootArtEligibilityGate(const CGPlayer_C& active_player) const;
  [[nodiscard]] bool EvaluateLootArtVisualRequest(bool* quest_sparkle,
                                                  bool* tracked_resource_match) const;
  [[nodiscard]] static bool LockEntryMatchesTrackResources(
      const data::dbc::LockEntry& lock_entry, std::uint32_t track_resources);

  const GameObjectTemplateInfo* template_info_{nullptr};

  std::uint32_t cached_interaction_value_{0};

  std::optional<GameObjectLoadedModelState> loaded_model_state_{};

  bool mo_transport_model_ready_latched_{false};

  std::uint8_t cached_go_state_byte_{0};

  LootArtVisualControlState loot_art_visual_control_{};
  ModelAnimationControlState model_animation_control_{};
  M2GoAnimationControlState m2_go_animation_control_{};

  std::int8_t applied_model_anim_state_{kGoAnimStateInvalid};

  std::uint32_t animation_completion_tick_{kInvalidAnimCompletionTick};
  FlagVisualControlState flag_visual_control_{};
  ArtKitVisualControlState art_kit_visual_control_{};
  DestructibleVisualControlState destructible_visual_control_{};
  std::optional<float> destructible_nameplate_model_height_{};

  std::optional<std::array<float, 6>> model_local_bounds_{};

  std::optional<std::vector<std::array<float, 4>>> model_convex_volume_planes_{};
  PlayerNameDesc* player_name_desc_{nullptr};
  TransportPathProgressControlState transport_path_progress_control_{};
  DifficultyVisibilityControlState difficulty_visibility_control_{};
  MOTransportAnimationControlState mo_transport_animation_control_{};
  std::optional<MOTransportTimedPathState> mo_transport_path_state_{};

  std::uint32_t mo_transport_handler_timestamp_ms_{0};
  std::optional<MOTransportMovePhase> mo_transport_previous_phase_{};

  std::optional<MOTransportMovePhase> mo_transport_published_move_phase_{};
  std::optional<std::array<float, 3>> mo_transport_previous_position_{};

  std::optional<std::array<float, 3>> mo_transport_previous_path_position_{};
  std::uint32_t mo_transport_last_update_tick_ms_{0};
  bool mo_transport_has_update_tick_{false};

  std::optional<std::uint64_t> mo_transport_advance_frame_stamp_{};
  TrapdoorRenderSyncState trapdoor_render_sync_{};
  std::uint32_t tracked_positional_sound_handle_{0};

  std::uint32_t cached_transport_sequence_effect_state_{0};

  float type_handler_anim_time_{-1.0f};

  std::list<GameObjectAttachmentNode*> active_attachment_list_;

  std::list<GameObjectAttachmentNode*> free_attachment_list_;

  std::list<std::unique_ptr<GameObjectAttachmentNode>>
      transport_attachment_nodes_;

  bool transport_attachment_destruction_in_progress_{false};

  std::uint32_t transport_context_id_{0};

  render::RuntimeRenderAssetReadinessState render_readiness_state_{};
  PlayerInventoryReplica& inventory_;
};

CGGameObject_C* CGGameObject_OnCreate_LinkToObjectMgr(ObjectManager& objects,
                                                       std::uint64_t guid);

}
