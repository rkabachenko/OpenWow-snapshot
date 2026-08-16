#pragma once

#include "openwow/data/terrain/adt_file.h"
#include "openwow/data/terrain/wdt_file.h"
#include "openwow/data/terrain/wmo_placement_scale.h"
#include "openwow/world/coordinates/world_coordinates.h"
#include "openwow/world/coordinates/world_geometry.h"

namespace openwow::world {
namespace detail {

inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kDegreesToRadians = kPi / 180.0f;

[[nodiscard]] inline Matrix4 BuildPlacementMatrix(const float position[3], const float rotation[3],
                                                  const float scale, const Vec3 &origin) {
  Matrix4 matrix = kIdentityMatrix;
  matrix[12] = origin[0] - position[2];
  matrix[13] = origin[1] - position[0];
  matrix[14] = origin[2] + position[1];
  matrix = Multiply(RotationZ(rotation[1] * kDegreesToRadians + kPi), matrix);
  matrix = Multiply(RotationY(rotation[0] * kDegreesToRadians), matrix);
  matrix = Multiply(RotationX(rotation[2] * kDegreesToRadians), matrix);
  if (scale != 1.0f) {
    for (std::size_t row = 0; row < 3; ++row) {
      for (std::size_t column = 0; column < 4; ++column) {
        matrix[row * 4 + column] *= scale;
      }
    }
  }
  return matrix;
}

}

[[nodiscard]] inline Matrix4
BuildDoodadModelMatrix(const data::terrain::DoodadPlacement &placement) {
  return detail::BuildPlacementMatrix(placement.position, placement.rotation,
                                      static_cast<float>(placement.scale) / 1024.0f,
                                      {kMapHalfSize, kMapHalfSize, 0.0f});
}

[[nodiscard]] inline Matrix4 BuildWmoModelMatrix(const data::terrain::WmoPlacement &placement) {
  return detail::BuildPlacementMatrix(placement.position, placement.rotation,
                                      data::terrain::DecodeWmoPlacementScale(placement.scale),
                                      {kMapHalfSize, kMapHalfSize, 0.0f});
}

[[nodiscard]] inline Matrix4 BuildWmoModelMatrix(const data::terrain::WdtWmoPlacement &placement) {
  return detail::BuildPlacementMatrix(placement.position, placement.rotation,
                                      data::terrain::DecodeWmoPlacementScale(placement.scale), {});
}

[[nodiscard]] inline Bounds BuildWmoWorldBounds(const data::terrain::WmoPlacement &placement) {
  return {
      kMapHalfSize - placement.bounds_upper[2],
      kMapHalfSize - placement.bounds_upper[0],
      placement.bounds_lower[1],
      kMapHalfSize - placement.bounds_lower[2],
      kMapHalfSize - placement.bounds_lower[0],
      placement.bounds_upper[1],
  };
}

[[nodiscard]] inline Bounds BuildWmoWorldBounds(const data::terrain::WdtWmoPlacement &placement) {
  return {
      -placement.bounds_upper[2], -placement.bounds_upper[0], placement.bounds_lower[1],
      -placement.bounds_lower[2], -placement.bounds_lower[0], placement.bounds_upper[1],
  };
}

}
