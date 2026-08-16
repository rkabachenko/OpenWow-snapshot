#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace openwow::ui::widgets {

enum class ParsedSliderOrientationKind : std::uint8_t {
  Unspecified,
  Horizontal,
  Vertical,
  Invalid,
};

struct ParsedSliderOrientation {
  ParsedSliderOrientationKind kind{ParsedSliderOrientationKind::Unspecified};
  std::string invalid_token;
};

struct SliderDefinition {
  std::optional<float> minimum;
  std::optional<float> maximum;
  std::optional<float> default_value;
  std::optional<float> value_step;
  ParsedSliderOrientation orientation;
};

void InheritMissingSliderDefinition(
    std::optional<SliderDefinition>& destination,
    const std::optional<SliderDefinition>& source);

}
