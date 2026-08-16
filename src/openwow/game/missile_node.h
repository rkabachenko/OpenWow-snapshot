
#pragma once

#include "openwow/game/ceffect_c.h"
#include <cstdint>
#include <array>
#include <cstddef>
#include <optional>
#include <functional>
#include <string>
#include <vector>

#include "openwow/game/inventory/equipment/equipment_visual.h"
#include "openwow/game/object_guid.h"
#include "openwow/game/spell_visual_system.h"
#include "openwow/render/api/math/render_math_types.h"

namespace openwow::game {

class ObjectManager;

class CGObject_C;
struct ItemTemplate;

}

namespace openwow::data::dbc {
struct SpellVisualEffectNameEntry;
}

namespace openwow::game {

class CMissileNode_C;

[[nodiscard]] CMissileNode_C** GetActiveWorldSpellVisualEffectListHeadSlot();

struct AttachedUnitVisualReset {
  ObjectGuid unit_guid;
  bool reset_primary_visual_state{false};
  bool reset_secondary_visual_state{false};
};

class CMissileNode_C : public CEffect_C {
 public:
  static constexpr std::uint32_t kObjectItemVisualFlag = 0x80000u;
  explicit CMissileNode_C(openwow::render::m2::M2System& m2_system,
                          ObjectManager& object_manager,
                          const WorldSession& session)
      : CEffect_C(m2_system, object_manager, session) {}
  ~CMissileNode_C();
  CMissileNode_C(const CMissileNode_C&) = delete;
  CMissileNode_C& operator=(const CMissileNode_C&) = delete;
  CMissileNode_C(CMissileNode_C&&) = delete;
  CMissileNode_C& operator=(CMissileNode_C&&) = delete;

  static constexpr std::size_t kMaxLoopingLightningHandles = 12;

  void ReleaseLoopingLightningHandles();

  void SetLoopingLightningHandle(std::size_t slot, LightningObjectHandle handle);
  [[nodiscard]] LightningObjectHandle GetLoopingLightningHandle(
      std::size_t slot) const;

  [[nodiscard]] bool SetAttachedModelVisualSelectorFlag(bool enabled);

  void PlayImpactSound(std::uint32_t sound_kit_id,
                       std::uint32_t impact_context_value,
                       std::uint32_t impact_event_value,
                       const float* position,
                       CGObject_C* event_object,
                       std::uint32_t distance_value);

  void SetupFromObject(std::uint32_t spell_id,
                       std::uint32_t timestamp_start,
                       std::uint32_t timestamp_end,
                       const std::uint8_t* guid_data,
                       std::uint32_t listen_distance);

  void AttachSpellVisualEffectToUnit(std::uint32_t spell_id,
                                     float duration_seconds,
                                     std::uint32_t timestamp_start,
                                     std::uint32_t timestamp_end,
                                     const std::uint8_t* guid_data,
                                     std::uint32_t extra_flags);

  void SetupFromObjectWithItem(std::uint32_t spell_id,
                               std::uint32_t item_entry,
                               std::uint32_t timestamp_start,
                               std::uint32_t timestamp_end,
                               const std::uint8_t* guid_data,
                               std::uint32_t extra_flags);

  [[nodiscard]] static std::optional<EquipmentSlot>
  ResolveObjectItemVisualSlot(std::uint32_t inventory_type);

  void RefreshObjectItemVisualForOwnerState();

  void SetupFromSpellEffect(std::uint32_t spell_id,
                            std::uint32_t secondary_value,
                            const std::uint32_t* guid_data);

  [[nodiscard]] bool CreatePrimaryModelFromCreatureDisplay();
  [[nodiscard]] bool CreatePrimaryModelFromCreatureDisplay(
      std::uint32_t creature_display_id);

  [[nodiscard]] bool SetupWorldSpellVisualPrimaryModel(
      const data::dbc::SpellVisualEffectNameEntry& effect,
      const float* world_position,
      std::uint32_t flags,
      std::uint64_t follow_guid = 0);

  void HandlePrimaryModelAnimation(std::uint32_t animation_id);

  void ResetPrimaryModelForLightningInit();

  void SetMountTransitionHandle(MountTransitionObjectHandle handle) {
    mount_transition_handle_ = handle;
  }

  [[nodiscard]] std::uint64_t GetSourceGuid() const {
    return (static_cast<std::uint64_t>(source_guid_high_) << 32) |
           source_guid_low_;
  }
  [[nodiscard]] std::uint64_t GetTriggeredEventObjectGuid() const {
    return (static_cast<std::uint64_t>(triggered_event_guid_high_) << 32) |
           triggered_event_guid_low_;
  }
  [[nodiscard]] std::uint64_t GetFollowGuid() const {
    return (static_cast<std::uint64_t>(follow_guid_high_) << 32) |
           follow_guid_low_;
  }
  [[nodiscard]] std::uint32_t GetSpellId() const { return spell_id_; }
  [[nodiscard]] std::uint32_t GetFlags() const { return flags_; }
  [[nodiscard]] std::uint32_t GetMoveEventType() const {
    return move_event_type_;
  }
  [[nodiscard]] std::uint32_t GetSequenceCounter() const {
    return sequence_counter_;
  }
  [[nodiscard]] MountTransitionObjectHandle GetMountTransitionHandle() const {
    return mount_transition_handle_;
  }
  [[nodiscard]] std::uint32_t GetCreatureDisplayId() const {
    return creature_display_id_;
  }
  [[nodiscard]] std::uint32_t GetPrimaryModelInstanceId() const {
    return primary_model_instance_id_;
  }
  [[nodiscard]] bool SetPrimaryModelWorldTransform(
      const render::RenderMatrix4x4& matrix);
  void ClearPrimaryModelWorldTransform();
  void SetPrimaryModelAlpha(float alpha);
  [[nodiscard]] std::uint32_t GetPrimaryModelVisualState() const {
    return primary_model_visual_state_;
  }
  [[nodiscard]] std::uint32_t GetImpactEventValueForTesting() const {
    return timestamp_start_;
  }
  [[nodiscard]] std::uint32_t GetImpactSoundDistanceForTesting() const {
    return impact_sound_distance_;
  }
  [[nodiscard]] std::uint32_t GetImpactSoundContextValueForTesting() const {
    return impact_sound_context_value_;
  }
  [[nodiscard]] std::uint32_t GetImpactSoundHandleIdForTesting() const {
    return impact_sound_handle_id_;
  }
  void SetFlagsForTesting(const std::uint32_t flags) {
    flags_ = flags;
  }

 private:
  friend class CEffect_C;

  MountTransitionObjectHandle mount_transition_handle_{};
  std::uint32_t creature_template_entry_id_{0};
  std::uint32_t creature_display_id_{0};
  std::uint32_t item_template_entry_id_{0};
  std::uint32_t item_display_id_{0};

  std::array<LightningObjectHandle, kMaxLoopingLightningHandles>
      looping_lightning_handles_{};
  std::vector<AttachedUnitVisualReset> attached_unit_visual_resets_{};
  std::optional<EquipmentSlot> object_item_visual_slot_{};
  bool object_item_visual_applied_to_owner_{false};

  void CleanupImpactSound();
  void DestroyPrimaryModelInstance();
  [[nodiscard]] bool RefreshPrimaryModelPlacement();
  [[nodiscard]] bool CreatePrimaryModelInstanceFromPath(
      const std::string& model_path,
      float model_scale,
      std::function<void(std::uint32_t)> animation_callback);
  void HandleResolvedObjectItemTemplate(const ItemTemplate& item_template);
  void ApplyObjectItemVisualToOwner();
  void ClearObjectItemVisualFromOwner();
  void CancelPendingCreatureTemplateLookup();
  void CancelPendingObjectItemTemplateLookup();
  void ReleaseMountTransitionState();
  void ReleaseObjectItemVisualState();
  void RestoreAttachedUnitVisualState();
};

class ChainChannelVisualNode {
 public:
  ChainChannelVisualNode() = default;
  ChainChannelVisualNode(ObjectGuid source_guid, ObjectGuid target_guid)
      : source_guid_(source_guid), target_guid_(target_guid) {}

  [[nodiscard]] std::uint64_t GetSourceGuid() const {
    return source_guid_.GetRawValue();
  }
  [[nodiscard]] std::uint64_t GetTargetGuid() const {
    return target_guid_.GetRawValue();
  }
  [[nodiscard]] ObjectGuid GetSourceObjectGuid() const { return source_guid_; }
  [[nodiscard]] ObjectGuid GetTargetObjectGuid() const { return target_guid_; }

 private:
  ObjectGuid source_guid_{};
  ObjectGuid target_guid_{};
};

}
