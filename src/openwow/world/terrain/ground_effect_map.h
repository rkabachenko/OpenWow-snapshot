#pragma once

#include "openwow/data/terrain/adt_file.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace openwow::world {

inline constexpr std::size_t kTerrainGroundEffectCellsPerSide = 8u;
inline constexpr std::size_t kTerrainGroundEffectCellCount =
    kTerrainGroundEffectCellsPerSide * kTerrainGroundEffectCellsPerSide;

[[nodiscard]] std::array<std::uint32_t, kTerrainGroundEffectCellCount>
BuildTerrainChunkGroundEffectIdGrid(
    const data::terrain::TerrainChunk& chunk);

}
