#pragma once

#include "openwow/game/actions/model/action_page.h"
#include "openwow/game/actions/macros/rules/secure_command_option_parser.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace openwow::game::actions::macros::rules {

struct MacroTargetConditionState {
  bool exists{false};
  bool dead{false};
  bool in_party{false};
  bool in_raid{false};
  bool can_help{false};
  bool can_harm{false};
  bool has_vehicle_ui{false};
};

struct MacroConditionSnapshot {
  MacroTargetConditionState target;
  bool in_party{false};
  bool in_raid{false};
  bool combat{false};
  bool stealth{false};
  bool swimming{false};
  bool mounted{false};
  bool flying{false};
  bool flyable{false};
  bool indoors{false};
  bool outdoors{false};
  bool vehicle_ui{false};
  bool channeling{false};
  std::uint32_t stance{0};
  actions::ActionPage action_bar_page{actions::ActionPage::First()};
  std::uint32_t bonus_bar{0};
  std::uint32_t specialization{0};
  std::optional<std::string> pet_name;
  std::optional<std::string> pet_family;
  std::optional<std::string> channel_spell;
  std::optional<std::string> cursor_type;
  std::function<bool(std::optional<std::string_view>)> modifier_matches;
  std::function<bool(std::string_view)> button_matches;
  std::function<bool(std::string_view)> equipped_item_type;
};

class MacroConditionRules {
 public:
  using SnapshotProvider =
      std::function<MacroConditionSnapshot(std::string_view target)>;
  using UnknownConditionHandler = std::function<void(std::string_view)>;

  [[nodiscard]] static bool Evaluate(
      std::string_view conditions,
      std::string_view target,
      const SnapshotProvider& snapshots,
      const UnknownConditionHandler& report_unknown = {});
  [[nodiscard]] static SecureConditionBlockResult EvaluateBlock(
      std::string_view condition_block,
      const SnapshotProvider& snapshots,
      const UnknownConditionHandler& report_unknown = {});
};

}
