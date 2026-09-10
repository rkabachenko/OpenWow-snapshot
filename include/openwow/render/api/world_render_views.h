#pragma once

#include <cstdint>

namespace openwow::render {

struct WorldRenderViews {
  std::uint8_t shadow{};
  std::uint8_t sky{};
  std::uint8_t low_detail{};
  std::uint8_t foreground_depth_reset{};
  std::uint8_t scene{};
  std::uint8_t wmo{};
  std::uint8_t doodads{};
  std::uint8_t detail_doodads{};
  std::uint8_t alpha{};
  std::uint8_t reflection{};
  std::uint8_t refraction{};
  std::uint8_t water{};
  std::uint8_t weather{};
};

}
