#pragma once

#include <string>

namespace openwow::ui {

struct MinimapMarkerPresentation {
  float left{0.0f};
  float top{0.0f};
  float right{0.0f};
  float bottom{0.0f};
  std::string tooltip;
  bool transport_layer_mismatch{false};
  bool flight_master{false};

  [[nodiscard]] bool Contains(const float x, const float y) const noexcept {
    return x >= left && x <= right && y >= top && y <= bottom;
  }
};

}
