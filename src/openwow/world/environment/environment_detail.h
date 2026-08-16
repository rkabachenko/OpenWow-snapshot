#pragma once

#include <array>

namespace openwow::world {

struct EnvironmentDetailDistances {
  std::array<float, 5> size_class{1.0f, 4.0f, 15.0f, 100.0f, 100000.0f};
  std::array<float, 5> maximum{30.0f, 100.0f, 200.0f, 750.0f, 1250.0f};
  std::array<float, 5> fade_start{25.0f, 90.0f, 185.0f, 730.0f, 1200.0f};
  std::array<float, 5> maximum_squared{
      900.0f, 10000.0f, 40000.0f, 562500.0f, 1562500.0f};

  [[nodiscard]] bool operator==(const EnvironmentDetailDistances&) const =
      default;
};

[[nodiscard]] EnvironmentDetailDistances MakeEnvironmentDetailDistances(
    float scale);

}
