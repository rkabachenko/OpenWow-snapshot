#pragma once

#include <cstdint>

namespace openwow::foundation::hashing {

struct AdlerSeedState {
  std::uint32_t value{0};
  std::uint32_t packed{0};
};

struct AdlerSeedUnitCircleDirection {
  float x{0.0f};
  float y{0.0f};
};

[[nodiscard]] AdlerSeedState MakeAdlerSeedState(std::uint32_t seed);
[[nodiscard]] std::uint32_t AdvanceAdlerSeed(AdlerSeedState& state);
[[nodiscard]] float AdlerSeedNextUnitFloat(AdlerSeedState& state);
[[nodiscard]] float AdlerSeedNextSignedUnitFloat(AdlerSeedState& state);
[[nodiscard]] float AdlerSeedNextRangeFloat(float lower_bound,
                                            float upper_bound,
                                            AdlerSeedState& state);
[[nodiscard]] std::uint32_t AdlerSeedNextBoundedValue(
    std::uint32_t upper_bound, AdlerSeedState& state);
[[nodiscard]] AdlerSeedUnitCircleDirection
AdlerSeedNextUnitCircleDirection(AdlerSeedState& state);

}
