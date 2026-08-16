#pragma once

#include <array>
#include <cstdint>

namespace openwow::data {

struct ColorRGB {
  float r = 0.0f;
  float g = 0.0f;
  float b = 0.0f;
};

class DBClientColorTable {
 public:
  DBClientColorTable();

  static constexpr uint32_t kColorEntryCount = 25;

  [[nodiscard]] const ColorRGB& GetColor(uint32_t index) const;
  void SetColor(uint32_t index, const ColorRGB& color);

  [[nodiscard]] uint32_t GetStateField(uint32_t index) const;
  void SetStateField(uint32_t index, uint32_t value);

  void Reset();

 private:

  std::array<ColorRGB, kColorEntryCount> colors_{};

  uint32_t state_a_[4] = {};

  uint32_t state_b_ = 0;

  uint32_t state_c_[5] = {};
};

}
