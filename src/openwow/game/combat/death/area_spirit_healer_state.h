#pragma once

#include "openwow/game/object_guid.h"

#include <chrono>
#include <cstdint>
#include <optional>

namespace openwow::game::combat::death {

struct AreaSpiritHealerSelectionChange {
  bool changed = false;
  bool cancel_pending_resurrection = false;
  ObjectGuid healer_to_query;
};

class AreaSpiritHealerState {
 public:
  [[nodiscard]] ObjectGuid active_healer() const;
  [[nodiscard]] AreaSpiritHealerSelectionChange SelectHealer(ObjectGuid healer);

  [[nodiscard]] bool StartCountdown(ObjectGuid healer,
                                    std::chrono::milliseconds delay,
                                    std::uint32_t current_tick);
  [[nodiscard]] std::chrono::seconds RemainingTime(std::uint32_t current_tick) const;

  void Reset();

 private:
  ObjectGuid active_healer_;
  std::optional<std::uint32_t> resurrection_deadline_tick_;
};

}
