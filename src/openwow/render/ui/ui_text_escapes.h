#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace openwow::render::ui {

struct ColoredTextRun {
  std::string text;
  std::uint32_t color{0xFFFFFFFF};
  bool inherits_default_color{true};
};

class UiTextEscapes final {
 public:
  [[nodiscard]] static std::vector<ColoredTextRun> ParseColorCodes(
      const std::string& text,
      std::uint32_t default_color = 0xFFFFFFFF);
  [[nodiscard]] static std::string StripColorCodes(const std::string& text);
};

}
