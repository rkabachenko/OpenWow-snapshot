#include "openwow/world/environment/environment_detail.h"

#include <algorithm>

namespace openwow::world {

EnvironmentDetailDistances MakeEnvironmentDetailDistances(
    const float requested_scale) {
  EnvironmentDetailDistances distances;
  const float scale = std::clamp(requested_scale, 0.5f, 1.5f);
  constexpr std::array<float, 5> base{30.0f, 100.0f, 200.0f, 750.0f,
                                      1250.0f};
  constexpr std::array<float, 5> fade_width{5.0f, 10.0f, 15.0f, 20.0f,
                                            50.0f};
  for (std::size_t index = 0; index < base.size(); ++index) {
    const float distance =
        index == 0u || index == 4u ? base[index] : base[index] * scale;
    distances.maximum[index] = distance;
    distances.fade_start[index] = distance - fade_width[index];
    distances.maximum_squared[index] = distance * distance;
  }
  return distances;
}

}
