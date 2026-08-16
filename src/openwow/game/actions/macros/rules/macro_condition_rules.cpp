#include "openwow/game/actions/macros/rules/macro_condition_rules.h"

#include "openwow/game/actions/macros/rules/retail_macro_text.h"

#include <algorithm>
#include <cctype>
#include <vector>

namespace openwow::game::actions::macros::rules {
namespace {

constexpr std::size_t kArgumentLimit = 1023;

bool EqualsNoCase(const std::string_view lhs,
                  const std::string_view rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t index = 0; index < lhs.size(); ++index) {
    if (std::tolower(static_cast<unsigned char>(lhs[index])) !=
        std::tolower(static_cast<unsigned char>(rhs[index]))) {
      return false;
    }
  }
  return true;
}

bool StartsWithNoCase(const std::string_view value,
                      const std::string_view prefix) {
  return value.size() >= prefix.size() &&
         EqualsNoCase(value.substr(0, prefix.size()), prefix);
}

struct Condition {
  std::string_view name;
  std::string_view arguments;
  bool has_arguments{false};
  bool negate{false};
};

bool IsKnown(const std::string_view name) {
  return name == "combat" || name == "exists" || name == "help" ||
         name == "harm" || name == "party" || name == "raid" ||
         name == "dead" || name == "flyable" || name == "indoors" ||
         name == "outdoors" || name == "swimming" ||
         name == "flying" || name == "mounted" ||
         name == "stealth" || name == "group" ||
         name == "stance" || name == "form" ||
         name == "modifier" || name == "mod" ||
         name == "button" || name == "btn" ||
         name == "actionbar" || name == "bar" ||
         name == "bonusbar" || name == "equipped" ||
         name == "worn" || name == "pet" ||
         name == "channeling" || name == "spec" ||
         name == "vehicleui" || name == "unithasvehicleui" ||
         name == "cursor";
}

Condition ParseCondition(std::string_view token) {
  Condition condition;
  if (token.size() > 2 && StartsWithNoCase(token, "no")) {
    condition.negate = true;
    token.remove_prefix(2);
  }
  const auto split =
      SplitRetailMacroToken(token, 0, token.size(), ':');
  condition.name = split.value;
  if (split.raw_end + 1 < token.size()) {
    condition.has_arguments = true;
    condition.arguments = token.substr(split.raw_end + 1);
  }
  return condition;
}

template <typename Callback>
void ForEachToken(const std::string_view text, const char separator,
                  Callback&& callback) {
  std::size_t cursor = 0;
  while (cursor <= text.size()) {
    const auto token =
        SplitRetailMacroToken(text, cursor, text.size(), separator);
    callback(token.value);
    if (token.raw_end >= text.size()) {
      break;
    }
    cursor = token.raw_end + 1;
  }
}

std::string_view Truncate(std::string_view value) {
  return value.substr(0, std::min(value.size(), kArgumentLimit));
}

bool Boolean(const bool value, const Condition& condition) {
  if (!condition.has_arguments) {
    return value;
  }
  const auto first =
      SplitRetailMacroToken(condition.arguments, 0,
                            condition.arguments.size(), '/');
  return static_cast<std::uint32_t>(value) ==
         ParseRetailMacroUnsignedPrefix(first.value);
}

bool MatchesNumberList(const Condition& condition,
                       const std::uint32_t value) {
  if (!condition.has_arguments) {
    return false;
  }
  bool matched = false;
  ForEachToken(condition.arguments, '/', [&](const std::string_view item) {
    matched |= ParseRetailMacroUnsignedPrefix(item) == value;
  });
  return matched;
}

bool MatchesTextList(const Condition& condition,
                     const std::string_view value,
                     const bool reverse = false) {
  if (!condition.has_arguments) {
    return !value.empty();
  }
  std::vector<std::string_view> arguments;
  ForEachToken(condition.arguments, '/', [&](const std::string_view item) {
    arguments.push_back(Truncate(item));
  });
  if (reverse) {
    std::reverse(arguments.begin(), arguments.end());
  }
  return std::any_of(
      arguments.begin(), arguments.end(),
      [&](const std::string_view item) {
        return EqualsNoCase(item, value);
      });
}

bool EvaluateOne(const Condition& condition,
                 const MacroConditionSnapshot& state) {
  const auto name = condition.name;
  if (name.empty()) {
    return !condition.negate;
  }
  bool value = false;
  if (name == "combat") value = Boolean(state.combat, condition);
  else if (name == "exists") value = Boolean(state.target.exists, condition);
  else if (name == "help") value = Boolean(state.target.can_help, condition);
  else if (name == "harm") value = Boolean(state.target.can_harm, condition);
  else if (name == "party") value = Boolean(state.target.in_party, condition);
  else if (name == "raid") value = Boolean(state.target.in_raid, condition);
  else if (name == "dead") value = Boolean(state.target.dead, condition);
  else if (name == "flyable") value = Boolean(state.flyable, condition);
  else if (name == "indoors") value = Boolean(state.indoors, condition);
  else if (name == "outdoors") value = Boolean(state.outdoors, condition);
  else if (name == "swimming") value = Boolean(state.swimming, condition);
  else if (name == "flying") value = Boolean(state.flying, condition);
  else if (name == "mounted") value = Boolean(state.mounted, condition);
  else if (name == "stealth") value = Boolean(state.stealth, condition);
  else if (name == "vehicleui") value = state.vehicle_ui;
  else if (name == "unithasvehicleui") value = state.target.has_vehicle_ui;
  else if (name == "stance" || name == "form") {
    value = condition.has_arguments
                ? MatchesNumberList(condition, state.stance)
                : state.stance != 0;
  } else if (name == "actionbar" || name == "bar") {
    value = MatchesNumberList(condition, state.action_bar_page.value());
  } else if (name == "bonusbar") {
    value = MatchesNumberList(condition, state.bonus_bar);
  } else if (name == "spec") {
    value = MatchesNumberList(condition, state.specialization);
  } else if (name == "group") {
    if (!condition.has_arguments) {
      value = state.in_party || state.in_raid;
    } else {
      ForEachToken(condition.arguments, '/', [&](const std::string_view item) {
        value |= (item == "party" && state.in_party) ||
                 (item == "raid" && state.in_raid);
      });
    }
  } else if (name == "button" || name == "btn") {
    if (condition.has_arguments && state.button_matches) {
      ForEachToken(condition.arguments, '/', [&](const std::string_view item) {
        value |= state.button_matches(Truncate(item));
      });
    }
  } else if (name == "modifier" || name == "mod") {
    if (state.modifier_matches && !condition.has_arguments) {
      value = state.modifier_matches(std::nullopt);
    } else if (state.modifier_matches) {
      ForEachToken(condition.arguments, '/', [&](std::string_view item) {
        value |= state.modifier_matches(Truncate(item));
      });
    }
  } else if (name == "pet") {
    value = state.pet_name.has_value();
    if (condition.has_arguments) {
      value = false;
      ForEachToken(condition.arguments, '/', [&](const std::string_view item) {
        value |= (state.pet_name &&
                  EqualsNoCase(Truncate(item), *state.pet_name)) ||
                 (state.pet_family &&
                  EqualsNoCase(Truncate(item), *state.pet_family));
      });
    }
  } else if (name == "channeling") {
    value = state.channeling &&
            (!condition.has_arguments ||
             (state.channel_spell &&
              MatchesTextList(condition, *state.channel_spell)));
  } else if (name == "cursor") {
    value = state.cursor_type.has_value() &&
            (!condition.has_arguments ||
             MatchesTextList(condition, *state.cursor_type, true));
  } else if (name == "equipped" || name == "worn") {
    if (state.equipped_item_type) {
      ForEachToken(condition.arguments, '/', [&](const std::string_view item) {
        value |= state.equipped_item_type(item);
      });
    }
  }
  return condition.negate ? !value : value;
}

struct ParsedBlock {
  std::string target;
  std::vector<Condition> conditions;
};

ParsedBlock ParseBlock(std::string_view block,
                       const MacroConditionRules::UnknownConditionHandler&
                           report_unknown) {
  ParsedBlock result;
  block = TrimRetailMacroSpaces(block);
  block = block.substr(0, std::min(block.size(), kArgumentLimit));
  ForEachToken(block, ',', [&](const std::string_view token) {
    if (StartsWithNoCase(token, "target=")) {
      result.target = std::string(TrimRetailMacroSpaces(token.substr(7)));
      return;
    }
    if (!token.empty() && token.front() == '@') {
      result.target = std::string(TrimRetailMacroSpaces(token.substr(1)));
      return;
    }
    const auto condition = ParseCondition(token);
    if (!IsKnown(condition.name)) {
      if (!condition.name.empty() && report_unknown) {
        report_unknown(condition.name);
      }
      result.conditions.push_back(
          {.name = {}, .arguments = condition.arguments,
           .has_arguments = condition.has_arguments,
           .negate = condition.negate});
      return;
    }
    result.conditions.push_back(condition);
  });
  return result;
}

}

bool MacroConditionRules::Evaluate(
    const std::string_view conditions,
    const std::string_view target,
    const SnapshotProvider& snapshots,
    const UnknownConditionHandler& report_unknown) {
  if (conditions.empty()) {
    return true;
  }
  const auto state = snapshots ? snapshots(target) : MacroConditionSnapshot{};
  bool matched = true;
  ForEachToken(conditions, ',', [&](const std::string_view token) {
    if (!matched) {
      return;
    }
    auto condition = ParseCondition(token);
    if (!IsKnown(condition.name)) {
      condition.name = {};
    }
    matched = EvaluateOne(condition, state);
  });
  return matched;
}

SecureConditionBlockResult MacroConditionRules::EvaluateBlock(
    const std::string_view condition_block,
    const SnapshotProvider& snapshots,
    const UnknownConditionHandler& report_unknown) {
  const auto parsed = ParseBlock(condition_block, report_unknown);
  const auto state =
      snapshots ? snapshots(parsed.target) : MacroConditionSnapshot{};
  bool matched = true;
  for (const auto& condition : parsed.conditions) {
    if (!EvaluateOne(condition, state)) {
      matched = false;
      break;
    }
  }
  return {.target = parsed.target, .matched = matched};
}

}
