#include "openwow/world/terrain/ground_effect_map.h"

#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/world/coordinates/world_coordinates.h"

#include <algorithm>
#include <array>
#include <string>

namespace openwow::world {

std::array<std::uint32_t, kTerrainGroundEffectCellCount>
BuildTerrainChunkGroundEffectIdGrid(
    const data::terrain::TerrainChunk& chunk) {
  std::array<std::uint32_t, kTerrainGroundEffectCellCount> effect_ids{};
  const std::size_t layer_count = std::min<std::size_t>(chunk.layers.size(), 4u);
  if (layer_count == 0u) {
    return effect_ids;
  }

  bool reported_invalid_layer = false;

  for (std::size_t cell_row = 0u;
       cell_row < kTerrainGroundEffectCellsPerSide; ++cell_row) {
    const std::size_t packed_row_offset = cell_row * 2u;
    const std::uint16_t packed_layers =
        static_cast<std::uint16_t>(
            chunk.header.low_quality_texmap[packed_row_offset]) |
        static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(
                chunk.header.low_quality_texmap[packed_row_offset + 1u])
            << 8u);
    for (std::size_t cell_column = 0u;
         cell_column < kTerrainGroundEffectCellsPerSide; ++cell_column) {
      const std::size_t cell_index =
          cell_row * kTerrainGroundEffectCellsPerSide + cell_column;
      if (IsTerrainHoleCell(chunk.holes, static_cast<int>(cell_row),
                            static_cast<int>(cell_column))) {
        continue;
      }

      const std::size_t layer_index =
          (packed_layers >> (cell_column * 2u)) & 0x3u;
      if (layer_index >= layer_count) {
        if (!reported_invalid_layer) {
          reported_invalid_layer = true;
          diagnostics::Log(
              diagnostics::LogLevel::kWarn,
              "TerrainGroundEffectMap: low-quality texture layer is out of "
              "range chunk=" +
                  std::to_string(chunk.header.index_x) + "," +
                  std::to_string(chunk.header.index_y) + " layer=" +
                  std::to_string(layer_index) + " layer_count=" +
                  std::to_string(layer_count));
        }
        continue;
      }
      effect_ids[cell_index] = chunk.layers[layer_index].effect_id;
    }
  }
  return effect_ids;
}

}
