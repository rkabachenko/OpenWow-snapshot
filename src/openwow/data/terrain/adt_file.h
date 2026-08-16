#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::data::terrain {

constexpr float kMapHalfSize = 17066.666f;

constexpr float kChunkSize = 33.333332f;

constexpr float kUnitSize = kChunkSize / 8.0f;

constexpr int kChunksPerSide = 16;
constexpr int kTotalChunks = kChunksPerSide * kChunksPerSide;

constexpr int kOuterGrid = 9;
constexpr int kInnerGrid = 8;
constexpr int kVerticesPerChunk = kOuterGrid * kOuterGrid + kInnerGrid * kInnerGrid;

struct ChunkHeader {
  uint32_t magic;
  uint32_t size;
};
static_assert(sizeof(ChunkHeader) == 8, "ChunkHeader must be 8 bytes.");

struct AdtHeader {
  uint32_t mcin_offset;
  uint32_t mtex_offset;
  uint32_t mmdx_offset;
  uint32_t mmid_offset;
  uint32_t mwmo_offset;
  uint32_t mwid_offset;
  uint32_t mddf_offset;
  uint32_t modf_offset;
  uint32_t mfbo_offset;
  uint32_t mh2o_offset;
  uint32_t mtxf_offset;
  uint32_t pad[4];
};
static_assert(sizeof(AdtHeader) == 60, "AdtHeader must be 60 bytes.");

struct McnkIndex {
  uint32_t offset;
  uint32_t size;
  uint32_t flags;
  uint32_t async_id;
};
static_assert(sizeof(McnkIndex) == 16, "McnkIndex must be 16 bytes.");

struct McnkHeader {
  uint32_t flags;
  uint32_t index_x;
  uint32_t index_y;
  uint32_t num_layers;
  uint32_t num_doodad_refs;
  uint32_t mcvt_offset;
  uint32_t mcnr_offset;
  uint32_t mcly_offset;
  uint32_t mcrf_offset;
  uint32_t mcal_offset;
  uint32_t mcal_size;
  uint32_t mcsh_offset;
  uint32_t mcsh_size;
  uint32_t area_id;
  uint32_t num_wmo_refs;
  uint16_t holes;
  uint16_t pad_holes;
  uint8_t low_quality_texmap[16];
  uint32_t predtex;
  uint32_t num_effects_doodad;
  uint32_t mcse_offset;
  uint32_t num_sound_emitters;
  uint32_t mclq_offset;
  uint32_t mclq_size;
  float position_x;
  float position_y;
  float position_z;
  uint32_t mccv_offset;
  uint32_t mclv_offset;
  uint32_t unused;
};
static_assert(sizeof(McnkHeader) == 128, "McnkHeader must be 128 bytes.");

struct TextureLayer {
  uint32_t texture_id;
  uint32_t flags;
  uint32_t alpha_map_offset;
  uint16_t effect_id;
  uint16_t padding;
};
static_assert(sizeof(TextureLayer) == 16, "TextureLayer must be 16 bytes.");

struct DoodadPlacement {
  uint32_t name_id;
  uint32_t unique_id;
  float position[3];
  float rotation[3];
  uint16_t scale;
  uint16_t flags;
};
static_assert(sizeof(DoodadPlacement) == 36, "DoodadPlacement must be 36 bytes.");

struct WmoPlacement {
  uint32_t name_id;
  uint32_t unique_id;
  float position[3];
  float rotation[3];
  float bounds_lower[3];
  float bounds_upper[3];
  uint16_t flags;
  uint16_t doodad_set;
  uint16_t name_set;
  uint16_t scale;
};
static_assert(sizeof(WmoPlacement) == 64, "WmoPlacement must be 64 bytes.");

struct SoundEmitterEntry {
  uint32_t sound_entry_id;
  float position[3];
  float direction[3];
};
static_assert(sizeof(SoundEmitterEntry) == 28, "SoundEmitterEntry must be 28 bytes.");

struct PackedNormal {
  int8_t x;
  int8_t z;
  int8_t y;
};
static_assert(sizeof(PackedNormal) == 3, "PackedNormal must be 3 bytes.");

struct VertexColor {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
};
static_assert(sizeof(VertexColor) == 4, "VertexColor must be 4 bytes.");

struct McnkSubchunkBinding {
  uint32_t payload_offset = 0;
  uint32_t payload_size = 0;
  bool present = false;
};

struct McnkSubchunkLayout {
  McnkSubchunkBinding mcvt{};
  McnkSubchunkBinding mccv{};
  McnkSubchunkBinding mcnr{};
  McnkSubchunkBinding mcrf{};
  McnkSubchunkBinding mcly{};
  McnkSubchunkBinding mcal{};
  McnkSubchunkBinding mcsh{};
  McnkSubchunkBinding mcse{};
  McnkSubchunkBinding mclq{};
  McnkSubchunkBinding mclv{};
};

enum class AdtWaterLayerObjectKind : std::uint16_t {
  HeightAndBytePayload = 0,
  HeightAndWordPayload = 1,
  BytePayloadOnly = 2,
};

struct AdtWaterLayer {
  uint16_t liquid_type{0};
  uint16_t liquid_object{0};
  float min_height{0.0f};
  float max_height{0.0f};
  uint8_t x_offset{0};
  uint8_t y_offset{0};
  uint8_t width{0};
  uint8_t height{0};
  std::vector<bool> exists_bitmap;
  std::vector<float> heights;
  std::vector<std::uint8_t> aux_byte_payload;
  std::vector<std::uint32_t> aux_word_payload;
};

[[nodiscard]] inline float AdtWaterLayer_GetMh2oHeight(const AdtWaterLayer &layer,
                                                       const std::size_t vertex_index) noexcept {
  if (layer.liquid_object >
      static_cast<std::uint16_t>(AdtWaterLayerObjectKind::HeightAndWordPayload)) {
    return 0.0f;
  }

  if (vertex_index >= layer.heights.size()) {
    return 0.0f;
  }

  return layer.heights[vertex_index];
}

[[nodiscard]] inline std::uint8_t AdtWaterLayer_GetMh2oAuxByte(const AdtWaterLayer &layer,
                                                               const std::size_t index) noexcept {
  switch (layer.liquid_object) {
  case static_cast<std::uint16_t>(AdtWaterLayerObjectKind::HeightAndBytePayload):
    return index < layer.aux_byte_payload.size() ? layer.aux_byte_payload[index] : 0u;
  case static_cast<std::uint16_t>(AdtWaterLayerObjectKind::BytePayloadOnly):
    if (layer.aux_byte_payload.empty() || index >= layer.aux_byte_payload.size()) {
      return 0xFFu;
    }
    return layer.aux_byte_payload[index];
  default:
    return 0u;
  }
}

struct AdtWaterChunk {
  std::vector<AdtWaterLayer> layers;
};

struct LegacyMclqLayer {
  std::uint8_t category{0};
  float min_height{0.0f};
  float max_height{0.0f};
  std::array<float, 81> heights{};
  std::array<std::array<std::uint8_t, 4>, 81> auxiliary_data{};
  std::array<std::uint8_t, 64> cell_flags{};
};

struct TerrainChunk {
  McnkHeader header{};
  std::array<float, kVerticesPerChunk> heights{};
  std::array<PackedNormal, kVerticesPerChunk> normals{};
  std::vector<TextureLayer> layers;
  std::vector<uint8_t> alpha_data;
  std::vector<VertexColor> vertex_colors;
  std::vector<VertexColor>
      low_res_vertex_colors;
  std::vector<uint32_t> doodad_wmo_refs;

  std::vector<SoundEmitterEntry> sound_emitters;
  std::vector<LegacyMclqLayer> legacy_liquid_layers;
  std::vector<uint8_t> shadow_map;
  McnkSubchunkLayout subchunks{};

  uint32_t holes{0};
};

[[nodiscard]] inline bool TerrainChunk_HasLegacyMclqPayload(const TerrainChunk &chunk) noexcept {
  return chunk.subchunks.mclq.present && chunk.subchunks.mclq.payload_size > 0u;
}

struct AdtFile {
  uint32_t version{0};
  AdtHeader header{};
  std::vector<std::string> textures;
  std::vector<std::string> models;
  std::vector<uint32_t> model_offsets;
  std::vector<std::string> wmos;
  std::vector<uint32_t> wmo_offsets;
  std::array<float, 162> flight_bounds{};
  std::vector<DoodadPlacement> doodads;
  std::vector<WmoPlacement> wmo_placements;

  std::vector<uint32_t> referenced_doodad_indices;
  std::vector<uint32_t> referenced_wmo_placement_indices;
  std::vector<uint32_t> texture_flags;
  std::array<TerrainChunk, kTotalChunks> chunks;
  std::array<AdtWaterChunk, kTotalChunks> water_chunks;
};

struct AdtLoadResult {
  bool ok{false};
  std::string error;
  AdtFile adt;
};

AdtLoadResult LoadAdt(const uint8_t *data, size_t size);

AdtLoadResult LoadAdt(const std::vector<uint8_t> &data);

namespace AlphaMapFlags {
inline constexpr uint32_t kHasAlpha = 0x100;
inline constexpr uint32_t kCompressed = 0x200;
}

[[nodiscard]] std::vector<uint8_t> DecompressAlphaMap(const uint8_t *raw_alpha, size_t raw_size,
                                                      uint32_t layer_flags, bool big_alpha,
                                                      bool fix_last_row_and_column);

namespace McnkFlags {

inline constexpr uint32_t kDoNotFixAlphaMap = 0x8000u;
}

[[nodiscard]] bool DecompressAlphaMapInto(const uint8_t *raw_alpha, size_t raw_size,
                                          uint32_t layer_flags, bool big_alpha,
                                          bool fix_last_row_and_column, uint8_t *output,
                                          size_t output_size, size_t pixel_stride = 1u,
                                          size_t row_stride = 64u);

[[nodiscard]] std::vector<float> AlphaMapToFloat(const std::vector<uint8_t> &alpha);

struct NormalFloat {
  float x, y, z;
};
[[nodiscard]] NormalFloat UnpackNormal(const PackedNormal &n);

struct PlacementTransform {
  float x, y, z;
  float rotX, rotY, rotZ;
  float scale;
};
[[nodiscard]] PlacementTransform DoodadToWorldTransform(const DoodadPlacement &d);
[[nodiscard]] PlacementTransform WmoToWorldTransform(const WmoPlacement &w);

}
