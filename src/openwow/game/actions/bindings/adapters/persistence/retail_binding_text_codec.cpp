#include "openwow/game/actions/bindings/adapters/persistence/retail_binding_text_codec.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <unordered_map>

namespace openwow::game::actions::bindings::adapters::persistence {
namespace {

std::string UppercaseAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](const unsigned char character) {
                   return static_cast<char>(std::toupper(character));
                 });
  return value;
}

bool StartsWithIgnoreCase(std::string_view value, std::string_view prefix) {
  if (value.size() < prefix.size()) {
    return false;
  }
  for (std::size_t index = 0; index < prefix.size(); ++index) {
    const auto lhs = static_cast<unsigned char>(value[index]);
    const auto rhs = static_cast<unsigned char>(prefix[index]);
    if (std::toupper(lhs) != std::toupper(rhs)) {
      return false;
    }
  }
  return true;
}

std::string_view TrimLinePrefix(std::string_view line) {
  while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
    line.remove_prefix(1);
  }
  return line;
}

bool ConsumeToken(std::string_view& cursor,
                  std::string_view delimiters,
                  std::string_view& token) {
  while (!cursor.empty() &&
         delimiters.find(cursor.front()) != std::string_view::npos) {
    cursor.remove_prefix(1);
  }
  if (cursor.empty()) {
    token = {};
    return false;
  }

  const std::size_t token_length = cursor.find_first_of(delimiters);
  if (token_length == std::string_view::npos) {
    token = cursor;
    cursor = {};
    return true;
  }

  token = cursor.substr(0, token_length);
  cursor.remove_prefix(token_length + 1);
  return true;
}

unsigned int ParseRetailUnsignedDecimal(std::string_view value) {
  if (value.empty()) {
    return 0;
  }

  const bool negative = value.front() == '-';
  if (negative) {
    value.remove_prefix(1);
  }
  if (value.empty() || value.front() < '0' || value.front() > '9') {
    return 0;
  }

  unsigned int result = static_cast<unsigned int>(value.front() - '0');
  value.remove_prefix(1);
  while (!value.empty() && value.front() >= '0' && value.front() <= '9') {
    result = result * 10u + static_cast<unsigned int>(value.front() - '0');
    value.remove_prefix(1);
  }
  return negative ? 0u - result : result;
}

}

std::string NormalizeRetailBindingChord(const std::string_view chord) {
  static const std::unordered_map<std::string, std::string> kTranslations = {
      {"LEFTBRACKET", "["},  {"RIGHTBRACKET", "]"}, {"SLASH", "/"},
      {"BACKSLASH", "\\"},   {"SEMICOLON", ";"},    {"APOSTROPHE", "'"},
      {"COMMA", ","},         {"PERIOD", "."},       {"TILDE", "`"},
      {"PLUS", "="},          {"MINUS", "-"},
  };

  std::string normalized = UppercaseAscii(std::string(chord));
  const auto translation = kTranslations.find(normalized);
  return translation == kTranslations.end() ? std::move(normalized)
                                             : translation->second;
}

ParsedBindingDocument ParseRetailBindingDocument(std::string_view text) {
  ParsedBindingDocument document;
  BindingSlot active_slot = BindingSlot::Primary();
  std::string_view line;

  while (ConsumeToken(text, "\r\n", line)) {
    const std::string_view trimmed = TrimLinePrefix(line);
    if (trimmed.empty()) {
      continue;
    }

    if (StartsWithIgnoreCase(trimmed, "BINDINGMODE ")) {
      const unsigned int slot_value =
          ParseRetailUnsignedDecimal(trimmed.substr(12));
      if (slot_value <= 3u) {
        active_slot =
            *BindingSlot::FromValue(static_cast<std::uint8_t>(slot_value));
      }
      continue;
    }

    if (StartsWithIgnoreCase(trimmed, "bind ")) {
      std::string_view remainder = trimmed.substr(5);
      std::string_view chord;
      if (!ConsumeToken(remainder, " ", chord) || chord.empty()) {
        continue;
      }
      document.bindings.push_back(
          {active_slot,
           BindingChord(NormalizeRetailBindingChord(chord)),
           BindingCommand(remainder.empty() ? "NONE" : std::string(remainder))});
      continue;
    }

    if (StartsWithIgnoreCase(trimmed, "modifiedclick ")) {
      std::string_view remainder = trimmed.substr(14);
      std::string_view binding;
      if (!ConsumeToken(remainder, " ", binding) || binding.empty() ||
          remainder.empty()) {
        continue;
      }
      document.modified_clicks.push_back(
          {ModifiedClickAction(std::string(remainder)), std::string(binding)});
    }
  }
  return document;
}

std::string SerializeRetailModifiedClick(const std::uint8_t modifier_bits,
                                         const std::uint8_t button_index,
                                         const bool has_button_token) {
  std::string value;
  const auto append_modifier =
      [&value, modifier_bits](const std::uint8_t both,
                              const std::uint8_t left,
                              const std::uint8_t right,
                              std::string_view combined_text,
                              std::string_view left_text,
                              std::string_view right_text) {
        if ((modifier_bits & both) == both) {
          value += combined_text;
        } else if ((modifier_bits & left) != 0u) {
          value += left_text;
        } else if ((modifier_bits & right) != 0u) {
          value += right_text;
        }
      };
  append_modifier(0x30u, 0x10u, 0x20u, "ALT-", "LALT-", "RALT-");
  append_modifier(0x0Cu, 0x04u, 0x08u, "CTRL-", "LCTRL-", "RCTRL-");
  append_modifier(0x03u, 0x01u, 0x02u, "SHIFT-", "LSHIFT-", "RSHIFT-");

  if (has_button_token) {
    value += "BUTTON";
    value += std::to_string(button_index);
  } else if (!value.empty()) {
    value.pop_back();
  }
  return value.empty() ? "NONE" : value;
}

}
