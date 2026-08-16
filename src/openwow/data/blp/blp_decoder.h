#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace openwow::data::blp {

struct RgbaImage {
  bool ok{false};
  std::string error;
  std::uint32_t width{0};
  std::uint32_t height{0};
  std::vector<std::uint8_t> pixels_rgba;
};

RgbaImage DecodeBlp(const std::vector<std::uint8_t>& bytes);
RgbaImage DecodeBlpFile(const std::string& file_path);

}
