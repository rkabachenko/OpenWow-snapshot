#include "openwow/ui/widgets/slider_definition.h"

namespace openwow::ui::widgets {

void InheritMissingSliderDefinition(
    std::optional<SliderDefinition>& destination,
    const std::optional<SliderDefinition>& source) {
  if (!source.has_value()) {
    return;
  }
  if (!destination.has_value()) {
    destination = source;
    return;
  }

  auto& derived = *destination;
  const auto& base = *source;
  if (!derived.minimum.has_value()) {
    derived.minimum = base.minimum;
  }
  if (!derived.maximum.has_value()) {
    derived.maximum = base.maximum;
  }
  if (!derived.default_value.has_value()) {
    derived.default_value = base.default_value;
  }
  if (!derived.value_step.has_value()) {
    derived.value_step = base.value_step;
  }
  if (derived.orientation.kind == ParsedSliderOrientationKind::Unspecified) {
    derived.orientation = base.orientation;
  }
}

}
