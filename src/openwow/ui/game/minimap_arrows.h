#pragma once

#include <cstdint>

namespace openwow::ui {

enum class MinimapRotatingArrowKind : std::uint8_t {
  kStaticPoi = 0,
  kGuidePoi = 1,
  kCorpsePoi = 2,
  kGroupMember = 3,
};

struct MinimapRotatingArrow {
  float angle_radians{0.0f};
  MinimapRotatingArrowKind kind{MinimapRotatingArrowKind::kGuidePoi};
  bool visible_in_indoor_minimap{false};
};

inline constexpr float kMinimapNominalRadiusUiUnits = 70.4f;

inline constexpr float kMinimapRotatingArrowRingRadiusUiUnits =
    kMinimapNominalRadiusUiUnits * 0.8f;
inline constexpr float kMinimapRotatingArrowQuadSpanUiUnits = 57.6f;
inline constexpr float kMinimapVehicleStateBlipQuadSpanUiUnits = 48.0f;
inline constexpr float kMinimapBlipQuadSpanUiUnits = 16.0f;

}
