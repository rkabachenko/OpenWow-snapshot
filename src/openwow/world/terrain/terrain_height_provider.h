#pragma once

#include <optional>

namespace openwow::world {

class TerrainHeightProvider {
 public:
  virtual ~TerrainHeightProvider() = default;

  [[nodiscard]] virtual std::optional<float> GetHeightAt(float x,
                                                         float y) const = 0;

  [[nodiscard]] virtual bool IsLoaded(float x, float y) const = 0;
};

}
