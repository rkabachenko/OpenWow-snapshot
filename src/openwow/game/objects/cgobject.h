#pragma once

#include <memory>

#include "openwow/game/cmirror_slot_bucket.h"
#include "openwow/game/movement_info.h"
#include "openwow/game/object_effect_system.h"
#include "openwow/game/object_guid.h"
#include "openwow/game/object_types.h"
#include "openwow/game/update_fields.h"
#include "openwow/game/update_object_parser.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <tuple>
#include <vector>

namespace openwow::render {
class OverlayModelCache;
class WorldFrame;
namespace m2 {
class M2System;
}
}

namespace openwow::data::dbc {
class DbcLoader;
struct MapEntry;
struct MapDifficultyEntry;
struct SpellEntry;
struct SpellVisualEntry;
struct SpellVisualKitEntry;
struct SpellVisualEffectNameEntry;
}

namespace openwow::world {
class WorldCamera;
}
namespace openwow::audio { class SoundRuntime; }

namespace openwow::game {
class WorldEnvironmentState;

class ObjectManager;
class TargetingSystem;
class WorldSession;
class CMissileNode_C;

struct Position {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float facing = 0.0f;
};

enum class OverlayDisplayType : std::uint8_t {
  kNone = 0,
  kType1 = 1,
  kQuestAvailable = 2,
  kQuestAvailableRepeatable = 3,
  kQuestAvailableTrivial = 4,
  kType5 = 5,
  kType6 = 6,
  kType7 = 7,
  kType8 = 8,
  kType9 = 9,
  kType10 = 10,
  kMaxOverlayType = 11
};

inline constexpr std::uint32_t kOverlayTypeToModelIndex[] = {
    0, 6, 1, 2, 3, 5, 10, 11, 4, 9, 9};

inline constexpr std::size_t kOverlayTypeToModelIndexCount =
    sizeof(kOverlayTypeToModelIndex) / sizeof(kOverlayTypeToModelIndex[0]);

inline constexpr std::uint32_t kOverlayModelSlotCount = 12;

inline constexpr std::uint32_t kOverlayModelIndexTaxiEnable = 7;

class CGObject_C {
 public:

  explicit CGObject_C(TypeID type_id = TypeID::kObject);
  CGObject_C(ObjectGuid guid, TypeID type_id);
  explicit CGObject_C(ObjectManager& objects,
                      TypeID type_id = TypeID::kObject);
  CGObject_C(ObjectManager& objects, ObjectGuid guid, TypeID type_id);
  virtual ~CGObject_C();
  [[nodiscard]] ObjectManager* object_manager() const noexcept {
    return object_manager_;
  }
  [[nodiscard]] openwow::audio::SoundRuntime& sound_runtime() const;
  void BindM2System(openwow::render::m2::M2System& m2_system) noexcept {
    m2_system_ = &m2_system;
  }
  [[nodiscard]] openwow::render::m2::M2System* m2_system() const noexcept {
    return m2_system_;
  }
  void BindWorldFrame(openwow::render::WorldFrame* world_frame) noexcept {
    world_frame_ = world_frame;
  }
  [[nodiscard]] openwow::render::WorldFrame* world_frame() const noexcept {
    return world_frame_;
  }
  void BindWorldEnvironmentState(WorldEnvironmentState* world_environment) noexcept {
    world_environment_ = world_environment;
  }
  [[nodiscard]] WorldEnvironmentState* world_environment() const noexcept {
    return world_environment_;
  }

  CGObject_C(const CGObject_C&) = delete;
  CGObject_C& operator=(const CGObject_C&) = delete;

  CGObject_C(CGObject_C&&) noexcept = default;
  CGObject_C& operator=(CGObject_C&&) noexcept = default;

  [[nodiscard]] ObjectGuid GetGuid() const { return guid_; }

  [[nodiscard]] bool IsActiveMover() const;
  [[nodiscard]] TypeID GetTypeId() const { return type_id_; }
  [[nodiscard]] std::uint16_t GetTypeMask() const {
    return TypeMaskFor(type_id_);
  }

  [[nodiscard]] float GetX() const { return position_.GetX(); }
  [[nodiscard]] float GetY() const { return position_.GetY(); }
  [[nodiscard]] float GetZ() const { return position_.GetZ(); }
  [[nodiscard]] float GetOrientation() const { return position_.GetO(); }

  [[nodiscard]] Position GetPosition() const;

  [[nodiscard]] Position GetRawPosition() const;
  [[nodiscard]] double GetSquaredDistanceToPosition(const Position& position) const;
  [[nodiscard]] const MovementUpdate& GetMovementUpdate() const {
    return position_;
  }
  [[nodiscard]] const MovementInfo& GetMovementInfo() const {
    return position_.movement;
  }

  [[nodiscard]] std::uint32_t GetObjectTimeOffsetMs() const {
    return object_time_offset_ms_;
  }

  void SetMovementInfo(const MovementInfo& info) {
    position_.update_flags |= static_cast<std::uint16_t>(kUpdateFlagLiving);
    position_.movement = info;
  }

  [[nodiscard]] float GetDistance(const CGObject_C& other) const {
    float dx = GetX() - other.GetX();
    float dy = GetY() - other.GetY();
    float dz = GetZ() - other.GetZ();
    return std::sqrt(dx * dx + dy * dy + dz * dz);
  }

  [[nodiscard]] float GetDistance2D(const CGObject_C& other) const {
    float dx = GetX() - other.GetX();
    float dy = GetY() - other.GetY();
    return std::sqrt(dx * dx + dy * dy);
  }

  [[nodiscard]] std::uint32_t GetUInt32(std::uint16_t index) const {
    return index < fields_.size() ? fields_[index] : 0u;
  }
  [[nodiscard]] float GetFloat(std::uint16_t index) const {
    const std::uint32_t raw = GetUInt32(index);
    float value;
    std::memcpy(&value, &raw, sizeof(value));
    return value;
  }
  [[nodiscard]] std::uint64_t GetUInt64(std::uint16_t index) const {
    const std::uint32_t lo = GetUInt32(index);
    const std::uint32_t hi = GetUInt32(static_cast<std::uint16_t>(index + 1));
    return static_cast<std::uint64_t>(lo) |
           (static_cast<std::uint64_t>(hi) << 32);
  }
  [[nodiscard]] ObjectGuid GetGuidField(std::uint16_t index) const {
    return ObjectGuid(GetUInt64(index));
  }

  [[nodiscard]] std::uint16_t GetFieldCount() const {
    return FieldCountFor(type_id_);
  }

  void SetPrimaryM2InstanceId(std::uint32_t instance_id) {
    if (primary_m2_instance_id_ != instance_id) {
      overlay_bone_attached_ = false;
      overlay_bone_rotation_compensated_ = false;
    }
    primary_m2_instance_id_ = instance_id;
  }
  [[nodiscard]] std::uint32_t GetPrimaryM2InstanceId() const {
    return primary_m2_instance_id_;
  }

  [[nodiscard]] std::uint32_t ExchangePrimaryM2InstanceId(
      std::uint32_t new_instance_id) {
    const auto old = primary_m2_instance_id_;
    SetPrimaryM2InstanceId(new_instance_id);
    return old;
  }

  [[nodiscard]] bool IsDirty(std::uint16_t field_index) const {
    const std::uint32_t block = field_index / 32;
    const std::uint32_t bit = field_index % 32;
    return block < dirty_mask_.size() && (dirty_mask_[block] & (1u << bit));
  }

  void ClearDirtyFlags() {
    std::fill(dirty_mask_.begin(), dirty_mask_.end(), 0u);
  }

  [[nodiscard]] bool HasAnyDirtyField() const {
    for (auto block : dirty_mask_) {
      if (block != 0) return true;
    }
    return false;
  }

  [[nodiscard]] std::uint32_t GetEntry() const {
    return GetUInt32(OBJECT_FIELD_ENTRY);
  }

  [[nodiscard]] float GetScale() const {
    return native_scale_ * GetFloat(OBJECT_FIELD_SCALE_X);
  }

  void SetNativeScale(float s) { native_scale_ = s; }
  [[nodiscard]] float GetNativeScale() const { return native_scale_; }

  void SetDisplayScale(float scale, openwow::world::WorldCamera* camera);

  [[nodiscard]] std::uint32_t GetDisplayScaleUpdateTick() const {
    return display_scale_update_tick_;
  }
  [[nodiscard]] float GetDisplayScaleStored() const {
    return display_scale_stored_;
  }

  void GetObjectBoundingBox(float* out_bbox) const;
  void SetObjectBoundingBox(const float* bbox);
  void ClearObjectBoundingBox();
  [[nodiscard]] bool HasObjectBoundingBox() const {
    return has_object_bounding_box_;
  }

  void SetModelBoundingBoxHeight(float h) { model_bounding_box_height_ = h; }
  [[nodiscard]] float GetModelBoundingBoxHeight() const {
    return model_bounding_box_height_;
  }

  [[nodiscard]] float GetNameplateHeight() const;

  [[nodiscard]] virtual Position GetNamePlatePosition() const;

  [[nodiscard]] virtual std::uint32_t GetDisplayId() const;
  [[nodiscard]] virtual std::uint32_t GetFactionTemplate() const { return 0u; }
  [[nodiscard]] virtual const char* GetPortraitTextureName() const {
    return nullptr;
  }
  [[nodiscard]] virtual std::uint32_t GetHealth() const;
  [[nodiscard]] virtual std::uint32_t GetMaxHealth() const;
  [[nodiscard]] virtual std::uint32_t GetLevel() const;
  [[nodiscard]] std::string GetName() const { return name_; }

  [[nodiscard]] CMissileNode_C** GetEffectNodeListHeadSlot() {
    return &effect_node_list_head_;
  }
  [[nodiscard]] CMissileNode_C* const* GetEffectNodeListHeadSlot() const {
    return &effect_node_list_head_;
  }

  CObjectEffect& EnsureObjectEffect() {
    if (!object_effect_) {
      object_effect_ = std::make_unique<CObjectEffect>(sound_runtime());
    }
    return *object_effect_;
  }

  [[nodiscard]] CObjectEffect* GetObjectEffect() const {
    return object_effect_.get();
  }

  [[nodiscard]] bool BindObjectEffectPackage(std::uint32_t package_id);

  [[nodiscard]] virtual float GetTypeHandlerAnimTime() const { return -1.0f; }
  void ClearObjectEffectPackage();

  [[nodiscard]] std::uint16_t GetLifetimeHoldCount() const { return lifetime_hold_count_; }
  [[nodiscard]] bool HasLifetimeHolds() const { return lifetime_hold_count_ != 0; }
  [[nodiscard]] bool IsPendingRemoval() const { return pending_removal_; }
  void SetPendingRemoval(const bool pending) { pending_removal_ = pending; }
  [[nodiscard]] std::uint16_t AdjustLifetimeHold(bool acquire);

  virtual void PrepareForWorldRemoval();

  virtual void Show(bool fade = true);

  virtual void Cleanup(int ) {}

  void ClearAllSlotBucketArrays();

  virtual std::uint32_t UpdateOverlayModel();

  virtual void AttachOverlayModelToBone();

  virtual void RefreshOverlayBoneScale();

  void SetOverlayDisplayType(OverlayDisplayType type) {
    overlay_display_type_ = type;
  }
  [[nodiscard]] OverlayDisplayType GetOverlayDisplayType() const {
    return overlay_display_type_;
  }

  void SetQuestGiverIconStatus(OverlayDisplayType new_type);

  void ClearOverlayModelImmediate();

  void SetOverlayModelIndexOverride(std::uint32_t index) {
    overlay_model_index_override_ = index;
  }
  [[nodiscard]] std::uint32_t GetOverlayModelIndexOverride() const {
    return overlay_model_index_override_;
  }

  [[nodiscard]] std::uint32_t GetActiveOverlayModelIndex() const {
    return active_overlay_model_index_;
  }

  [[nodiscard]] float GetOverlayModelScale() const {
    return overlay_model_scale_;
  }

  [[nodiscard]] bool IsOverlayModelVisible() const {
    return overlay_model_visible_;
  }

  [[nodiscard]] bool IsOverlayBoneAttached() const {
    return overlay_bone_attached_;
  }

  [[nodiscard]] bool IsOverlayBoneRotationCompensated() const {
    return overlay_bone_rotation_compensated_;
  }

  virtual void SetIdleAnimation();

  [[nodiscard]] std::uint16_t GetOverlayAnimationId() const {
    return overlay_animation_id_;
  }

  void SetRenderObjectSleepFlag(bool sleeping) {
    render_object_has_sleep_flag_ = sleeping;
  }
  [[nodiscard]] bool HasRenderObjectSleepFlag() const {
    return render_object_has_sleep_flag_;
  }

  virtual const data::dbc::SpellVisualEntry* ResolveSpellVisualRecord(
      const data::dbc::SpellEntry& spell,
      data::dbc::SpellVisualEntry& out,
      std::uint32_t kit_visual_id,
      std::uint32_t kit_visual_id_fallback) const;

  virtual void ResetMatchingSpellVisualNodes(
      const WorldSession&,
      [[maybe_unused]] std::uint32_t spell_id,
      [[maybe_unused]] std::uint32_t visual_kit_param) {}

  virtual void CreateWeaponSpellVisualEffects(
      [[maybe_unused]] const WorldSession& session,
      [[maybe_unused]] std::uint32_t spell_id,
      [[maybe_unused]] const data::dbc::SpellVisualKitEntry& kit,
      [[maybe_unused]] const float* position,
      [[maybe_unused]] std::uint64_t source_guid,
      [[maybe_unused]] std::uint32_t group_param,
      [[maybe_unused]] std::uint32_t& dispatch_flags,
      [[maybe_unused]] std::uintptr_t callback,
      [[maybe_unused]] bool& out_created,
      [[maybe_unused]] bool has_aura_visual_flag) {}

  static constexpr std::uint32_t kEffectFlagAuraVisual    = 0x40000u;
  static constexpr std::uint32_t kEffectFlagCreated       = 0x400u;
  static constexpr std::uint32_t kEffectFlagClearMask     = 0x411u;
  static constexpr std::uint32_t kKitFlagAttachedModelSelector = 0x200u;
  static constexpr std::uint32_t kNodeFlagAttachedModelSelector = 0x2000000u;

  struct EffectNodeCreationResult {
    bool created{false};
    std::uint64_t effect_id{0};
    std::uint32_t node_spell_id{0};
    std::uint32_t node_flags{0};
    std::uint64_t resolved_source_guid{0};
    std::uint32_t updated_dispatch_flags{0};
    bool model_hidden{false};
    bool attached_model_selector_flag_set{false};
  };

  EffectNodeCreationResult CreateSpellVisualEffectNode(
      const WorldSession& session,
      std::uint32_t attachment_point,
      std::uint32_t cleanup_tick,
      std::uint32_t spell_id,
      const data::dbc::SpellVisualKitEntry* kit,
      const data::dbc::SpellVisualEffectNameEntry* effect_name,
      std::uint32_t& dispatch_flags,
      std::uintptr_t animation_callback,
      const float* position,
      std::uint64_t source_guid,
      std::uint32_t visual_kit_param,
      std::uintptr_t transform_key,
      const std::array<float, 3>* local_offset = nullptr,
      const std::array<float, 3>* local_rotation_degrees = nullptr,
      bool world_space = false);

  [[nodiscard]] virtual bool CanBeTransportParent() const { return false; }

  [[nodiscard]] virtual bool IsItemVisible() const { return false; }

  [[nodiscard]] virtual float GetFlyHeight() const { return 0.0f; }

  [[nodiscard]] virtual float GetBoundsRadius() const { return 0.0f; }

  [[nodiscard]] virtual ObjectGuid GetTransportGUID() const {
    return ObjectGuid(0);
  }

  [[nodiscard]] virtual float GetWorldFacing() const;

  [[nodiscard]] virtual float GetFacing() const { return GetOrientation(); }

  [[nodiscard]] float GetLocalFacing() const;

  [[nodiscard]] virtual float GetObjectScale() const {
    return GetFloat(OBJECT_FIELD_SCALE_X);
  }

  virtual bool UpdateModelNodeTransform(float dt, std::uint32_t current_tick_ms);

  void SetVisualModelWorldTransform(const float* matrix);
  void ClearVisualModelWorldTransform();
  [[nodiscard]] bool GetVisualModelWorldTransform(float* out_matrix) const;

  virtual void ApplyModelParentTransform(const float* parent_matrix);
  [[nodiscard]] bool GetModelParentTransform(float* out_matrix) const;

  virtual void QueryModelRebuildFlags(std::uint8_t flags,
                                      std::uint32_t& out_needs_construct,
                                      std::uint32_t& out_needs_refresh);

  virtual void GetWorldMatrix(float* out_matrix) const;

  [[nodiscard]] virtual std::tuple<float, float, float, float>
  GetWorldRotation() const;

  [[nodiscard]] virtual std::uint8_t GetBaseRace() const { return 0xFF; }

  [[nodiscard]] virtual bool ShouldFadeOnShow() const { return true; }

  [[nodiscard]] virtual float GetModelOpacity() const { return 1.0f; }

  struct ModelTintColor { float r{1.0f}; float g{1.0f}; float b{1.0f}; };
  [[nodiscard]] virtual ModelTintColor GetModelTintColor() const {
    return {1.0f, 1.0f, 1.0f};
  }

  virtual void AnimateOpacityTransition(std::uint32_t now_ms);

  void SetOpacityTarget(float target, std::uint32_t duration_ms);

  [[nodiscard]] float GetRenderedOpacity() const { return rendered_opacity_; }
  [[nodiscard]] float GetEffectiveRenderOpacity() const;

  void SetOpacityMaster(std::uint8_t alpha);
  [[nodiscard]] std::uint8_t GetOpacityMaster() const { return opacity_master_; }

  [[nodiscard]] float GetRenderedOpacityWithoutMaster() const {
    return static_cast<float>(opacity_current_) * (1.0f / 255.0f);
  }

  [[nodiscard]] bool IsOpacityFading() const { return opacity_fade_duration_ != 0; }

  static void SetActivePlayerGuid(const ObjectGuid& guid);

  [[nodiscard]] static ObjectGuid GetActivePlayerGuid();

  [[nodiscard]] bool IsActivePlayer() const {
    return guid_ == GetActivePlayerGuid();
  }

  [[nodiscard]] bool IsType(TypeMask mask) const {
    return (GetTypeMask() & static_cast<std::uint16_t>(mask)) != 0;
  }
  [[nodiscard]] bool IsUnit() const {
    return IsType(kTypeMaskUnit);
  }
  [[nodiscard]] bool IsPlayer() const {
    return IsType(kTypeMaskPlayer);
  }
  [[nodiscard]] bool IsItem() const {
    return IsType(kTypeMaskItem);
  }
  [[nodiscard]] bool IsGameObject() const {
    return IsType(kTypeMaskGameObject);
  }
  [[nodiscard]] bool IsContainer() const {
    return GetTypeId() == TypeID::kContainer;
  }
  [[nodiscard]] bool IsDynamicObject() const {
    return GetTypeId() == TypeID::kDynamicObject;
  }
  [[nodiscard]] bool IsCorpse() const {
    return GetTypeId() == TypeID::kCorpse;
  }

  void DisableMouseoverHighlightAndNotify();

  void SetMouseoverHighlightActive(bool active) {
    mouseover_highlight_active_ = active;
  }
  [[nodiscard]] bool IsMouseoverHighlightActive() const {
    return mouseover_highlight_active_;
  }

  virtual void OnRightClickInteract(WorldSession* session,
                                    TargetingSystem* targeting) const;

  [[nodiscard]] float GetSpeed(SpeedType type) const {
    auto idx = static_cast<std::size_t>(type);
    return idx < kMaxSpeeds ? speeds_[idx] : 0.0f;
  }

  virtual std::vector<std::uint16_t> ApplyCreateUpdate(const CreateObjectUpdate& upd);

  virtual void FinalizeCreateUpdate(const CreateObjectUpdate& upd);

  virtual void FinalizeWorldPublication();

  virtual void FinalizePacketUpdatePromotion();

  virtual std::vector<std::uint16_t> ApplyValuesUpdate(const ValuesUpdate& upd);

  std::vector<std::uint16_t> ApplyRawFieldValues(
      const UpdateFieldValues& field_data);
  virtual bool ApplyMovementUpdate(const MovementOnlyUpdate& upd);
  void SetName(const std::string& name) { name_ = name; }

 protected:
  void RefreshRenderedOpacity();

  [[nodiscard]] bool TryGetRelativePosition(
      ObjectGuid& parent_guid,
      std::array<float, 3>& local_position) const;

  ObjectGuid guid_;
  TypeID type_id_{TypeID::kObject};
  std::string name_;

  MovementUpdate position_;
  std::uint32_t object_time_offset_ms_{0};
  std::array<float, kMaxSpeeds> speeds_{};

  std::vector<std::uint32_t> fields_;

  std::vector<std::uint32_t> dirty_mask_;

  std::array<float, 6> object_bounding_box_{};
  bool has_object_bounding_box_{false};

  float native_scale_{1.0f};

  float model_bounding_box_height_{1.0f};

  std::uint32_t display_scale_update_tick_{0};

  float display_scale_stored_{1.0f};

  std::array<float, 16> visual_model_world_transform_{};
  bool has_visual_model_world_transform_{false};

  std::array<float, 16> model_parent_transform_{};
  bool has_model_parent_transform_{false};

  std::uint32_t opacity_fade_start_time_{0};

  std::int32_t  opacity_fade_duration_{0};

  std::uint8_t  opacity_current_{0};

  std::uint8_t  opacity_fade_start_{0};

  std::uint8_t  opacity_fade_end_{0};

  std::uint8_t  opacity_master_{255};

  float rendered_opacity_{0.0f};

  CMissileNode_C* effect_node_list_head_{nullptr};
  std::unique_ptr<CObjectEffect> object_effect_;
  std::uint16_t lifetime_hold_count_{0};
  bool pending_removal_{false};

  bool mouseover_highlight_active_{false};

  std::uint32_t primary_m2_instance_id_{0};

  OverlayDisplayType overlay_display_type_{OverlayDisplayType::kNone};
  std::uint32_t overlay_model_index_override_{0};
  std::uint32_t active_overlay_model_index_{0};
  float overlay_model_scale_{1.0f};
  bool overlay_model_visible_{false};

  bool overlay_bone_attached_{false};

  bool overlay_bone_rotation_compensated_{false};

  std::uint16_t overlay_animation_id_{0};

  bool render_object_has_sleep_flag_{false};

  [[nodiscard]] std::uint32_t ResolveOverlayModelIndex() const;

  std::vector<std::uint16_t> ApplyFieldValues(const UpdateFieldValues& field_data);

  std::vector<MirrorSlotBucket> mirror_object_buckets_;
  std::vector<MirrorSlotBucket> mirror_derived_section_buckets_;
  std::vector<MirrorSlotBucket> mirror_extra_section_buckets_;
  std::vector<MirrorSlotBucket> mirror_active_player_buckets_;

  openwow::render::m2::M2System* m2_system_{nullptr};
  openwow::render::WorldFrame* world_frame_{nullptr};
  WorldEnvironmentState* world_environment_{nullptr};
  ObjectManager* object_manager_{nullptr};
  static thread_local ObjectGuid s_active_player_guid_;
  friend struct CGObjectTestAccess;
};

using WorldObject = CGObject_C;

}
