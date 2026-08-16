
#pragma once

#include "openwow/ui/animation/animation.h"
#include "openwow/ui/animation/animation_origin.h"

#include <string>

namespace openwow::ui::anim {

class RotationAnim : public Animation {
public:
  AnimKind GetKind() const override {
    return AnimKind::Rotation;
  }

  void SetDegrees(double degrees);
  double GetDegrees() const;

  void SetRadians(float radians);
  float GetRadians() const;

  void SetOriginStored(std::uint32_t point, float offsetX, float offsetY) {
    origin_.SetStored(point, offsetX, offsetY);
  }

  void SetOriginPixels(const std::string &point, float offsetX, float offsetY) {
    origin_.SetPixels(point, offsetX, offsetY);
  }

  void GetOriginStored(std::string &point, float &x, float &y) const {
    origin_.GetStored(point, x, y);
  }

  void Apply(float factor) override;
  void ResetEffect() override;

  float GetCurrentRadians() const {
    return current_radians_;
  }

private:
  float radians_ = 0.0f;
  float current_radians_ = 0.0f;
  AnimationOrigin origin_{};
};

}
