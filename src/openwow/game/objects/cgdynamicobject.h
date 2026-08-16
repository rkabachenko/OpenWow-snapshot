#pragma once

#include "openwow/game/objects/cgobject.h"

#include <cstdint>
#include <functional>
#include <string>

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::world {
class WorldCamera;
}

namespace openwow::game {

enum class DynamicObjectType : std::uint8_t {
  Portal        = 0,
  AreaSpell     = 1,
  FarsightFocus = 2,
  RaidMarker    = 3,
};

struct DynamicObjectAreaModelVisual {
  std::int32_t model_id{0};
  float rate{0.0f};

  [[nodiscard]] bool operator==(
      const DynamicObjectAreaModelVisual&) const = default;
};

struct DynamicObjectVisualState {
  std::uint32_t spell_id{0};
  std::int32_t violence_level{0};
  std::uint32_t spell_visual_id{0};
  std::uint32_t spell_visual_kit_id{0};
  std::uint32_t effect_id{0};
  std::uint32_t sound_kit_id{0};
  DynamicObjectAreaModelVisual area_model{};
  std::string model_path;
  float area_effect_size{0.0f};
  float effect_scale{1.0f};
  bool has_spell_record{false};
  bool has_spell_visual_record{false};
  bool has_spell_visual_kit_record{false};
  bool has_area_model{false};
  bool spell_attributes_ex5_bit30{false};

  [[nodiscard]] bool operator==(
      const DynamicObjectVisualState&) const = default;
};

class CGDynamicObject_C : public CGObject_C {
 public:
  CGDynamicObject_C();
  explicit CGDynamicObject_C(ObjectGuid guid);
  explicit CGDynamicObject_C(ObjectManager& objects);
  CGDynamicObject_C(ObjectManager& objects, ObjectGuid guid);
  ~CGDynamicObject_C() override;

  std::vector<std::uint16_t> ApplyCreateUpdate(const CreateObjectUpdate& upd) override;
  void FinalizeCreateUpdate(const CreateObjectUpdate& upd) override;
  void FinalizeWorldPublication() override;

  [[nodiscard]] ObjectGuid GetCaster() const;

  [[nodiscard]] std::uint32_t GetBytes() const;

  [[nodiscard]] std::uint32_t GetSpellId() const;

  [[nodiscard]] float GetRadius() const;

  [[nodiscard]] std::uint32_t GetCastTime() const;

  [[nodiscard]] DynamicObjectType GetDynObjType() const;

  [[nodiscard]] bool IsPortal() const;
  [[nodiscard]] bool IsAreaSpell() const;
  [[nodiscard]] bool IsFarsightFocus() const;
  [[nodiscard]] bool IsRaidMarker() const;

  void PrepareForWorldRemoval() override;

  void RegisterTransientVisualCleanup(std::function<void()> cleanup);

  void TrackTransientSoundHandle(std::uint32_t handle_id);

  [[nodiscard]] bool HasTransientRuntimeState() const;

  void QueryModelRebuildFlags(std::uint8_t flags,
                              std::uint32_t& out_needs_construct,
                              std::uint32_t& out_needs_refresh) override;

  [[nodiscard]] float GetObjectScale() const override { return object_scale_; }

  void SetObjectScale(float scale) { object_scale_ = scale; }

  [[nodiscard]] DynamicObjectVisualState ResolveVisualState(
      const data::dbc::DbcLoader& dbc,
      std::int32_t violence_level) const;

  bool OnModelLoaded(std::uint32_t instance_id,
                     const DynamicObjectVisualState& visual);

  bool OnModelLoaded(std::uint32_t instance_id);

  [[nodiscard]] bool HasDirectedCastAnimation() const {
    return has_directed_cast_anim_;
  }

  void SetupSpellVisualKit();

  void Cleanup(int reason) override;

  void OnCreate();

  bool ActivatePendingVisualIfReady();

  void GetWorldMatrix(float* out_matrix) const override;

  void ApplyModelParentTransform(const float* parent_matrix) override;

  void OnRenderUpdate(openwow::world::WorldCamera* camera,
                      std::uint32_t event_type,
                      std::uint32_t visual_id,
                      float* position,
                      std::uint32_t flags);

  void SetupAnimation();

  [[nodiscard]] bool HasStaticModelFlag() const { return static_model_flag_; }

  void SetStaticModelFlag(bool value) { static_model_flag_ = value; }

 private:
  [[nodiscard]] DynamicObjectVisualState ResolveVisualStateImpl(
      const data::dbc::DbcLoader& dbc,
      std::int32_t violence_level,
      bool emit_diagnostics) const;
  void ReleaseTransientRuntimeState();
  void ApplyVisualScaleToNativeScale(float scale);

  bool static_model_flag_{false};

  bool has_directed_cast_anim_{false};

  float object_scale_{1.0f};
  float applied_visual_scale_{1.0f};
  std::uintptr_t area_model_handle_{0};
  std::function<void()> transient_visual_cleanup_{};
  std::uint32_t transient_sound_handle_id_{0};
};

}
