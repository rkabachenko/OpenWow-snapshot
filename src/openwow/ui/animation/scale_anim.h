
#pragma once

#include "openwow/ui/animation/animation.h"
#include "openwow/ui/animation/animation_origin.h"

#include <string>

namespace openwow::ui::anim {

class ScaleAnim : public Animation {
public:
  AnimKind GetKind() const override {
    return AnimKind::Scale;
  }

  void SetScaleDelta(float x, float y);
  void GetScaleDelta(float &x, float &y) const;

  void SetFromScale(float x, float y) {
    from_x_ = x;
    from_y_ = y;
  }
  void GetFromScale(float &x, float &y) const {
    x = from_x_;
    y = from_y_;
  }

  void SetToScale(float x, float y) {
    StoreTargetScale(x, y);
  }
  void GetToScale(float &x, float &y) const {
    x = to_x_;
    y = to_y_;
  }

  void SetOriginStored(std::uint32_t point, float offsetX, float offsetY) {
    origin_.SetStored(point, offsetX, offsetY);
  }

  void SetOriginPixels(const std::string &point, float offsetX, float offsetY) {
    origin_.SetPixels(point, offsetX, offsetY);
  }

  void GetOriginStored(std::string &point, float &x, float &y) const {
    origin_.GetStored(point, x, y);
  }

  void Apply(float progress) override;
  void ResetEffect() override;
  void ApplySignedFactor(float factor) override;

  float GetCurrentScaleX() const {
    return current_x_;
  }
  float GetCurrentScaleY() const {
    return current_y_;
  }

private:
  void StoreTargetScale(float x, float y) {
    to_x_ = x;
    to_y_ = y;
    delta_x_ = 1.0f - x;
    delta_y_ = 1.0f - y;
  }

  float from_x_ = 1.0f, from_y_ = 1.0f;
  float to_x_ = 1.0f, to_y_ = 1.0f;
  float current_x_ = 1.0f, current_y_ = 1.0f;
  float delta_x_ = 0.0f, delta_y_ = 0.0f;

  AnimationOrigin origin_{};
};

}
