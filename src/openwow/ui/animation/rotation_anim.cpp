
#include "openwow/ui/animation/rotation_anim.h"

#include "openwow/ui/animation/animation_group.h"
#include "openwow/ui/widgets/script_region.h"

namespace openwow::ui::anim {

namespace {

constexpr float kRotationDegreesToRadians = 0.01745329238474369f;
constexpr float kRotationRadiansToDegrees = 57.295780181884766f;

}

void RotationAnim::SetDegrees(double degrees) {
  radians_ = static_cast<float>(degrees * static_cast<double>(kRotationDegreesToRadians));
}

double RotationAnim::GetDegrees() const {
  return static_cast<double>(radians_) * static_cast<double>(kRotationRadiansToDegrees);
}

void RotationAnim::SetRadians(float radians) {
  radians_ = radians;
}

float RotationAnim::GetRadians() const {
  return radians_;
}

void RotationAnim::Apply(float factor) {
  current_radians_ = radians_ * factor;

  if (group_ == nullptr) {
    return;
  }

  auto* owner = group_->GetOwnerRegion();
  if (owner == nullptr) {
    return;
  }

  const float origin_offset[2] = {
      origin_.GetStoredX(),
      origin_.GetStoredY(),
  };

  owner->ApplyAnimRotation(
      static_cast<openwow::ui::widgets::FramePoint>(origin_.GetStoredPoint()),
      origin_offset,
      current_radians_);
}

void RotationAnim::ResetEffect() {
  current_radians_ = 0.0f;
}

}
