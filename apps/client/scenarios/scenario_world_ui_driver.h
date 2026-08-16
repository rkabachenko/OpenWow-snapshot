#pragma once

#include "scenario_runner.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace openwow::ui::game {
class GameUIManager;
}

namespace openwow::client {

class ScenarioWorldUiDriver {
 public:
  [[nodiscard]] ScenarioWorldUiActionResult Exercise(
      openwow::ui::game::GameUIManager& manager,
      ScenarioWorldUiAction action);

  void Reset() noexcept;

 private:
  [[nodiscard]] ScenarioWorldUiActionResult RestoreTransientState(
      openwow::ui::game::GameUIManager& manager);

  std::unordered_map<std::string, std::string> original_cvars_;
  std::optional<double> original_minimap_zoom_;
  std::optional<std::uint64_t> original_mouseover_guid_;
  bool staged_action_target_{false};
  bool action_target_fallback_used_{false};
  bool action_target_fallback_reported_{false};
};

}
