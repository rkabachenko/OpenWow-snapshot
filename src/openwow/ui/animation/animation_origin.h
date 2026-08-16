
#pragma once

#include "openwow/ui/animation/animation_coordinate_space.h"
#include "openwow/ui/ui_enum_helpers.h"

#include <cstdint>
#include <string>

namespace openwow::ui::anim {

class AnimationOrigin {
public:
  void SetStored(const std::uint32_t point, const float stored_x, const float stored_y) {
    point_ = point;
    stored_x_ = stored_x;
    stored_y_ = stored_y;
  }

  void SetPixels(const std::string &point, const float offset_x, const float offset_y) {
    int parsed_point = 4;
    openwow::ui::StringToFramePoint(point.c_str(), &parsed_point);
    SetStored(static_cast<std::uint32_t>(parsed_point), PixelAnimationOffsetToStored(offset_x),
              PixelAnimationOffsetToStored(offset_y));
  }

  void GetStored(std::string &point, float &stored_x, float &stored_y) const {
    point = openwow::ui::FramePointToString(static_cast<int>(point_));
    stored_x = stored_x_;
    stored_y = stored_y_;
  }

  [[nodiscard]] std::uint32_t GetStoredPoint() const noexcept { return point_; }
  [[nodiscard]] float GetStoredX() const noexcept { return stored_x_; }
  [[nodiscard]] float GetStoredY() const noexcept { return stored_y_; }

private:
  std::uint32_t point_ = 4;
  float stored_x_ = 0.0f;
  float stored_y_ = 0.0f;
};

}
