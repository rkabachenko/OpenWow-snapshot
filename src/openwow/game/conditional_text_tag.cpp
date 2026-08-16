
#include "openwow/game/conditional_text_tag.h"

#include <cctype>

namespace openwow::game {
namespace {

std::size_t SkipLeadingAsciiWhitespace(const std::string_view text) {
  std::size_t index = 0;
  while (index < text.size() &&
         std::isspace(static_cast<unsigned char>(text[index])) != 0) {
    ++index;
  }
  return index;
}

std::string_view TrimAscii(std::string_view text) {
  const std::size_t begin = SkipLeadingAsciiWhitespace(text);
  std::size_t end = text.size();
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
    --end;
  }
  return text.substr(begin, end - begin);
}

}

bool TrySelectConditionalTextTag(std::string_view input,
                                 const ConditionalTextTagContext &context,
                                 ConditionalTextTagSelection *selection) {
  if (selection == nullptr) {
    return false;
  }

  const std::size_t begin = SkipLeadingAsciiWhitespace(input);
  if (begin >= input.size()) {
    return false;
  }

  const std::string_view trimmed_input = input.substr(begin);
  const std::size_t first_colon = trimmed_input.find(':');
  const std::size_t semicolon = trimmed_input.find(';');
  if (first_colon == std::string_view::npos ||
      semicolon == std::string_view::npos ||
      first_colon >= semicolon) {
    return false;
  }

  const std::size_t second_colon = trimmed_input.find(':', first_colon + 1);
  const bool has_branch_selector =
      second_colon != std::string_view::npos &&
      second_colon < semicolon &&
      second_colon + 1 < semicolon;

  int selector = context.selector;
  if (has_branch_selector) {
    switch (trimmed_input[second_colon + 1]) {
      case 'C':
      case 'c':
        selector = context.class_selector;
        break;
      case 'R':
      case 'r':
        selector = context.race_selector;
        break;
      default:
        break;
    }
  }

  const std::size_t branch_begin = selector != 0 ? first_colon + 1 : 0;
  const std::size_t branch_end =
      selector != 0
          ? (has_branch_selector ? second_colon : semicolon)
          : first_colon;

  selection->text =
      TrimAscii(trimmed_input.substr(branch_begin, branch_end - branch_begin));
  selection->consumed = begin + semicolon + 1;
  return true;
}

}
