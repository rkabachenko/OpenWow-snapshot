#include "openwow/game/actions/macros/rules/macro_body_rules.h"
#include "openwow/game/actions/macros/rules/retail_macro_text.h"

#include <cctype>
#include <sstream>

namespace openwow::game::actions::macros::rules {
namespace {

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

std::size_t RetailUtf8TruncationOffset(
    const std::string_view text,
    const std::size_t max_codepoints) {
  std::size_t position = 0;
  std::size_t count = 0;
  while (position < text.size() && count < max_codepoints) {
    const auto byte = static_cast<std::uint8_t>(text[position]);
    std::size_t sequence_length = 1;
    if ((byte & 0xE0u) == 0xC0u) {
      sequence_length = 2;
    } else if ((byte & 0xF0u) == 0xE0u) {
      sequence_length = 3;
    } else if ((byte & 0xF8u) == 0xF0u) {
      sequence_length = 4;
    } else if ((byte & 0xFCu) == 0xF8u) {
      sequence_length = 5;
    } else if ((byte & 0xFEu) == 0xFCu) {
      sequence_length = 6;
    }
    if (position + sequence_length > text.size()) {
      break;
    }
    position += sequence_length;
    ++count;
  }
  return position;
}

enum class PresentationCommand {
  kNone,
  kCastOrUse,
  kRandom,
  kCastSequence,
  kEquip,
  kEquipSlot,
  kShow,
};

PresentationCommand ClassifyPresentationCommand(
    const std::string_view command) {
  if (EqualsNoCase(command, "#show") ||
      EqualsNoCase(command, "#showtooltip")) {
    return PresentationCommand::kShow;
  }
  if (EqualsNoCase(command, "/cast") ||
      EqualsNoCase(command, "/use")) {
    return PresentationCommand::kCastOrUse;
  }
  if (EqualsNoCase(command, "/castrandom") ||
      EqualsNoCase(command, "/userandom")) {
    return PresentationCommand::kRandom;
  }
  if (EqualsNoCase(command, "/castsequence")) {
    return PresentationCommand::kCastSequence;
  }
  if (EqualsNoCase(command, "/equip")) {
    return PresentationCommand::kEquip;
  }
  if (EqualsNoCase(command, "/equipslot")) {
    return PresentationCommand::kEquipSlot;
  }
  return PresentationCommand::kNone;
}

std::string_view StripEquippedSlot(std::string_view value) {
  std::size_t cursor = 0;
  while (cursor < value.size() && value[cursor] >= '0' &&
         value[cursor] <= '9') {
    ++cursor;
  }
  while (cursor < value.size() && value[cursor] == ' ') {
    ++cursor;
  }
  return value.substr(cursor);
}

bool RequiresIconUpdates(const PresentationCommand command,
                         std::string_view arguments) {
  arguments = TrimRetailMacroSpaces(arguments);
  if (arguments.empty()) {
    return false;
  }
  if (command == PresentationCommand::kCastSequence ||
      arguments.front() == '[') {
    return true;
  }
  if (command == PresentationCommand::kEquipSlot) {
    arguments = TrimRetailMacroSpaces(StripEquippedSlot(arguments));
  }
  return !arguments.empty() && arguments.front() >= '0' &&
         arguments.front() <= '9';
}

}

std::string MacroBodyRules::NormalizeRetailBody(
    const std::string_view body) {
  std::string normalized = CopyRetailMacroSpan(body, kMaxBodyLength + 1);
  const auto end =
      RetailUtf8TruncationOffset(normalized, kMaxBodyCodepoints);
  if (end < normalized.size()) {
    normalized.erase(end);
  }
  return normalized;
}

MacroBodyPresentation MacroBodyRules::AnalyzePresentation(
    const std::string& body) {
  MacroBodyPresentation result;
  for (const auto& line : SplitLines(body)) {
    const auto command =
        SplitRetailMacroToken(line, 0, line.size(), ' ');
    if (EqualsNoCase(command.value, "#showtooltip")) {
      result.has_showtooltip = true;
    }
    const auto kind = ClassifyPresentationCommand(command.value);
    if (kind == PresentationCommand::kNone) {
      continue;
    }
    const auto arguments =
        SplitRetailMacroToken(line, command.raw_end, line.size(), '\0');
    result.requires_action_bar_icon_updates |=
        RequiresIconUpdates(kind, arguments.value);
  }
  return result;
}

std::string MacroBodyRules::ResolveExecutableBody(
  const std::string& body,
  const SecureOptionResolver& resolve_options) {
  std::string result;
  std::istringstream stream(body);
  std::string line;
  while (std::getline(stream, line)) {
    const auto command =
        SplitRetailMacroToken(line, 0, line.size(), ' ');
    if (command.value.empty()) {
      result += line;
      result.push_back('\n');
      continue;
    }

    const auto arguments =
        SplitRetailMacroToken(line, command.raw_end, line.size(), '\0');
    if (EqualsNoCase(command.value, "#showtooltip")) {
      result += "#showtooltip";
      if (!arguments.value.empty()) {
        result.push_back(' ');
        result.append(arguments.value);
      }
      result.push_back('\n');
      continue;
    }

    if (EqualsNoCase(command.value, "/cast") ||
        EqualsNoCase(command.value, "/use")) {
      const auto resolved = resolve_options(arguments.value);
      if (resolved.matched && !resolved.value.empty()) {
        result.append(command.value);
        result.push_back(' ');
        result += resolved.value;
        result.push_back('\n');
      }
      continue;
    }

    result += line;
    result.push_back('\n');
  }

  if (!result.empty()) {
    result.pop_back();
  }
  return result;
}

std::vector<std::string> MacroBodyRules::SplitLines(const std::string& body) {
  std::vector<std::string> lines;
  std::size_t cursor = 0;
  while (cursor < body.size()) {
    while (cursor < body.size() &&
           (body[cursor] == '\r' || body[cursor] == '\n')) {
      ++cursor;
    }
    if (cursor >= body.size()) {
      break;
    }
    const std::size_t line_start = cursor;
    while (cursor < body.size() && body[cursor] != '\r' &&
           body[cursor] != '\n') {
      ++cursor;
    }
    lines.emplace_back(body.substr(line_start, cursor - line_start));
  }
  return lines;
}

MacroLine MacroBodyRules::ParseLine(const std::string& line) {
  if (line.empty() || line.front() != '/') {
    return {.command = {}, .arguments = line};
  }
  const std::size_t space = line.find(' ', 1);
  if (space == std::string::npos) {
    return {.command = line.substr(1), .arguments = {}};
  }
  return {
      .command = line.substr(1, space - 1),
      .arguments = line.substr(space + 1),
  };
}

bool MacroBodyRules::IsValid(const std::string& body) {
  return body.size() <= kMaxBodyLength;
}

}
