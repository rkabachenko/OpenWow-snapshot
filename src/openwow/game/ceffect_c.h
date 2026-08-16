#pragma once

#include "openwow/game/object_guid.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace openwow::data::dbc {
struct SpellVisualEffectNameEntry;
struct SpellVisualKitEntry;
}

namespace openwow::render::m2 {
class M2System;
class M2TransparentDrawOrder;
struct M2RenderFrameResult;

enum class M2RenderPassScope : std::uint8_t;
}

namespace openwow::game {

class CGObject_C;
class CGUnit_C;
class CMissileNode_C;
class ObjectManager;
class WorldSession;

namespace CEffectFlags {

constexpr std::uint32_t kSoundModeOne = 0x00000001u;
constexpr std::uint32_t kUseOwnerTransform = 0x00000002u;
constexpr std::uint32_t kRestoreOwnerAnimationOnEnd = 0x00000004u;
constexpr std::uint32_t kPendingDestroy = 0x00000020u;
constexpr std::uint32_t kChannelVisual = 0x00000040u;
constexpr std::uint32_t kTeardownQueued = 0x00000100u;
constexpr std::uint32_t kScaleFromOwner = 0x00000200u;
constexpr std::uint32_t kSuppressKitSound = 0x00000400u;
constexpr std::uint32_t kInitialized = 0x00000800u;
constexpr std::uint32_t kPlayingAnimation = 0x00001000u;
constexpr std::uint32_t kStandalone = 0x00002000u;
constexpr std::uint32_t kContinuouslyTrackOwner = 0x00004000u;
constexpr std::uint32_t kUseExplicitFacing = 0x00008000u;
constexpr std::uint32_t kHasExplicitLocalPosition = 0x00010000u;
constexpr std::uint32_t kPersistent = 0x00020000u;
constexpr std::uint32_t kEmitterCountdown = 0x00040000u;
constexpr std::uint32_t kObjectItemVisual = 0x00080000u;
constexpr std::uint32_t kTrackSoundLifetime = 0x00100000u;
constexpr std::uint32_t kDoNotBindSoundToOwner = 0x00200000u;
constexpr std::uint32_t kAttachedModelSelector = 0x02000000u;

constexpr std::uint32_t kInitializationGate =
    kTeardownQueued | kInitialized;
constexpr std::uint32_t kChannelTeardownMask =
    kChannelVisual | kPlayingAnimation | kStandalone;
}

enum class CEffectLifecycle : std::uint8_t {
  kPendingInitialization,
  kActive,
  kTearingDown,
};

struct CEffectCreateInfo {
  CGObject_C* owner{nullptr};
  ObjectGuid source_guid{};
  std::uint32_t spell_id{0};
  const data::dbc::SpellVisualKitEntry* visual_kit{nullptr};
  const data::dbc::SpellVisualEffectNameEntry* effect_name{nullptr};

  std::uint32_t visual_kit_id{0};
  std::uint32_t effect_name_id{0};

  std::uint32_t resource_id{0};
  std::int32_t attachment_point{-1};
  std::uint32_t flags{0};
  std::uint32_t visual_kit_param{0};
  std::uintptr_t transform_key{0};
  std::uint32_t cleanup_tick{0};
  const std::array<float, 3>* position{nullptr};
  std::array<float, 3> local_offset{};
  std::array<float, 3> local_rotation_degrees{};
  bool world_space{false};
};

struct CEffectSnapshot {
  std::uint64_t effect_id{0};
  ObjectGuid owner_guid{};
  ObjectGuid source_guid{};
  std::uint32_t spell_id{0};
  std::uint32_t flags{0};
  std::uint32_t effect_name_id{0};
  std::uint32_t visual_kit_id{0};
  std::uint32_t resource_id{0};
  std::int32_t attachment_point{-1};
  std::uint32_t visual_kit_param{0};
  std::uintptr_t transform_key{0};
  CEffectLifecycle lifecycle{CEffectLifecycle::kPendingInitialization};
};

class CEffect_C {
 public:
  explicit CEffect_C(openwow::render::m2::M2System& m2_system,
                    ObjectManager& object_manager,
                    const WorldSession& session)
      : m2_system_(m2_system), object_manager_(object_manager),
        session_(session) {}
  virtual ~CEffect_C() = default;
  CEffect_C(const CEffect_C&) = delete;
  CEffect_C& operator=(const CEffect_C&) = delete;
  CEffect_C(CEffect_C&&) = delete;
  CEffect_C& operator=(CEffect_C&&) = delete;

  [[nodiscard]] bool Update(std::uint32_t frame_tick_ms);
  void BeginTeardown();

  void ReleaseOwnerReference();
  void IncrementOwnerRefCount() noexcept { ++owner_reference_count_; }
  void SetOwnerRefCount(std::uint32_t count) noexcept {
    owner_reference_count_ = count;
  }
  [[nodiscard]] std::uint32_t GetOwnerRefCount() const noexcept {
    return owner_reference_count_;
  }

  [[nodiscard]] CMissileNode_C* UnlinkFromObjectList() noexcept;
  void LinkToObject(CMissileNode_C** target_list_head) noexcept;
  [[nodiscard]] CMissileNode_C* GetNextAttachedEffect() const noexcept {
    return owner_list_next_node_;
  }
  [[nodiscard]] bool IsLinkedToObject() const noexcept {
    return owner_list_prev_slot_ != nullptr;
  }

  [[nodiscard]] static CMissileNode_C* AddEffect(
      const WorldSession& session, const CEffectCreateInfo& info);

  [[nodiscard]] static CMissileNode_C* AddLogicalEffect(
      const WorldSession& session, CGObject_C& owner,
      const CEffectSnapshot& state);

  static void UpdateAll(std::uint32_t frame_tick_ms);

  [[nodiscard]] static render::m2::M2RenderFrameResult RenderAll(
      std::uint16_t view_id, const float* view_matrix,
      render::m2::M2RenderPassScope pass_scope,
      render::m2::M2TransparentDrawOrder* transparent_draw_order);
  static void ProcessTeardownList();
  static void DetachAllFromOwner(ObjectGuid owner_guid);
  static void Shutdown();
  [[nodiscard]] static std::size_t CountAttached(
      const CGObject_C& owner, bool include_tearing_down = false) noexcept;

  [[nodiscard]] bool MatchesReplacement(
      std::uint32_t effect_name_id,
      std::int32_t attachment_point,
      std::uint32_t spell_id,
      std::uint32_t visual_kit_id,
      std::uintptr_t transform_key) const noexcept;

  [[nodiscard]] CEffectSnapshot Snapshot() const noexcept;
  [[nodiscard]] CEffectLifecycle GetLifecycle() const noexcept {
    return lifecycle_;
  }
  [[nodiscard]] bool IsTornDown() const noexcept {
    return lifecycle_ == CEffectLifecycle::kTearingDown;
  }

  [[nodiscard]] std::uint32_t GetFlags() const noexcept { return flags_; }
  void SetFlags(std::uint32_t flags) noexcept { flags_ = flags; }
  void AddFlags(std::uint32_t flags) noexcept { flags_ |= flags; }
  void ClearFlags(std::uint32_t flags) noexcept { flags_ &= ~flags; }
  void EnableAttachedModelSelector();
  void RefreshOwnerScale();

  void RefreshOwnerAttachmentTransform();

  [[nodiscard]] std::uint32_t GetSpellId() const noexcept { return spell_id_; }
  void SetSpellId(std::uint32_t spell_id) noexcept { spell_id_ = spell_id; }
  [[nodiscard]] std::int32_t GetAttachmentPoint() const noexcept {
    return attachment_point_;
  }
  [[nodiscard]] std::uint32_t GetVisualKitId() const noexcept;
  [[nodiscard]] std::uint32_t GetEffectNameId() const noexcept;
  [[nodiscard]] std::uint32_t GetResourceId() const noexcept {
    return resource_id_;
  }
  [[nodiscard]] std::uint32_t GetVisualKitParam() const noexcept {
    return visual_kit_param_;
  }
  [[nodiscard]] ObjectGuid GetAttachmentOwnerGuid() const noexcept {
    return attachment_owner_guid_;
  }
  [[nodiscard]] std::uint64_t GetSourceGuid() const noexcept {
    return (static_cast<std::uint64_t>(source_guid_high_) << 32u) |
           source_guid_low_;
  }

  [[nodiscard]] const std::array<float, 3>& GetPosition() const noexcept {
    return resolved_position_;
  }
  void SetPosition(const std::array<float, 3>& position) noexcept;

  void SetActivationTick(std::uint32_t tick) noexcept { activation_tick_ = tick; }
  void SetTimestampCleanup(std::uint32_t tick) noexcept { cleanup_tick_ = tick; }
  void SetTimestampAlphaFade(std::uint32_t tick) noexcept {
    owner_alpha_restore_tick_ = tick;
  }

  void ConfigureOwnerAlphaRestore(std::uint32_t tick,
                                  std::uint32_t duration_ms) noexcept;
  static void CancelOwnerAlphaRestore(ObjectGuid owner_guid);
  [[nodiscard]] std::uint32_t GetTimestampCleanup() const noexcept {
    return cleanup_tick_;
  }
  [[nodiscard]] std::uint32_t GetTimestampAlphaFade() const noexcept {
    return owner_alpha_restore_tick_;
  }

  void SetFadeoutCounter(std::int32_t count) noexcept {
    emitter_countdown_ = count;
  }
  [[nodiscard]] std::int32_t GetFadeoutCounter() const noexcept {
    return emitter_countdown_;
  }
  void DisableEffectEmittersImmediately();
  void EnableEffectEmittersForTwoUpdates();

  [[nodiscard]] bool HasModel() const noexcept {
    return primary_model_instance_id_ != 0u;
  }
  [[nodiscard]] std::uint32_t GetModelInstanceId() const noexcept {
    return primary_model_instance_id_;
  }
  [[nodiscard]] float GetModelAlpha() const noexcept { return model_alpha_; }
  [[nodiscard]] std::uint32_t GetSoundHandleId() const noexcept {
    return impact_sound_handle_id_;
  }
  [[nodiscard]] std::uint32_t GetSoundKitId() const noexcept {
    return sound_kit_id_;
  }

  void BuildWorldTransform(float* out_matrix4x4,
                           const CGUnit_C* parent_unit) const;

  [[nodiscard]] ObjectManager& owner_object_manager() const noexcept {
    return object_manager_;
  }

 protected:
  friend class CMissileNode_C;
  [[nodiscard]] openwow::render::m2::M2System& m2_system() const noexcept {
    return m2_system_;
  }
  [[nodiscard]] ObjectManager& object_manager() const noexcept {
    return object_manager_;
  }
  [[nodiscard]] const WorldSession& session() const noexcept { return session_; }

  std::uint32_t source_guid_low_{0};
  std::uint32_t source_guid_high_{0};
  std::uint32_t triggered_event_guid_low_{0};
  std::uint32_t triggered_event_guid_high_{0};
  std::uint32_t timestamp_start_{0};
  std::uint32_t timestamp_end_{0};
  std::uint32_t move_event_type_{0};
  std::uint32_t flags_{0};
  std::uint32_t follow_guid_low_{0};
  std::uint32_t follow_guid_high_{0};
  std::uint32_t spell_id_{0};
  std::uint32_t sequence_counter_{0};
  std::uint32_t primary_model_visual_state_{0};
  std::uint32_t impact_sound_distance_{0};
  std::uint32_t impact_sound_context_value_{0};
  CMissileNode_C** owner_list_prev_slot_{nullptr};
  CMissileNode_C* owner_list_next_node_{nullptr};
  float impact_pos_[3]{0.0f, 0.0f, 0.0f};
  std::uint32_t impact_sound_handle_id_{0};
  std::uint32_t primary_model_instance_id_{0};
  float primary_model_scale_{1.0f};

 private:
  openwow::render::m2::M2System& m2_system_;
  ObjectManager& object_manager_;
  const WorldSession& session_;
  [[nodiscard]] bool ComputeAttachmentPosition(
      std::array<float, 3>& out_position,
      std::array<float, 3>& out_orientation);
  void PlayEffectSound();
  void RunFirstFrameSoundModelUpdate();
  void TrackOwnerTransform();
  void SetEffectEmittersEnabled(bool enabled);
  void StopEffectSound();
  [[nodiscard]] bool RestoreOwnerAlpha();

  std::uint64_t effect_id_{0};
  ObjectGuid attachment_owner_guid_{};
  std::uint32_t visual_kit_id_{0};
  std::uint32_t effect_name_id_{0};
  std::uint32_t resource_id_{0};
  std::int32_t attachment_point_{-1};
  std::uint32_t visual_kit_param_{0};
  std::uintptr_t transform_key_{0};
  std::array<float, 3> effect_position_{};
  std::array<float, 3> resolved_position_{};
  std::array<float, 3> local_offset_{};
  std::array<float, 3> local_rotation_degrees_{};
  std::uint32_t activation_tick_{0};
  std::uint32_t cleanup_tick_{0};
  std::uint32_t owner_alpha_restore_tick_{0};
  std::uint32_t owner_alpha_restore_duration_ms_{0};
  std::int32_t emitter_countdown_{0};
  std::uint32_t owner_reference_count_{0};
  std::uint32_t sound_kit_id_{0};
  float effect_record_scale_{1.0f};
  float effect_record_min_scale_{0.0f};
  float effect_record_max_scale_{0.0f};
  float explicit_facing_radians_{0.0f};
  float model_alpha_{1.0f};
  CEffectLifecycle lifecycle_{CEffectLifecycle::kPendingInitialization};
  bool world_space_{false};
  bool owner_alpha_restore_inclusive_{false};
};

}
