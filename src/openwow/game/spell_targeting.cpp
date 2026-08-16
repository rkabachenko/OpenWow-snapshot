
#include "openwow/game/spell_targeting.h"
#include "openwow/game/actions/held_cursor/adapters/platform/cursor_surface.h"
#include "openwow/game/c_input_control.h"
#include "openwow/game/spell_book.h"
#include "openwow/game/spell_cast_runtime.h"

namespace openwow::game {

namespace {

constexpr std::uint32_t kRetailPointCursorType = 1u;
constexpr std::uint32_t kRetailUnableCastCursorType = 0x1cu;

constexpr float kPreviewQuarterTurnRadians = 1.5707964f;
constexpr float kPreviewFullTurnRadians = 6.2831855f;

constexpr bool UsesGroundLocationTarget(const std::uint32_t mask) {
  constexpr std::uint32_t location_flags =
      static_cast<std::uint32_t>(SpellTargetFlag::kSourceLocation) |
      static_cast<std::uint32_t>(SpellTargetFlag::kDestLocation);
  return (mask & location_flags) != 0;
}

constexpr bool HasUnitOrCorpseTarget(const std::uint32_t mask) {
  constexpr std::uint32_t unit_or_corpse_flags =
      static_cast<std::uint32_t>(SpellTargetFlag::kUnit) |
      static_cast<std::uint32_t>(SpellTargetFlag::kUnitRaid) |
      static_cast<std::uint32_t>(SpellTargetFlag::kUnitParty) |
      static_cast<std::uint32_t>(SpellTargetFlag::kUnitEnemy) |
      static_cast<std::uint32_t>(SpellTargetFlag::kUnitAlly) |
      static_cast<std::uint32_t>(SpellTargetFlag::kCorpseEnemy) |
      static_cast<std::uint32_t>(SpellTargetFlag::kUnitDead) |
      static_cast<std::uint32_t>(SpellTargetFlag::kCorpseAlly) |
      static_cast<std::uint32_t>(SpellTargetFlag::kUnitMinipet);
  return (mask & unit_or_corpse_flags) != 0;
}

void PublishTargetingSlotOccupied(const bool occupied) {
  if (auto *cursor = GetActiveCursorSurface(); cursor != nullptr) {
    cursor->SetBaseCursor(occupied ? CursorType::kCast : CursorType::kDefault);
    cursor->SetImmediateCursorType(occupied ? kRetailUnableCastCursorType
                                            : kRetailPointCursorType);
  }
  SetSpellTargetingCursorForce(occupied);
}

void ClearCompletedTargetingState(SpellTargetingState *state, std::uint32_t *target_mask,
                                  float *max_range, float *min_range) {
  const bool was_active = state->isActive;
  state->isActive = false;
  state->mode = SpellTargetingMode::None;
  state->spellId = 0;
  *target_mask = 0;
  *max_range = 0.0f;
  *min_range = 0.0f;
  if (was_active) {
    PublishTargetingSlotOccupied(false);
  }
}

}

void SpellTargeting::StartTargeting(std::uint32_t spellId, SpellTargetingMode mode, float radius,
                                    float coneAngle, std::uint32_t targetMask) {
  ResetPreview();
  state_.spellId = spellId;
  state_.mode = mode;
  state_.radius = radius;
  state_.coneAngle = coneAngle;
  state_.isActive = true;
  state_.cursorX = 0.0f;
  state_.cursorY = 0.0f;
  state_.cursorZ = 0.0f;
  target_mask_ = targetMask;
  ClearPendingSourceItem();
  PublishTargetingSlotOccupied(true);
}

void SpellTargeting::CancelTargeting() {
  const std::uint32_t cancelled_spell_id = state_.spellId;
  ClearCompletedTargetingState(&state_, &target_mask_, &max_range_, &min_range_);
  ResetPreview();
  if (casts_ != nullptr) {
    casts_->DiscardPreparedTargetedCast(cancelled_spell_id);
  }
}

void SpellTargeting::ConfirmTarget(float worldX, float worldY) {
  ConfirmTarget(worldX, worldY, 0.0f);
}

void SpellTargeting::ConfirmTarget(float worldX, float worldY, float worldZ) {
  last_ground_x_ = worldX;
  last_ground_y_ = worldY;
  last_ground_z_ = worldZ;
  ++confirmation_count_;
  ClearCompletedTargetingState(&state_, &target_mask_, &max_range_, &min_range_);
  ResetPreview();
}

void SpellTargeting::ConfirmTarget(ObjectGuid unitGuid) {
  last_confirmed_target_ = unitGuid;
  ++confirmation_count_;
  ClearCompletedTargetingState(&state_, &target_mask_, &max_range_, &min_range_);
  ResetPreview();
}

SpellTargetingState SpellTargeting::GetState() const {
  return state_;
}

bool SpellTargeting::IsTargeting() const {
  return state_.isActive;
}

SpellTargetingMode SpellTargeting::GetMode() const {
  return state_.mode;
}

std::uint32_t SpellTargeting::GetSpellId() const {
  return state_.isActive ? state_.spellId : 0;
}

void SpellTargeting::SetCursorPosition(float worldX, float worldY) {
  state_.cursorX = worldX;
  state_.cursorY = worldY;
}

void SpellTargeting::SetCursorPosition(float worldX, float worldY, float worldZ) {
  state_.cursorX = worldX;
  state_.cursorY = worldY;
  state_.cursorZ = worldZ;
}

std::pair<float, float> SpellTargeting::GetCursorPosition() const {
  return {state_.cursorX, state_.cursorY};
}

float SpellTargeting::GetCursorZ() const {
  return state_.cursorZ;
}

float SpellTargeting::GetRadius() const {
  return state_.radius;
}
float SpellTargeting::GetConeAngle() const {
  return state_.coneAngle;
}

void SpellTargeting::SetMaxRange(float range) {
  max_range_ = range;
}
float SpellTargeting::GetMaxRange() const {
  return max_range_;
}

void SpellTargeting::SetMinRange(float range) {
  min_range_ = range;
}
float SpellTargeting::GetMinRange() const {
  return min_range_;
}

bool SpellTargeting::IsInRange(float distance) const {

  if (max_range_ > 0.0f && distance > max_range_)
    return false;

  if (min_range_ > 0.0f && distance < min_range_)
    return false;
  return true;
}

void SpellTargeting::SetLastConfirmedTarget(ObjectGuid guid) {
  last_confirmed_target_ = guid;
}

ObjectGuid SpellTargeting::GetLastConfirmedTarget() const {
  return last_confirmed_target_;
}

void SpellTargeting::SetLastGroundTarget(float x, float y) {
  last_ground_x_ = x;
  last_ground_y_ = y;
}

void SpellTargeting::SetLastGroundTarget(float x, float y, float z) {
  last_ground_x_ = x;
  last_ground_y_ = y;
  last_ground_z_ = z;
}

std::pair<float, float> SpellTargeting::GetLastGroundTarget() const {
  return {last_ground_x_, last_ground_y_};
}

float SpellTargeting::GetLastGroundTargetZ() const {
  return last_ground_z_;
}

void SpellTargeting::SetCurrentTarget(ObjectGuid guid) {
  current_target_ = guid;
}

ObjectGuid SpellTargeting::GetCurrentTarget() const {
  return current_target_;
}

void SpellTargeting::SetFocusTarget(ObjectGuid guid) {
  focus_target_ = guid;
}

ObjectGuid SpellTargeting::GetFocusTarget() const {
  return focus_target_;
}

void SpellTargeting::SetTargetOfTarget(ObjectGuid guid) {
  target_of_target_ = guid;
}

ObjectGuid SpellTargeting::GetTargetOfTarget() const {
  return target_of_target_;
}

ObjectGuid SpellTargeting::ResolveTarget(ObjectGuid self_guid, bool is_beneficial,
                                         bool requires_target) const {

  if (state_.isActive && state_.mode == SpellTargetingMode::Self) {
    return self_guid;
  }

  if (static_cast<bool>(current_target_)) {
    return current_target_;
  }

  if (is_beneficial) {
    return self_guid;
  }

  if (!requires_target) {
    return self_guid;
  }

  return ObjectGuid{};
}

TargetValidation SpellTargeting::ValidateRange(float distance) const {
  if (max_range_ > 0.0f && distance > max_range_) {
    return TargetValidation::OutOfRange;
  }
  if (min_range_ > 0.0f && distance < min_range_) {
    return TargetValidation::OutOfRange;
  }
  return TargetValidation::Valid;
}

std::uint32_t SpellTargeting::GetTargetFlags() const {
  using F = SpellTargetFlag;
  switch (state_.mode) {
  case SpellTargetingMode::Self:
    return static_cast<std::uint32_t>(F::kNone);

  case SpellTargetingMode::Unit:
    return static_cast<std::uint32_t>(F::kUnit);

  case SpellTargetingMode::GroundTarget:
    return static_cast<std::uint32_t>(F::kDestLocation);

  case SpellTargetingMode::AreaCone:
  case SpellTargetingMode::DirectionalCone:
    return static_cast<std::uint32_t>(F::kDestLocation);

  case SpellTargetingMode::AreaCircle:
    return static_cast<std::uint32_t>(F::kDestLocation);

  case SpellTargetingMode::None:
  default:
    return static_cast<std::uint32_t>(F::kNone);
  }
}

void SpellTargeting::SetTargetMask(std::uint32_t mask) {
  target_mask_ = mask;
  if (state_.isActive) {
    if (UsesGroundLocationTarget(mask)) {
      state_.mode = SpellTargetingMode::GroundTarget;
    } else if (HasUnitOrCorpseTarget(mask)) {
      state_.mode = SpellTargetingMode::Unit;
    }
  }
}
std::uint32_t SpellTargeting::GetTargetMask() const {
  return target_mask_;
}

void SpellTargeting::SetPendingSourceItem(const ObjectGuid item_guid) {
  pending_source_item_ = item_guid;
}

ObjectGuid SpellTargeting::GetPendingSourceItem() const {
  return pending_source_item_;
}

void SpellTargeting::ClearPendingSourceItem() {
  pending_source_item_ = ObjectGuid();
}

void SpellTargeting::SetItemCursorSource(const ObjectGuid item_guid) {
  item_cursor_source_ = item_guid;
}

ObjectGuid SpellTargeting::GetItemCursorSource() const {
  return item_cursor_source_;
}

std::uint32_t SpellTargeting::GetConfirmationCount() const {
  return confirmation_count_;
}

bool SpellTargeting::UsesManualPreviewFacing() const {
  return preview_uses_manual_facing_;
}

void SpellTargeting::SetManualPreviewFacing(const bool enabled) {
  preview_uses_manual_facing_ = enabled;
}

float SpellTargeting::GetPreviewFacingRadians() const {
  return preview_facing_radians_;
}

void SpellTargeting::SetPreviewFacingRadians(const float radians) {
  preview_facing_radians_ = radians;
}

void SpellTargeting::AdvancePreviewFacingQuarterTurn() {
  preview_facing_radians_ += kPreviewQuarterTurnRadians;
  if (preview_facing_radians_ >= kPreviewFullTurnRadians) {
    preview_facing_radians_ -= kPreviewFullTurnRadians;
  }
}

float SpellTargeting::GetPreviewScale() const {
  return preview_scale_;
}

void SpellTargeting::SetPreviewScale(const float scale) {
  preview_scale_ = scale;
}

void SpellTargeting::ResetPreview() {
  preview_uses_manual_facing_ = false;
  preview_facing_radians_ = 0.0f;
  preview_scale_ = 1.0f;
}

void SpellTargeting::Reset() {
  if (state_.isActive) {
    PublishTargetingSlotOccupied(false);
  }
  state_ = SpellTargetingState{};
  target_mask_ = 0;
  max_range_ = 0.0f;
  min_range_ = 0.0f;
  last_confirmed_target_ = ObjectGuid{};
  last_ground_x_ = 0.0f;
  last_ground_y_ = 0.0f;
  last_ground_z_ = 0.0f;
  current_target_ = ObjectGuid{};
  focus_target_ = ObjectGuid{};
  target_of_target_ = ObjectGuid{};
  ClearPendingSourceItem();
  item_cursor_source_ = ObjectGuid{};
  confirmation_count_ = 0;
  ResetPreview();
}

}
