#include "openwow/ui/widgets/status_bar_definition.h"

namespace openwow::ui::widgets {

void InheritMissingStatusBarDefinition(
    std::optional<StatusBarDefinition>& destination,
    const std::optional<StatusBarDefinition>& source) {
  if (!source.has_value()) {
    return;
  }
  if (!destination.has_value()) {
    destination = source;
    return;
  }

  auto& derived = *destination;
  const auto& base = *source;
  if (derived.texture_path.empty()) {
    derived.texture_path = base.texture_path;
  }
  if (!derived.color.has_value()) {
    derived.color = base.color;
  }
  if (!derived.minimum.has_value()) {
    derived.minimum = base.minimum;
  }
  if (!derived.maximum.has_value()) {
    derived.maximum = base.maximum;
  }
  if (!derived.default_value.has_value()) {
    derived.default_value = base.default_value;
  }
  if (derived.orientation.kind ==
      ParsedStatusBarOrientationKind::Unspecified) {
    derived.orientation = base.orientation;
  }
  if (!derived.rotates_texture.has_value()) {
    derived.rotates_texture = base.rotates_texture;
  }
}

}
