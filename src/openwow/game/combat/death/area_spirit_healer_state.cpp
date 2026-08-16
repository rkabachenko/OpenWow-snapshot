#include "openwow/game/combat/death/area_spirit_healer_state.h"

#include <limits>

namespace openwow::game::combat::death {

ObjectGuid AreaSpiritHealerState::active_healer() const {
  return active_healer_;
}

AreaSpiritHealerSelectionChange AreaSpiritHealerState::SelectHealer(
    const ObjectGuid healer) {
  if (active_healer_ == healer) {
    return {};
  }

  AreaSpiritHealerSelectionChange change;
  change.changed = true;
  change.cancel_pending_resurrection = resurrection_deadline_tick_.has_value();
  change.healer_to_query = healer;

  active_healer_ = healer;
  resurrection_deadline_tick_.reset();
  return change;
}

bool AreaSpiritHealerState::StartCountdown(
    const ObjectGuid healer,
    const std::chrono::milliseconds delay,
    const std::uint32_t current_tick) {
  if (healer.IsEmpty() || healer != active_healer_ ||
      delay <= std::chrono::milliseconds::zero() ||
      delay.count() > std::numeric_limits<std::uint32_t>::max()) {
    return false;
  }

  auto deadline = current_tick + static_cast<std::uint32_t>(delay.count());
  if (deadline == 0u) {
    deadline = 1u;
  }
  resurrection_deadline_tick_ = deadline;
  return true;
}

std::chrono::seconds AreaSpiritHealerState::RemainingTime(
    const std::uint32_t current_tick) const {
  if (!resurrection_deadline_tick_.has_value()) {
    return std::chrono::seconds::zero();
  }

  const auto remaining_milliseconds = static_cast<std::int32_t>(
      *resurrection_deadline_tick_ - current_tick);
  if (remaining_milliseconds <= 0) {
    return std::chrono::seconds::zero();
  }

  return std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::milliseconds(remaining_milliseconds));
}

void AreaSpiritHealerState::Reset() {
  active_healer_ = {};
  resurrection_deadline_tick_.reset();
}

}
