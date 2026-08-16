#pragma once

#include <array>

namespace openwow::render {

struct SelectionTriangleVertices {
  std::array<std::array<float, 3>, 3> positions{};
};

}
