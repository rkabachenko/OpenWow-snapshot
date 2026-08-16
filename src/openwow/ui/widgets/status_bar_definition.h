#pragma once

#include "openwow/ui/widgets/status_bar_value_state.h"

#include <cstdint>
#include <optional>
#include <string>

namespace openwow::ui::widgets {

enum class StatusBarOrientation : std::uint8_t { Horizontal, Vertical };

struct StatusBarColor {
  float red{1.0F};
  float green{1.0F};
  float blue{1.0F};
  float alpha{1.0F};
};

enum class ParsedStatusBarOrientationKind : std::uint8_t {
  Unspecified,
  Horizontal,
  Vertical,
  Invalid,
};

struct ParsedStatusBarOrientation {
  ParsedStatusBarOrientationKind kind{
      ParsedStatusBarOrientationKind::Unspecified};
  std::string invalid_token;
};

struct StatusBarDefinition {
  std::string texture_path;
  std::optional<StatusBarColor> color;
  std::optional<float> minimum;
  std::optional<float> maximum;
  std::optional<float> default_value;
  ParsedStatusBarOrientation orientation;
  std::optional<bool> rotates_texture;
};

void InheritMissingStatusBarDefinition(
    std::optional<StatusBarDefinition>& destination,
    const std::optional<StatusBarDefinition>& source);

}
