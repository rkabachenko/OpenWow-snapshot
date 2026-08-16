#pragma once

#include <cstdint>

namespace openwow::game::CollisionGlobals {

inline std::uint32_t *s_collisionContext = nullptr;
inline std::uint32_t s_maxCellIndex = 0;
inline std::uint8_t *s_cellNormals = nullptr;
inline std::uint8_t *s_cellEdgeData = nullptr;

}
