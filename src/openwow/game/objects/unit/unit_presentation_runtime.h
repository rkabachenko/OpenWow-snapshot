#pragma once

#include "openwow/game/guild_manager.h"
#include "openwow/game/inventory/equipment/equipment_visual.h"
#include "openwow/game/objects/cgobject.h"
#include "openwow/game/objects/unit/unit_area_weather.h"
#include "openwow/game/objects/unit/unit_footprint.h"
#include "openwow/render/m2/m2_public_types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace openwow::data::dbc {
struct CreatureDisplayInfoEntry;
struct CreatureDisplayInfoExtraEntry;
struct CreatureModelDataEntry;
struct SpellVisualEntry;
}

namespace openwow::game {

class CGUnit_C;
class UnitMissileTrajectory_C;
class WorldSession;
struct DisplayInfoEntry;

struct CharacterModelVisualHandle {
  std::uint32_t value{0};

  [[nodiscard]] constexpr bool IsValid() const { return value != 0; }
  [[nodiscard]] constexpr explicit operator bool() const { return IsValid(); }
  friend constexpr bool operator==(CharacterModelVisualHandle,
                                   CharacterModelVisualHandle) = default;
};

struct CharacterModelVisualState {
  CharacterModelVisualHandle handle{};
  std::uint32_t refresh_flags{0};
  std::optional<GuildEmblem> guild_tabard_emblem{};

  [[nodiscard]] bool HasHandle() const { return handle.IsValid(); }
  void Clear() {
    handle = {};
    refresh_flags = 0;
    guild_tabard_emblem.reset();
  }
};

struct UnitBodyEquipmentData {
  std::uint8_t item_class{0};
  std::uint8_t fallback_weapon_subclass_id{0};
  std::uint8_t weapon_subclass_id{0xFF};
  std::uint8_t material_id{0};
};

struct UnitAttachmentVisualSelectorNode {
  std::uint16_t selector_id{0};
  std::uint32_t model_instance_id{0};
  std::vector<UnitAttachmentVisualSelectorNode *> children{};
};

struct UnitGroundPositionResult {
  float distance{0.0f};
  bool missed{false};
  float normal_x{0.0f};
  float normal_y{0.0f};
};

struct CalcGroundPosCollisionResult {
  float ground_z{0.0f};
  bool hit{false};
  float normal_x{0.0f};
  float normal_y{0.0f};
  float normal_z{1.0f};
};

using CalcGroundPosCallback = CalcGroundPosCollisionResult (*)(
    const CGUnit_C &unit, const std::array<float, 3> &origin,
    float max_distance, void *context);
using UnitCollisionAabbCallback = bool (*)(const float *aabb, void *context);

void SetCalcGroundPosCallback(CalcGroundPosCallback callback, void *context);
void ClearCalcGroundPosCallback();
void SetUnitCollisionAabbCallback(UnitCollisionAabbCallback callback,
                                  void *context);
void ClearUnitCollisionAabbCallback();

class UnitPresentationRuntime final {
public:
  explicit UnitPresentationRuntime(CGUnit_C &owner) noexcept : owner_(owner) {}
  UnitPresentationRuntime(const UnitPresentationRuntime &) = delete;
  UnitPresentationRuntime &operator=(const UnitPresentationRuntime &) = delete;
  UnitPresentationRuntime(UnitPresentationRuntime &&) = delete;
  UnitPresentationRuntime &operator=(UnitPresentationRuntime &&) = delete;

  [[nodiscard]] UnitFootprintComponent &Footprint() noexcept { return footprint_; }
  [[nodiscard]] const UnitFootprintComponent &Footprint() const noexcept {
    return footprint_;
  }
  [[nodiscard]] UnitAreaWeatherComponent &AreaWeather() noexcept {
    return area_weather_;
  }
  [[nodiscard]] const UnitAreaWeatherComponent &AreaWeather() const noexcept {
    return area_weather_;
  }

  [[nodiscard]] std::uint32_t DisplayId() const;
  [[nodiscard]] std::uint32_t NativeDisplayId() const;

  [[nodiscard]] std::uint32_t VisibleBodyDisplayId() const;

  [[nodiscard]] std::uint32_t CurrentDisplayId() const;
  [[nodiscard]] std::uint32_t CreatureModelLookupDisplayId() const;
  [[nodiscard]] bool IsTransformed() const;
  void OnDisplayIdChanged();
  [[nodiscard]] const DisplayInfoEntry *ResolveCreatureModelDisplayInfo() const;

  struct CreatureDisplayRows {
    const data::dbc::CreatureDisplayInfoEntry *display{nullptr};
    const data::dbc::CreatureModelDataEntry *model{nullptr};
  };
  [[nodiscard]] CreatureDisplayRows ResolveCreatureDisplayRowsFor(
      std::uint32_t display_id) const;
  [[nodiscard]] CreatureDisplayRows ResolveCreatureDisplayRows() const;
  [[nodiscard]] const char *PortraitTextureName() const;
  [[nodiscard]] std::uint32_t SizeClass() const;
  [[nodiscard]] float AttachedEffectModelScale() const;
  [[nodiscard]] float ModelHeight() const;
  [[nodiscard]] float CameraTargetHeight() const;
  [[nodiscard]] float ModelScale() const;
  [[nodiscard]] float ModelOpacity() const;
  [[nodiscard]] bool ShouldFadeOnShow() const;
  [[nodiscard]] float CollisionHeight() const noexcept { return collision_height_; }
  void SetCollisionHeight(float height) noexcept { collision_height_ = height; }
  [[nodiscard]] float ModelBoundingRadius() const noexcept {
    return model_bounding_radius_;
  }
  [[nodiscard]] float CachedHoverHeight() const noexcept {
    return cached_hover_height_;
  }
  void SetCachedHoverHeight(float height) noexcept { cached_hover_height_ = height; }
  [[nodiscard]] std::uint32_t DisplayGender() const;
  [[nodiscard]] std::uint32_t DisplayRace() const;
  void SetDisplayInfoExtra(
      const data::dbc::CreatureDisplayInfoExtraEntry *entry) noexcept {
    cached_display_info_extra_rec_ = entry;
  }

  [[nodiscard]] float ResolveDisplayNativeScale() const;

  struct CreatureFamilySizeCurve {
    float min_scale{1.0f};
    std::uint32_t min_scale_level{0};
    float max_scale{1.0f};
    std::uint32_t max_scale_level{0};
  };

  [[nodiscard]] static float CombineDisplayNativeScale(
      float display_scale, float model_scale,
      const CreatureFamilySizeCurve *family, std::int32_t level,
      bool is_numbered_pet) noexcept;
  void RefreshDisplayInfoScale(bool reset_ratio);
  void NotifyNameplateLevelChanged();
  [[nodiscard]] const UnitBodyEquipmentData *BodyArmorEquipmentData() const;
  void SetBodyArmorEquipmentData(const UnitBodyEquipmentData &data);
  [[nodiscard]] float ScaledModelHeight(
      const data::dbc::CreatureDisplayInfoEntry *display_info,
      const data::dbc::CreatureModelDataEntry *model_data,
      float *out_raw_scale = nullptr) const;
  [[nodiscard]] float AnimScaleFromDbc(
      const data::dbc::CreatureDisplayInfoEntry *display_info) const;

  [[nodiscard]] bool CalcGroundPos(UnitGroundPositionResult &result) const;
  [[nodiscard]] CalcGroundPosCollisionResult QueryGroundSurface(
      const std::array<float, 3> &origin, float max_distance) const;
  [[nodiscard]] bool InitMountedCollisionBounds(float mount_height, bool forced);
  void HandleScaleChange(float new_scale);
  [[nodiscard]] bool InitDisplayCollisionBounds(bool growing, bool forced);
  [[nodiscard]] bool InitPlayerDisplayCollisionBounds(bool growing, bool forced);
  void RefreshActiveDisplayRuntimeState();
  void ResetRuntimeState();

  void ComputeModelBoundingBox();
  void UpdateEffectAttachments();
  void RefreshObjectItemEffectTransforms();
  void RefreshModelBoundsAndEffects();
  void RefreshModelBoundsAndEffectsForced();
  void UpdateModelTransform(bool force);
  void UpdateMountTransitionNodeTransform();

  float *ModelToWorldMatrix(float *out_matrix) const;
  [[nodiscard]] std::optional<render::RenderVec3> ProjectPositionToScreen() const;
  [[nodiscard]] bool GetSpellVisualAttachmentPosition(
      float *out_position, const data::dbc::SpellVisualEntry &visual) const;
  [[nodiscard]] bool HasModelAttachmentPoint(std::uint32_t attachment_index,
                                             bool use_raw_index) const;
  [[nodiscard]] bool GetMappedAttachmentPosition(
      float *out_position, std::uint32_t attachment_index,
      const render::RenderVec3 &offset, bool use_raw_index) const;
  void GetBoneAttachmentWorldPosition(float *out_position,
                                      std::uint32_t lookup_index,
                                      const float *offset) const;

  void SetDisplayChangeCallback(
      std::function<void(std::uint32_t, std::uint32_t)> callback);
  void SetCachedNativeDisplayId(std::uint32_t display_id) noexcept {
    cached_native_display_id_ = display_id;
  }
  [[nodiscard]] std::uint32_t CachedNativeDisplayId() const noexcept {
    return cached_native_display_id_;
  }
  void SetNameplateFramePtr(std::uintptr_t ptr) noexcept {
    nameplate_frame_ptr_ = ptr;
  }
  [[nodiscard]] std::uintptr_t NameplateFramePtr() const noexcept {
    return nameplate_frame_ptr_;
  }
  [[nodiscard]] bool HasNameplateFrame() const noexcept {
    return nameplate_frame_ptr_ != 0;
  }
  void SetSpeechBubbleFramePtr(std::uintptr_t ptr) noexcept {
    speech_bubble_frame_ptr_ = ptr;
  }
  [[nodiscard]] std::uintptr_t SpeechBubbleFramePtr() const noexcept {
    return speech_bubble_frame_ptr_;
  }

  [[nodiscard]] bool HasCharacterModelVisual() const noexcept {
    return character_model_visual_state_.HasHandle();
  }
  [[nodiscard]] CharacterModelVisualHandle CharacterModelVisualHandleForTest()
      const noexcept {
    return character_model_visual_state_.handle;
  }
  [[nodiscard]] const CharacterModelVisualState &CharacterModelVisualStateForTest()
      const noexcept {
    return character_model_visual_state_;
  }
  void SetCharacterModelVisualHandleForTest(CharacterModelVisualHandle value);
  void SetCharacterVisualTabardEmblem(const GuildEmblem &emblem) {
    character_model_visual_state_.guild_tabard_emblem = emblem;
  }
  void AddCharacterVisualRefreshFlags(std::uint32_t flags) noexcept {
    if (HasCharacterModelVisual()) {
      character_model_visual_state_.refresh_flags |= flags;
    }
  }
  static CharacterModelVisualHandle PendingCharSelectVisualHandleForTest();
  static void SetPendingCharSelectVisualHandleForTest(
      CharacterModelVisualHandle value);
  static std::uint32_t PendingCharSelectDisplayId();
  static void SetPendingCharSelectDisplayId(std::uint32_t display_id);
  void ReleaseCharacterModelVisualHandle();
  void MarkCharacterVisualReleased();
  void TryReuseCharSelectModel();
  [[nodiscard]] bool EnsureModelReady() const;
  void PreserveOrReleaseCharacterVisualOnCleanup();

  void SetSpellVisualModelInstances(std::uint32_t primary_instance_id,
                                    std::uint32_t secondary_instance_id) noexcept;
  [[nodiscard]] std::uint32_t PrimarySpellVisualModelInstanceId() const noexcept;
  [[nodiscard]] std::uint32_t SecondarySpellVisualModelInstanceId() const noexcept;
  void SetAttachmentVisualSelectorRoots(
      std::vector<UnitAttachmentVisualSelectorNode *> roots);
  [[nodiscard]] bool SetAttachedModelVisualSelectorFlagForSelector(
      std::uint16_t selector_id, bool enabled);
  void SetTransientEquipmentDisplayOverride(EquipmentSlot slot,
                                            std::uint32_t display_id);
  void ClearTransientEquipmentDisplayOverride(EquipmentSlot slot);
  [[nodiscard]] std::optional<std::uint32_t>
  TransientEquipmentDisplayOverride(EquipmentSlot slot) const;

  void SetUnitAlpha(float alpha);
  [[nodiscard]] std::uint8_t UnitAlphaByte() const noexcept;
  [[nodiscard]] float UnitAlpha() const noexcept {
    return static_cast<float>(UnitAlphaByte()) / 255.0f;
  }
  void StartBodyColorFade(std::uint32_t start_time, std::uint32_t target_color,
                          std::uint32_t delay, std::uint32_t duration) noexcept;
  [[nodiscard]] bool TryGetInterpolatedBodyColor(std::uint32_t now_ms,
                                                  std::uint32_t &out_color);
  [[nodiscard]] std::uint32_t DefaultBodyColor() const noexcept {
    return default_body_color_;
  }
  void SetModelTintColor(CGObject_C::ModelTintColor color) noexcept {
    model_tint_color_ = color;
  }
  [[nodiscard]] CGObject_C::ModelTintColor ModelTintColor() const noexcept {
    return model_tint_color_;
  }

  [[nodiscard]] bool HasActiveCreatureModelData() const noexcept {
    return has_active_creature_model_data_;
  }
  [[nodiscard]] std::uint32_t ActiveCreatureModelFlags() const noexcept {
    return active_creature_model_flags_;
  }
  [[nodiscard]] std::uint32_t ActiveFootstepShakeSize() const noexcept {
    return active_footstep_shake_size_;
  }
  void UpdateIdleAnimationLatch();
  [[nodiscard]] bool ModelAnimationsReady() const noexcept;
  void OnModelLoaded(WorldSession &session, std::uint32_t instance_id);

  static void UpdateCameraTargetAndMissilePreview(
      UnitMissileTrajectory_C &missile_trajectory, CGUnit_C *unit);

private:
  CGUnit_C &owner_;
  std::function<void(std::uint32_t, std::uint32_t)> on_display_changed_;
  const data::dbc::CreatureDisplayInfoExtraEntry *cached_display_info_extra_rec_{
      nullptr};
  std::uint32_t cached_native_display_id_{0};
  std::uint32_t model_misc_flags_{0};
  float collision_height_{0.0f};
  float cached_hover_height_{0.0f};
  UnitBodyEquipmentData cached_body_equipment_{};
  float model_bounding_radius_{1.2f};
  UnitAreaWeatherComponent area_weather_;
  UnitFootprintComponent footprint_;
  std::uint32_t active_creature_model_flags_{0};
  std::uint32_t active_footstep_shake_size_{0};
  bool has_active_creature_model_data_{false};
  CharacterModelVisualState character_model_visual_state_{};
  static inline CharacterModelVisualState pending_char_select_visual_state_{};
  static inline std::uint32_t pending_char_select_display_id_{0};
  std::uintptr_t nameplate_frame_ptr_{0};
  std::uintptr_t speech_bubble_frame_ptr_{0};
  std::uint32_t primary_spell_visual_model_instance_id_{0};
  std::uint32_t secondary_spell_visual_model_instance_id_{0};
  std::vector<UnitAttachmentVisualSelectorNode *> attachment_visual_selector_roots_{};
  std::array<std::uint32_t, static_cast<std::size_t>(EquipmentSlot::SlotCount)>
      transient_equipment_display_overrides_{};
  std::uint32_t transient_equipment_display_override_mask_{0};
  std::uint32_t body_color_fade_start_time_{0};
  std::uint32_t body_color_fade_target_color_{0};
  std::uint32_t body_color_fade_delay_{0};
  std::uint32_t body_color_fade_duration_{0};
  std::uint32_t default_body_color_{0x00FFFFFF};
  CGObject_C::ModelTintColor model_tint_color_{1.0f, 1.0f, 1.0f};

};

}
