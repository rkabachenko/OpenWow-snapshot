
#include "openwow/ui/animation/scale_anim.h"

#include "openwow/ui/animation/animation_group.h"
#include "openwow/ui/widgets/script_region.h"

#include <algorithm>

namespace openwow::ui::anim {

void ScaleAnim::Apply(float progress) {
  current_x_ = from_x_ + (to_x_ - from_x_) * progress;
  current_y_ = from_y_ + (to_y_ - from_y_) * progress;
  ApplySignedFactor(progress);
}

void ScaleAnim::ResetEffect() {
  current_x_ = from_x_;
  current_y_ = from_y_;
}

void ScaleAnim::SetScaleDelta(float x, float y) {
  float cx = (x >= 0.001f) ? x : 0.001f;
  float cy = (y >= 0.001f) ? y : 0.001f;
  StoreTargetScale(cx, cy);
}

void ScaleAnim::GetScaleDelta(float& x, float& y) const {
  x = 1.0f - delta_x_;
  y = 1.0f - delta_y_;
}

void ScaleAnim::ApplySignedFactor(const float factor) {
  auto* grp = GetGroup();
  if (!grp) {
    return;
  }

  auto* parent_region = grp->GetOwnerRegion();
  if (parent_region == nullptr) {

    parent_region = static_cast<openwow::ui::widgets::CScriptRegion*>(
        grp->GetParentFrame());
  }
  if (parent_region == nullptr) {
    return;
  }

  const float scaled_delta[2] = {delta_x_ * factor, delta_y_ * factor};

  const float origin_offset[2] = {origin_.GetStoredX(), origin_.GetStoredY()};

  parent_region->ApplyAnimScale(origin_.GetStoredPoint(), origin_offset,
                                scaled_delta);
}

}
