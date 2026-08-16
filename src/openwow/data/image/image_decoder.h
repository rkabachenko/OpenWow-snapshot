#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace openwow::data::image {

struct DecodedImage {
  bool ok{false};
  std::string error;
  int width{0};
  int height{0};
  bool has_alpha_channel{false};
  std::vector<std::uint8_t> pixels_rgba;
};

DecodedImage DecodeImage(const std::vector<std::uint8_t>& bytes);

}
