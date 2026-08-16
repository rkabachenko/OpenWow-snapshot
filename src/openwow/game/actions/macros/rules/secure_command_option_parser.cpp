#include "openwow/game/actions/macros/rules/secure_command_option_parser.h"

#include <algorithm>
#include <cctype>

namespace openwow::game::actions::macros::rules {
namespace {

constexpr std::size_t kRetailConditionTextLimit = 1023;

std::string_view TrimRetailSpaces(std::string_view value) {
  while (!value.empty() && value.front() == ' ') {
    value.remove_prefix(1);
  }
  while (!value.empty() && value.back() == ' ') {
    value.remove_suffix(1);
  }
  return value;
}

std::string TruncateCondition(std::string_view value) {
  const auto trimmed = TrimRetailSpaces(value);
  return std::string(
      trimmed.substr(0, std::min(trimmed.size(),
                                 kRetailConditionTextLimit)));
}

}

SecureCommandOptionResult SecureCommandOptionParser::Parse(
    std::string_view options,
    const ConditionEvaluator& evaluate_condition) {
  SecureCommandOptionResult result;
  std::size_t cursor = 0;

  while (true) {
    bool saw_condition_block = false;
    bool clause_matched = false;
    std::string matched_target;
    std::size_t value_begin = cursor;

    while (true) {
      value_begin = cursor;
      while (cursor < options.size() && options[cursor] != '[' &&
             options[cursor] != ';') {
        ++cursor;
      }
      if (cursor >= options.size() || options[cursor] == ';') {
        break;
      }

      saw_condition_block = true;
      std::size_t condition_begin = cursor + 1;
      while (condition_begin < options.size() &&
             options[condition_begin] == ' ') {
        ++condition_begin;
      }

      std::size_t condition_end = condition_begin;
      while (condition_end < options.size() &&
             options[condition_end] != ']') {
        ++condition_end;
      }
      std::size_t trimmed_condition_end = condition_end;
      while (trimmed_condition_end > condition_begin &&
             options[trimmed_condition_end - 1] == ' ') {
        --trimmed_condition_end;
      }

      if (!clause_matched) {
        const auto remembered = RememberCondition(options.substr(
            condition_begin,
            trimmed_condition_end - condition_begin));
        const auto evaluated = evaluate_condition(remembered);
        clause_matched = evaluated.matched;
        if (clause_matched) {
          matched_target = evaluated.target;
        }
      }
      cursor = condition_end + 1;
    }

    if (!saw_condition_block || clause_matched) {
      const auto value_end = std::min(cursor, options.size());
      const auto trimmed_begin = std::min(value_begin, value_end);
      const auto value = TrimRetailSpaces(
          options.substr(trimmed_begin, value_end - trimmed_begin));
      result.value.assign(value);
      result.target = std::move(matched_target);
      result.matched = true;
      return result;
    }

    if (cursor >= options.size()) {
      return result;
    }
    ++cursor;
  }
}

void SecureCommandOptionParser::Reset() {
  std::lock_guard lock(mutex_);
  condition_text_by_folded_key_.clear();
}

std::string SecureCommandOptionParser::RememberCondition(
    std::string_view condition) {
  const auto truncated = TruncateCondition(condition);
  std::string folded_key = truncated;
  std::transform(
      folded_key.begin(), folded_key.end(), folded_key.begin(),
      [](const unsigned char value) {
        return static_cast<char>(std::tolower(value));
      });

  std::lock_guard lock(mutex_);
  const auto [it, inserted] =
      condition_text_by_folded_key_.try_emplace(
          std::move(folded_key), truncated);
  (void)inserted;
  return it->second;
}

}
