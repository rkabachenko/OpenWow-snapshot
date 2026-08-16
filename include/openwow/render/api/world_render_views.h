#pragma once

#include <cstdint>

namespace openwow::render {

struct WorldRenderViews {
  std::uint8_t shadow{};
  std::uint8_t sky{};
  std::uint8_t scene{};
  std::uint8_t wmo{};
  std::uint8_t alpha{};
  std::uint8_t reflection{};
  std::uint8_t refraction{};
  std::uint8_t water{};
  std::uint8_t weather{};
};

}
