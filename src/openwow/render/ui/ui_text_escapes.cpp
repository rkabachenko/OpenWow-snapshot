#include "openwow/render/ui/ui_text_escapes.h"

#include <utility>

namespace openwow::render::ui {
namespace {

bool ParseHexDigit(const char ch, std::uint32_t& value) {
  if (ch >= '0' && ch <= '9') {
    value = static_cast<std::uint32_t>(ch - '0');
    return true;
  }
  if (ch >= 'a' && ch <= 'f') {
    value = static_cast<std::uint32_t>(ch - 'a' + 10);
    return true;
  }
  if (ch >= 'A' && ch <= 'F') {
    value = static_cast<std::uint32_t>(ch - 'A' + 10);
    return true;
  }
  return false;
}

bool ParseHexColor(const std::string& text, const std::size_t offset,
                   std::uint32_t& value) {
  if (offset + 8 > text.size()) {
    return false;
  }

  value = 0;
  for (std::size_t index = 0; index < 8; ++index) {
    std::uint32_t digit = 0;
    if (!ParseHexDigit(text[offset + index], digit)) {
      return false;
    }
    value = (value << 4) | digit;
  }
  return true;
}

}

std::vector<ColoredTextRun> UiTextEscapes::ParseColorCodes(
    const std::string& text, const std::uint32_t default_color) {
  std::vector<ColoredTextRun> runs;
  std::uint32_t current_color = default_color;
  bool inherits_default_color = true;
  std::string current_text;

  for (std::size_t index = 0; index < text.size();) {
    if (text[index] == '|' && index + 1 < text.size()) {
      const char command = text[index + 1];
      if ((command == 'c' || command == 'C') && index + 10 <= text.size()) {
        std::uint32_t argb = 0;
        if (ParseHexColor(text, index + 2, argb)) {
          if (!current_text.empty()) {
            runs.push_back(
                {std::move(current_text), current_color, inherits_default_color});
            current_text.clear();
          }
          const std::uint32_t alpha = (argb >> 24) & 0xFF;
          const std::uint32_t red = (argb >> 16) & 0xFF;
          const std::uint32_t green = (argb >> 8) & 0xFF;
          const std::uint32_t blue = argb & 0xFF;
          current_color =
              (red << 24) | (green << 16) | (blue << 8) | alpha;
          inherits_default_color = false;
          index += 10;
          continue;
        }
      }
      if (command == 'r' || command == 'R') {
        if (!current_text.empty()) {
          runs.push_back(
              {std::move(current_text), current_color, inherits_default_color});
          current_text.clear();
        }
        current_color = default_color;
        inherits_default_color = true;
        index += 2;
        continue;
      }
    }
    current_text.push_back(text[index++]);
  }

  if (!current_text.empty()) {
    runs.push_back(
        {std::move(current_text), current_color, inherits_default_color});
  }
  return runs;
}

std::string UiTextEscapes::StripColorCodes(const std::string& text) {
  std::string result;
  result.reserve(text.size());

  for (std::size_t index = 0; index < text.size();) {
    if (text[index] == '|' && index + 1 < text.size()) {
      const char command = text[index + 1];
      if ((command == 'c' || command == 'C') && index + 10 <= text.size()) {
        std::uint32_t ignored = 0;
        if (ParseHexColor(text, index + 2, ignored)) {
          index += 10;
          continue;
        }
      }
      if (command == 'r' || command == 'R') {
        index += 2;
        continue;
      }
    }
    result.push_back(text[index++]);
  }
  return result;
}

}
