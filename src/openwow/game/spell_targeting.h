#pragma once

#include "openwow/game/object_guid.h"

#include <cstdint>
#include <utility>

namespace openwow::game {

class SpellCastRuntime;

enum class SpellTargetingMode : std::uint8_t {
  None            = 0,
  Unit            = 1,
  GroundTarget    = 2,
  AreaCone        = 3,
  AreaCircle      = 4,
  DirectionalCone = 5,
  Self            = 6,
};

struct SpellTargetingState {
  SpellTargetingMode mode   = SpellTargetingMode::None;
  std::uint32_t      spellId = 0;
  float              cursorX = 0.0f;
  float              cursorY = 0.0f;
  float              cursorZ = 0.0f;
  float              radius  = 0.0f;
  float              coneAngle = 0.0f;
  bool               isActive  = false;
};

enum class TargetValidation : std::uint8_t {
  Valid       = 0,
  OutOfRange  = 1,
  NotVisible  = 2,
  Dead        = 3,
  Friendly    = 4,
  Hostile     = 5,
  InvalidType = 6,
  NotInLineOfSight = 7,
};

class SpellTargeting {
 public:
  explicit SpellTargeting(SpellCastRuntime* casts = nullptr) noexcept
      : casts_(casts) {}

  void StartTargeting(std::uint32_t spellId, SpellTargetingMode mode,
                      float radius = 0.0f, float coneAngle = 0.0f,
                      std::uint32_t targetMask = 0);

  void CancelTargeting();

  void ConfirmTarget(float worldX, float worldY);

  void ConfirmTarget(float worldX, float worldY, float worldZ);

  void ConfirmTarget(ObjectGuid unitGuid);

  [[nodiscard]] SpellTargetingState GetState() const;
  [[nodiscard]] bool IsTargeting() const;
  [[nodiscard]] SpellTargetingMode GetMode() const;
  [[nodiscard]] std::uint32_t GetSpellId() const;

  void SetCursorPosition(float worldX, float worldY);

  void SetCursorPosition(float worldX, float worldY, float worldZ);

  [[nodiscard]] std::pair<float, float> GetCursorPosition() const;
  [[nodiscard]] float GetCursorZ() const;

  [[nodiscard]] float GetRadius() const;
  [[nodiscard]] float GetConeAngle() const;

  void  SetMaxRange(float range);
  [[nodiscard]] float GetMaxRange() const;

  void  SetMinRange(float range);
  [[nodiscard]] float GetMinRange() const;

  [[nodiscard]] bool IsInRange(float distance) const;

  void SetLastConfirmedTarget(ObjectGuid guid);
  [[nodiscard]] ObjectGuid GetLastConfirmedTarget() const;

  void SetLastGroundTarget(float x, float y);
  void SetLastGroundTarget(float x, float y, float z);
  [[nodiscard]] std::pair<float, float> GetLastGroundTarget() const;
  [[nodiscard]] float GetLastGroundTargetZ() const;

  void SetCurrentTarget(ObjectGuid guid);
  [[nodiscard]] ObjectGuid GetCurrentTarget() const;

  void SetFocusTarget(ObjectGuid guid);
  [[nodiscard]] ObjectGuid GetFocusTarget() const;

  void SetTargetOfTarget(ObjectGuid guid);
  [[nodiscard]] ObjectGuid GetTargetOfTarget() const;

  [[nodiscard]] ObjectGuid ResolveTarget(ObjectGuid self_guid,
                                         bool is_beneficial,
                                         bool requires_target) const;

  [[nodiscard]] TargetValidation ValidateRange(float distance) const;

  [[nodiscard]] std::uint32_t GetTargetFlags() const;

  void SetTargetMask(std::uint32_t mask);
  [[nodiscard]] std::uint32_t GetTargetMask() const;

  void SetPendingSourceItem(ObjectGuid item_guid);
  [[nodiscard]] ObjectGuid GetPendingSourceItem() const;

  void ClearPendingSourceItem();

  void SetItemCursorSource(ObjectGuid item_guid);
  [[nodiscard]] ObjectGuid GetItemCursorSource() const;

  [[nodiscard]] std::uint32_t GetConfirmationCount() const;

  [[nodiscard]] bool UsesManualPreviewFacing() const;
  void SetManualPreviewFacing(bool enabled);
  [[nodiscard]] float GetPreviewFacingRadians() const;
  void SetPreviewFacingRadians(float radians);
  void AdvancePreviewFacingQuarterTurn();
  [[nodiscard]] float GetPreviewScale() const;
  void SetPreviewScale(float scale);

  void Reset();

 private:
  void ResetPreview();

  SpellCastRuntime* casts_ = nullptr;
  SpellTargetingState state_;
  std::uint32_t target_mask_ = 0;

  float max_range_ = 0.0f;
  float min_range_ = 0.0f;

  ObjectGuid last_confirmed_target_;
  float last_ground_x_ = 0.0f;
  float last_ground_y_ = 0.0f;
  float last_ground_z_ = 0.0f;

  ObjectGuid current_target_;
  ObjectGuid focus_target_;
  ObjectGuid target_of_target_;
  ObjectGuid pending_source_item_;
  ObjectGuid item_cursor_source_;

  std::uint32_t confirmation_count_ = 0;
  bool preview_uses_manual_facing_ = false;
  float preview_facing_radians_ = 0.0f;
  float preview_scale_ = 1.0f;
};

}
