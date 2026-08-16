
#pragma once

#include "openwow/ui/animation/animation.h"
#include "openwow/ui/animation/animation_coordinate_space.h"

namespace openwow::ui::anim {

class TranslationAnim : public Animation {
 public:
  AnimKind GetKind() const override { return AnimKind::Translation; }

  void SetOffset(float x, float y) {
    SetStoredOffset(PixelAnimationOffsetToStored(x), PixelAnimationOffsetToStored(y));
  }

  void GetOffset(float& x, float& y) const {
    x = StoredAnimationOffsetToPixels(stored_offset_x_);
    y = StoredAnimationOffsetToPixels(stored_offset_y_);
  }

  void Apply(float progress) override;
  void ResetEffect() override;

  float GetCurrentX() const { return current_x_; }
  float GetCurrentY() const { return current_y_; }

 private:
  void SetStoredOffset(float x, float y) {
    if (y * y + x * x <= kAnimationStoredOffsetEpsilon) {
      stored_offset_x_ = 0.0f;
      stored_offset_y_ = 0.0f;
      return;
    }

    stored_offset_x_ = x;
    stored_offset_y_ = y;
  }

  float stored_offset_x_ = 0.0f;
  float stored_offset_y_ = 0.0f;
  float current_x_  = 0.0f, current_y_  = 0.0f;
};

}
