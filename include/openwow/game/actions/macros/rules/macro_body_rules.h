#pragma once

#include "openwow/game/actions/macros/rules/secure_command_option_parser.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::game::actions::macros::rules {

struct MacroLine {
  std::string command;
  std::string arguments;
};

struct MacroBodyPresentation {
  bool has_showtooltip{false};
  bool requires_action_bar_icon_updates{false};
};

class MacroBodyRules {
 public:
  using SecureOptionResolver =
      std::function<SecureCommandOptionResult(std::string_view)>;
  static constexpr std::uint32_t kMaxBodyLength = 1023;
  static constexpr std::uint32_t kMaxBodyCodepoints = 256;

  [[nodiscard]] static std::string NormalizeRetailBody(
      std::string_view body);
  [[nodiscard]] static MacroBodyPresentation AnalyzePresentation(
      const std::string& body);
  [[nodiscard]] static std::string ResolveExecutableBody(
      const std::string& body,
      const SecureOptionResolver& resolve_options);
  [[nodiscard]] static std::vector<std::string> SplitLines(
      const std::string& body);
  [[nodiscard]] static MacroLine ParseLine(const std::string& line);
  [[nodiscard]] static bool IsValid(const std::string& body);
};

}
