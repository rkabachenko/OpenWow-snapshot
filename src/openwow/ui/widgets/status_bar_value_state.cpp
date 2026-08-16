#include "openwow/ui/widgets/status_bar_value_state.h"

#include <cmath>

namespace openwow::ui::widgets {

StatusBarRangeChange StatusBarValueState::SetRange(
    const float minimum, const float maximum) noexcept {
  const float stored_minimum = minimum <= maximum ? minimum : maximum;
  const bool unchanged = has_range_ && !std::isnan(stored_minimum) &&
                         !std::isnan(minimum_) && stored_minimum == minimum_ &&
                         maximum == maximum_;
  if (unchanged) {
    return {};
  }

  minimum_ = stored_minimum;
  maximum_ = maximum;
  has_range_ = true;
  return {
      .range_changed = true,
      .reapply_value = has_value_,
  };
}

bool StatusBarValueState::SetValue(const float requested_value) noexcept {
  if (!has_range_) {
    return false;
  }

  const float clamped = ClampToRange(requested_value);
  if (has_value_ && clamped == value_) {
    return false;
  }

  value_ = clamped;
  has_value_ = true;
  return true;
}

StatusBarValueSnapshot StatusBarValueState::Snapshot() const noexcept {
  return {
      .minimum = minimum_,
      .maximum = maximum_,
      .value = value_,
      .has_range = has_range_,
      .has_value = has_value_,
  };
}

float StatusBarValueState::ClampToRange(
    const float requested_value) const noexcept {
  float clamped = maximum_;
  if (requested_value <= maximum_) {
    clamped = requested_value;
  }
  if (requested_value < minimum_) {
    clamped = minimum_;
  }
  return clamped;
}

}
