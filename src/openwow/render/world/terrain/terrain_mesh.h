#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "openwow/data/terrain/adt_file.h"

namespace openwow::render {

static constexpr int kMaxTerrainLayers = 4;

static constexpr int kAlphaMapSize = 64;

static constexpr int kTerrainAlphaAtlasChunksPerAxis = data::terrain::kChunksPerSide;
static constexpr int kTerrainAlphaAtlasSize = kAlphaMapSize * kTerrainAlphaAtlasChunksPerAxis;

struct TerrainVertex {
  float position[3];
  float normal[3];
  float texcoord[2];
  float alpha_texcoord[2];
  uint32_t color;

  uint8_t layer_slice[kMaxTerrainLayers];
};
static_assert(sizeof(TerrainVertex) == 48, "TerrainVertex must be 48 bytes");

struct TerrainLayerMaterialInfo {

  int layer_count{0};

  std::string texture_paths[kMaxTerrainLayers];

  std::uint32_t layer_flags[kMaxTerrainLayers]{};

  std::uint32_t texture_flags[kMaxTerrainLayers]{};
};

struct PreparedTerrainChunk : TerrainLayerMaterialInfo {
  uint32_t vertex_start{0};
  uint32_t vertex_count{0};
  uint32_t hole_index_start{0};
  uint32_t hole_index_count{0};
  float bounds_min[3]{};
  float bounds_max[3]{};
  uint32_t chunk_x{0};
  uint32_t chunk_y{0};
  bool valid{false};
};

struct PreparedTerrainTile {
  std::vector<TerrainVertex> vertices;

  std::vector<uint16_t> hole_indices;
  std::vector<uint8_t> alpha_atlas_rgba;
  std::array<PreparedTerrainChunk, data::terrain::kTotalChunks> chunks{};
  bool has_alpha_layers{false};
};

PreparedTerrainTile PrepareAdtTerrainTile(const data::terrain::AdtFile &adt, uint32_t tile_x,
                                          uint32_t tile_y, bool big_alpha);

}
