#include "openwow/data/terrain/adt_file.h"
#include "openwow/data/terrain/wmo_placement_scale.h"

#include "openwow/data/model/binary_reader.h"
#include "openwow/data/wow_chunk_fourcc.h"

#include <algorithm>
#include <bit>
#include <cstring>

namespace openwow::data::terrain {

using openwow::data::model::BinaryReader;

namespace {

bool ReadChunkHeader(const BinaryReader &r, size_t &offset, ChunkHeader &out) {
  auto magic = r.ReadU32(offset);
  auto size = r.ReadU32(offset + 4);
  if (!magic || !size)
    return false;
  out.magic = *magic;
  out.size = *size;
  offset += sizeof(ChunkHeader);
  return true;
}

std::vector<std::string> SplitStringBlob(const uint8_t *blob, size_t blob_size) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < blob_size) {
    const auto *start = reinterpret_cast<const char *>(blob + i);
    size_t len = 0;
    while (i + len < blob_size && start[len] != '\0') {
      ++len;
    }
    if (len > 0) {
      out.emplace_back(start, len);
    }
    i += len + 1;
  }
  return out;
}

std::vector<std::string> ResolveIndexedStringTable(const std::vector<uint8_t> &blob,
                                                   const std::vector<uint32_t> &offsets) {
  if (blob.empty()) {
    return {};
  }
  if (offsets.empty()) {
    return SplitStringBlob(blob.data(), blob.size());
  }

  std::vector<std::string> out;
  out.reserve(offsets.size());
  for (const uint32_t offset : offsets) {
    if (offset >= blob.size()) {
      out.emplace_back();
      continue;
    }

    const char *start = reinterpret_cast<const char *>(blob.data() + offset);
    size_t length = 0;
    while (offset + length < blob.size() && start[length] != '\0') {
      ++length;
    }
    out.emplace_back(start, length);
  }
  return out;
}

bool ParseMver(const BinaryReader &r, size_t data_offset, uint32_t data_size, AdtFile &adt,
               std::string &error) {
  if (data_size < 4) {
    error = "MVER chunk too small";
    return false;
  }
  auto v = r.ReadU32(data_offset);
  if (!v) {
    error = "MVER: read failed";
    return false;
  }
  adt.version = *v;
  if (adt.version != 18 && adt.version != 17) {
    error = "MVER: unsupported version " + std::to_string(adt.version);
    return false;
  }
  return true;
}

bool ParseMhdr(const BinaryReader &r, size_t data_offset, uint32_t data_size, AdtFile &adt,
               std::string &error) {
  if (data_size < sizeof(AdtHeader)) {
    error = "MHDR chunk too small";
    return false;
  }
  auto span = r.ReadBytes(data_offset, sizeof(AdtHeader));
  if (!span) {
    error = "MHDR: read failed";
    return false;
  }
  std::memcpy(&adt.header, span->data(), sizeof(AdtHeader));
  return true;
}

bool ParseMtex(const BinaryReader &r, size_t data_offset, uint32_t data_size, AdtFile &adt,
               std::string & ) {
  if (data_size == 0)
    return true;
  auto blob = r.ReadBytes(data_offset, data_size);
  if (!blob)
    return true;
  adt.textures = SplitStringBlob(blob->data(), blob->size());
  return true;
}

bool ParseMddf(const BinaryReader &r, size_t data_offset, uint32_t data_size, AdtFile &adt,
               std::string & ) {
  if (data_size == 0)
    return true;
  const size_t count = data_size / sizeof(DoodadPlacement);
  if (count == 0)
    return true;
  auto span = r.ReadSpan<DoodadPlacement>(data_offset, count);
  if (!span)
    return true;
  adt.doodads.assign(span->begin(), span->end());
  return true;
}

bool ParseModf(const BinaryReader &r, size_t data_offset, uint32_t data_size, AdtFile &adt,
               std::string & ) {
  if (data_size == 0)
    return true;
  const size_t count = data_size / sizeof(WmoPlacement);
  if (count == 0)
    return true;
  auto span = r.ReadSpan<WmoPlacement>(data_offset, count);
  if (!span)
    return true;
  adt.wmo_placements.assign(span->begin(), span->end());
  return true;
}

bool ParseMtxf(const BinaryReader &r, size_t data_offset, uint32_t data_size, AdtFile &adt,
               std::string & ) {
  if (data_size == 0)
    return true;
  const size_t count = data_size / sizeof(uint32_t);
  if (count == 0)
    return true;
  auto span = r.ReadSpan<uint32_t>(data_offset, count);
  if (!span)
    return true;
  adt.texture_flags.assign(span->begin(), span->end());
  return true;
}

constexpr uint32_t kMCVT = openwow::data::WowChunkFourCC("MCVT");
constexpr uint32_t kMCCV = openwow::data::WowChunkFourCC("MCCV");
constexpr uint32_t kMCNR = openwow::data::WowChunkFourCC("MCNR");
constexpr uint32_t kMCRF = openwow::data::WowChunkFourCC("MCRF");
constexpr uint32_t kMCLY = openwow::data::WowChunkFourCC("MCLY");
constexpr uint32_t kMCAL = openwow::data::WowChunkFourCC("MCAL");
constexpr uint32_t kMCSH = openwow::data::WowChunkFourCC("MCSH");
constexpr uint32_t kMCSE = openwow::data::WowChunkFourCC("MCSE");
constexpr uint32_t kMCLQ = openwow::data::WowChunkFourCC("MCLQ");
constexpr uint32_t kMCLV = openwow::data::WowChunkFourCC("MCLV");

constexpr uint32_t kRetailMcnrTraversalSize = 448u;

void BindMcnkSubchunk(McnkSubchunkBinding &binding, const size_t chunk_offset,
                      const size_t payload_offset, const uint32_t payload_size) {
  binding.payload_offset = static_cast<uint32_t>(payload_offset - chunk_offset);
  binding.payload_size = payload_size;
  binding.present = true;
}

[[nodiscard]] uint32_t NormalizeMcnkTraversalSize(const TerrainChunk &chunk,
                                                  const ChunkHeader &subchunk) {
  switch (subchunk.magic) {
  case kMCNR:
    return kRetailMcnrTraversalSize;
  case kMCAL:
    return chunk.header.mcal_size > sizeof(ChunkHeader)
               ? chunk.header.mcal_size - sizeof(ChunkHeader)
               : subchunk.size;

  case kMCSH:
    return subchunk.size;
  case kMCLQ:
    return chunk.header.mclq_size > sizeof(ChunkHeader)
               ? chunk.header.mclq_size - sizeof(ChunkHeader)
               : subchunk.size;
  default:
    return subchunk.size;
  }
}

void RecordMcnkSubchunkBinding(TerrainChunk &chunk, const ChunkHeader &subchunk,
                               const size_t chunk_offset, const size_t payload_offset,
                               const uint32_t normalized_payload_size) {
  switch (subchunk.magic) {
  case kMCVT:
    BindMcnkSubchunk(chunk.subchunks.mcvt, chunk_offset, payload_offset, normalized_payload_size);
    break;
  case kMCCV:
    BindMcnkSubchunk(chunk.subchunks.mccv, chunk_offset, payload_offset, normalized_payload_size);
    break;
  case kMCNR:
    BindMcnkSubchunk(chunk.subchunks.mcnr, chunk_offset, payload_offset, normalized_payload_size);
    break;
  case kMCRF:
    BindMcnkSubchunk(chunk.subchunks.mcrf, chunk_offset, payload_offset, normalized_payload_size);
    break;
  case kMCLY:
    BindMcnkSubchunk(chunk.subchunks.mcly, chunk_offset, payload_offset, normalized_payload_size);
    break;
  case kMCAL:
    BindMcnkSubchunk(chunk.subchunks.mcal, chunk_offset, payload_offset, normalized_payload_size);
    break;
  case kMCSH:
    BindMcnkSubchunk(chunk.subchunks.mcsh, chunk_offset, payload_offset, normalized_payload_size);
    break;
  case kMCSE:
    if (chunk.header.num_sound_emitters != 0u) {
      BindMcnkSubchunk(chunk.subchunks.mcse, chunk_offset, payload_offset, normalized_payload_size);
    }
    break;
  case kMCLQ:
    if (chunk.header.mclq_size > sizeof(ChunkHeader)) {
      BindMcnkSubchunk(chunk.subchunks.mclq, chunk_offset, payload_offset, normalized_payload_size);
    }
    break;
  case kMCLV:

    BindMcnkSubchunk(chunk.subchunks.mclv, chunk_offset, payload_offset, normalized_payload_size);
    break;
  default:
    break;
  }
}

void ParseMclv(const BinaryReader &r, size_t data_offset, uint32_t data_size, TerrainChunk &chunk) {
  constexpr size_t kExpectedBytes = kVerticesPerChunk * sizeof(VertexColor);
  if (data_size < kExpectedBytes)
    return;
  auto span = r.ReadSpan<VertexColor>(data_offset, kVerticesPerChunk);
  if (!span)
    return;
  chunk.low_res_vertex_colors.assign(span->begin(), span->end());
}

void ParseMcvt(const BinaryReader &r, size_t data_offset, uint32_t data_size, TerrainChunk &chunk) {
  constexpr size_t kExpectedBytes = kVerticesPerChunk * sizeof(float);
  if (data_size < kExpectedBytes)
    return;
  auto span = r.ReadSpan<float>(data_offset, kVerticesPerChunk);
  if (!span)
    return;
  std::copy(span->begin(), span->end(), chunk.heights.begin());
}

void ParseMcnr(const BinaryReader &r, size_t data_offset, uint32_t data_size, TerrainChunk &chunk) {
  constexpr size_t kExpectedBytes = kVerticesPerChunk * sizeof(PackedNormal);
  if (data_size < kExpectedBytes)
    return;
  auto span = r.ReadSpan<PackedNormal>(data_offset, kVerticesPerChunk);
  if (!span)
    return;
  std::copy(span->begin(), span->end(), chunk.normals.begin());
}

void ParseMcly(const BinaryReader &r, size_t data_offset, uint32_t data_size, TerrainChunk &chunk) {
  if (data_size == 0)
    return;
  const size_t count = data_size / sizeof(TextureLayer);
  if (count == 0)
    return;
  auto span = r.ReadSpan<TextureLayer>(data_offset, count);
  if (!span)
    return;
  chunk.layers.assign(span->begin(), span->end());
}

void ParseMcal(const BinaryReader &r, size_t data_offset, uint32_t data_size, TerrainChunk &chunk) {
  if (data_size == 0)
    return;
  auto bytes = r.ReadBytes(data_offset, data_size);
  if (!bytes)
    return;
  chunk.alpha_data.assign(bytes->begin(), bytes->end());
}

void ParseMccv(const BinaryReader &r, size_t data_offset, uint32_t data_size, TerrainChunk &chunk) {
  constexpr size_t kExpectedBytes = kVerticesPerChunk * sizeof(VertexColor);
  if (data_size < kExpectedBytes)
    return;
  auto span = r.ReadSpan<VertexColor>(data_offset, kVerticesPerChunk);
  if (!span)
    return;
  chunk.vertex_colors.assign(span->begin(), span->end());
}

void ParseMcrf(const BinaryReader &r, size_t data_offset, uint32_t data_size, TerrainChunk &chunk) {
  if (data_size == 0)
    return;
  const size_t count = data_size / sizeof(uint32_t);
  if (count == 0)
    return;
  auto span = r.ReadSpan<uint32_t>(data_offset, count);
  if (!span)
    return;
  chunk.doodad_wmo_refs.assign(span->begin(), span->end());
}

void ParseMcse(const BinaryReader &r, const size_t data_offset, const uint32_t data_size,
               TerrainChunk &chunk) {
  const std::size_t count = chunk.header.num_sound_emitters;
  if (count == 0u || count > data_size / sizeof(SoundEmitterEntry)) {
    return;
  }
  const auto span = r.ReadSpan<SoundEmitterEntry>(data_offset, count);
  if (!span) {
    return;
  }
  chunk.sound_emitters.assign(span->begin(), span->end());
}

[[nodiscard]] float DecodeLittleFloat(const std::span<const std::uint8_t> bytes,
                                      const std::size_t offset) noexcept {
  const std::uint32_t bits = static_cast<std::uint32_t>(bytes[offset + 0u]) |
                             (static_cast<std::uint32_t>(bytes[offset + 1u]) << 8u) |
                             (static_cast<std::uint32_t>(bytes[offset + 2u]) << 16u) |
                             (static_cast<std::uint32_t>(bytes[offset + 3u]) << 24u);
  return std::bit_cast<float>(bits);
}

void ParseMclq(const BinaryReader &r, const size_t data_offset, const uint32_t data_size,
               TerrainChunk &chunk) {

  constexpr std::size_t kRecordStride = 0x324u;
  constexpr std::size_t kVertexOffset = 8u;
  constexpr std::size_t kVertexStride = 8u;
  constexpr std::size_t kVertexCount = 81u;
  constexpr std::size_t kCellFlagsOffset = 0x290u;
  constexpr std::size_t kCellCount = 64u;
  constexpr std::size_t kMinimumRecordSize = kCellFlagsOffset + kCellCount;

  const auto payload = r.ReadBytes(data_offset, data_size);
  if (!payload) {
    return;
  }

  std::vector<LegacyMclqLayer> parsed;
  parsed.reserve(4u);
  std::size_t record_offset = 0u;
  for (std::uint8_t category = 0u; category < 4u; ++category) {
    const std::uint32_t active_flag = 4u << category;
    if ((chunk.header.flags & active_flag) == 0u) {
      continue;
    }
    if (record_offset > payload->size() || kMinimumRecordSize > payload->size() - record_offset) {
      return;
    }

    LegacyMclqLayer layer{};
    layer.category = category;
    layer.min_height = DecodeLittleFloat(*payload, record_offset + 0u);
    layer.max_height = DecodeLittleFloat(*payload, record_offset + 4u);
    for (std::size_t vertex = 0u; vertex < kVertexCount; ++vertex) {
      const std::size_t vertex_offset = record_offset + kVertexOffset + vertex * kVertexStride;
      std::copy_n(payload->begin() + static_cast<std::ptrdiff_t>(vertex_offset),
                  layer.auxiliary_data[vertex].size(), layer.auxiliary_data[vertex].begin());
      layer.heights[vertex] = DecodeLittleFloat(*payload, vertex_offset + 4u);
    }
    std::copy_n(payload->begin() + static_cast<std::ptrdiff_t>(record_offset + kCellFlagsOffset),
                kCellCount, layer.cell_flags.begin());
    parsed.push_back(std::move(layer));
    record_offset += kRecordStride;
  }
  chunk.legacy_liquid_layers = std::move(parsed);
}

void ParseMcsh(const BinaryReader &r, size_t data_offset, uint32_t data_size, TerrainChunk &chunk) {
  if (data_size == 0)
    return;
  auto bytes = r.ReadBytes(data_offset, data_size);
  if (!bytes)
    return;
  chunk.shadow_map.assign(bytes->begin(), bytes->end());
}

#pragma pack(push, 1)
struct SMLiquidChunk {
  uint32_t offset_instances;
  uint32_t layer_count;
  uint32_t offset_attributes;
};
static_assert(sizeof(SMLiquidChunk) == 12, "SMLiquidChunk must be 12 bytes.");

struct SMLiquidInstance {
  uint16_t liquid_type;
  uint16_t liquid_object;
  float min_height_level;
  float max_height_level;
  uint8_t x_offset;
  uint8_t y_offset;
  uint8_t width;
  uint8_t height;
  uint32_t offset_exists_bitmap;
  uint32_t offset_vertex_data;
};
static_assert(sizeof(SMLiquidInstance) == 24, "SMLiquidInstance must be 24 bytes.");
#pragma pack(pop)

bool ParseMh2o(const BinaryReader &r, size_t data_offset, uint32_t data_size, AdtFile &adt,
               std::string & ) {
  if (data_size < kTotalChunks * sizeof(SMLiquidChunk))
    return true;

  for (int i = 0; i < kTotalChunks; ++i) {
    const size_t chunk_hdr_offset = data_offset + static_cast<size_t>(i) * sizeof(SMLiquidChunk);
    auto hdr_span = r.ReadBytes(chunk_hdr_offset, sizeof(SMLiquidChunk));
    if (!hdr_span)
      continue;

    SMLiquidChunk lc{};
    std::memcpy(&lc, hdr_span->data(), sizeof(SMLiquidChunk));

    if (lc.layer_count == 0 || lc.offset_instances == 0)
      continue;

    auto &water_chunk = adt.water_chunks[i];

    for (uint32_t layer = 0; layer < lc.layer_count; ++layer) {
      const size_t inst_offset =
          data_offset + lc.offset_instances + static_cast<size_t>(layer) * sizeof(SMLiquidInstance);
      auto inst_span = r.ReadBytes(inst_offset, sizeof(SMLiquidInstance));
      if (!inst_span)
        continue;

      SMLiquidInstance inst{};
      std::memcpy(&inst, inst_span->data(), sizeof(SMLiquidInstance));

      AdtWaterLayer wl;
      wl.liquid_type = inst.liquid_type;
      wl.liquid_object = inst.liquid_object;
      wl.min_height = inst.min_height_level;
      wl.max_height = inst.max_height_level;
      wl.x_offset = inst.x_offset;
      wl.y_offset = inst.y_offset;
      wl.width = inst.width;
      wl.height = inst.height;

      const uint32_t bitmap_bits =
          static_cast<uint32_t>(wl.width) * static_cast<uint32_t>(wl.height);
      if (inst.offset_exists_bitmap != 0 && bitmap_bits > 0) {
        const uint32_t bitmap_bytes = (bitmap_bits + 7) / 8;
        const size_t bm_offset = data_offset + inst.offset_exists_bitmap;
        auto bm_data = r.ReadBytes(bm_offset, bitmap_bytes);
        if (bm_data) {
          wl.exists_bitmap.resize(bitmap_bits, false);
          for (uint32_t b = 0; b < bitmap_bits; ++b) {
            wl.exists_bitmap[b] = ((*bm_data)[b / 8] & (1u << (b % 8))) != 0;
          }
        }
      } else if (bitmap_bits > 0) {

        wl.exists_bitmap.assign(bitmap_bits, true);
      }

      if (inst.offset_vertex_data != 0 && wl.width > 0 && wl.height > 0) {
        const uint32_t vert_count =
            (static_cast<uint32_t>(wl.width) + 1) * (static_cast<uint32_t>(wl.height) + 1);
        const uint32_t tile_count =
            static_cast<uint32_t>(wl.width) * static_cast<uint32_t>(wl.height);
        const size_t vd_offset = data_offset + inst.offset_vertex_data;
        switch (inst.liquid_object) {
        case static_cast<std::uint16_t>(AdtWaterLayerObjectKind::HeightAndBytePayload): {
          auto height_span = r.ReadSpan<float>(vd_offset, vert_count);
          if (height_span) {
            wl.heights.assign(height_span->begin(), height_span->end());
          }

          auto byte_span =
              r.ReadBytes(vd_offset + static_cast<size_t>(vert_count) * sizeof(float), tile_count);
          if (byte_span) {
            wl.aux_byte_payload.assign(byte_span->begin(), byte_span->end());
          }
          break;
        }
        case static_cast<std::uint16_t>(AdtWaterLayerObjectKind::HeightAndWordPayload): {
          auto height_span = r.ReadSpan<float>(vd_offset, vert_count);
          if (height_span) {
            wl.heights.assign(height_span->begin(), height_span->end());
          }

          auto word_span = r.ReadSpan<std::uint32_t>(
              vd_offset + static_cast<size_t>(vert_count) * sizeof(float), tile_count);
          if (word_span) {
            wl.aux_word_payload.assign(word_span->begin(), word_span->end());
          }
          break;
        }
        case static_cast<std::uint16_t>(AdtWaterLayerObjectKind::BytePayloadOnly): {
          auto byte_span = r.ReadBytes(vd_offset, tile_count);
          if (byte_span) {
            wl.aux_byte_payload.assign(byte_span->begin(), byte_span->end());
          }
          break;
        }
        default:
          break;
        }
      }

      water_chunk.layers.push_back(std::move(wl));
    }
  }
  return true;
}

bool ParseMcnk(const BinaryReader &r, size_t chunk_offset, uint32_t chunk_size, TerrainChunk &chunk,
               std::string &error) {

  if (chunk_size < sizeof(McnkHeader)) {
    error = "MCNK chunk too small for header";
    return false;
  }
  auto hdr_span = r.ReadBytes(chunk_offset, sizeof(McnkHeader));
  if (!hdr_span) {
    error = "MCNK header read failed";
    return false;
  }
  std::memcpy(&chunk.header, hdr_span->data(), sizeof(McnkHeader));
  chunk.holes = chunk.header.holes;

  const size_t mcnk_end = chunk_offset + chunk_size;
  size_t cursor = chunk_offset + sizeof(McnkHeader);

  while (cursor + sizeof(ChunkHeader) <= mcnk_end) {
    ChunkHeader sub{};
    if (!ReadChunkHeader(r, cursor, sub))
      break;

    const size_t sub_data = cursor;
    const uint32_t traversal_size = NormalizeMcnkTraversalSize(chunk, sub);
    const size_t traversal_end = sub_data + traversal_size;
    if (traversal_end > mcnk_end)
      break;

    RecordMcnkSubchunkBinding(chunk, sub, chunk_offset, sub_data, traversal_size);

    if (sub.magic == kMCVT) {
      ParseMcvt(r, sub_data, sub.size, chunk);
    } else if (sub.magic == kMCNR) {
      ParseMcnr(r, sub_data, sub.size, chunk);
    } else if (sub.magic == kMCLY) {
      ParseMcly(r, sub_data, sub.size, chunk);
    } else if (sub.magic == kMCAL) {
      ParseMcal(r, sub_data, traversal_size, chunk);
    } else if (sub.magic == kMCCV) {
      ParseMccv(r, sub_data, sub.size, chunk);
    } else if (sub.magic == kMCRF) {
      ParseMcrf(r, sub_data, sub.size, chunk);
    } else if (sub.magic == kMCSE) {
      ParseMcse(r, sub_data, sub.size, chunk);
    } else if (sub.magic == kMCLQ) {
      ParseMclq(r, sub_data, traversal_size, chunk);
    } else if (sub.magic == kMCSH) {
      ParseMcsh(r, sub_data, traversal_size, chunk);
    } else if (sub.magic == kMCLV) {
      ParseMclv(r, sub_data, sub.size, chunk);
    }

    cursor = traversal_end;
  }

  return true;
}

void PreparePlacementReferenceCatalogs(AdtFile &adt) {
  adt.referenced_doodad_indices.clear();
  adt.referenced_wmo_placement_indices.clear();
  adt.referenced_doodad_indices.reserve(adt.doodads.size());
  adt.referenced_wmo_placement_indices.reserve(adt.wmo_placements.size());

  std::vector<std::uint8_t> seen_doodads(adt.doodads.size(), 0u);
  std::vector<std::uint8_t> seen_wmos(adt.wmo_placements.size(), 0u);
  for (const auto &chunk : adt.chunks) {
    const std::size_t doodad_count =
        std::min<std::size_t>(chunk.header.num_doodad_refs, chunk.doodad_wmo_refs.size());
    for (std::size_t reference = 0; reference < doodad_count; ++reference) {
      const std::uint32_t placement = chunk.doodad_wmo_refs[reference];
      if (placement >= seen_doodads.size() || seen_doodads[placement]) {
        continue;
      }
      seen_doodads[placement] = 1u;
      adt.referenced_doodad_indices.push_back(placement);
    }

    const std::size_t remaining = chunk.doodad_wmo_refs.size() - doodad_count;
    const std::size_t wmo_count = std::min<std::size_t>(chunk.header.num_wmo_refs, remaining);
    for (std::size_t reference = 0; reference < wmo_count; ++reference) {
      const std::uint32_t placement = chunk.doodad_wmo_refs[doodad_count + reference];
      if (placement >= seen_wmos.size() || seen_wmos[placement]) {
        continue;
      }
      seen_wmos[placement] = 1u;
      adt.referenced_wmo_placement_indices.push_back(placement);
    }
  }
}

}

AdtLoadResult LoadAdt(const uint8_t *data, size_t size) {
  AdtLoadResult result;
  if (data == nullptr || size < sizeof(ChunkHeader)) {
    result.error = "ADT: null or empty data";
    return result;
  }

  BinaryReader r(data, size);

  int mcnk_index = 0;
  bool got_mver = false;
  bool got_mhdr = false;
  std::vector<uint8_t> mwmo_blob;
  std::vector<uint32_t> mwid_offsets;
  std::vector<uint8_t> mmdx_blob;
  std::vector<uint32_t> mmid_offsets;

  size_t cursor = 0;
  while (cursor + sizeof(ChunkHeader) <= size) {
    ChunkHeader hdr{};
    if (!ReadChunkHeader(r, cursor, hdr))
      break;

    const size_t data_offset = cursor;
    const size_t chunk_end = cursor + hdr.size;
    if (chunk_end > size) {
      result.error = "ADT: chunk extends past end of file";
      return result;
    }

    static constexpr uint32_t kMVER = openwow::data::WowChunkFourCC("MVER");
    static constexpr uint32_t kMHDR = openwow::data::WowChunkFourCC("MHDR");
    static constexpr uint32_t kMTEX = openwow::data::WowChunkFourCC("MTEX");
    static constexpr uint32_t kMMDX = openwow::data::WowChunkFourCC("MMDX");
    static constexpr uint32_t kMWMO = openwow::data::WowChunkFourCC("MWMO");
    static constexpr uint32_t kMWID = openwow::data::WowChunkFourCC("MWID");
    static constexpr uint32_t kMDDF = openwow::data::WowChunkFourCC("MDDF");
    static constexpr uint32_t kMODF = openwow::data::WowChunkFourCC("MODF");
    static constexpr uint32_t kMCNK = openwow::data::WowChunkFourCC("MCNK");
    static constexpr uint32_t kMH2O = openwow::data::WowChunkFourCC("MH2O");
    static constexpr uint32_t kMMID = openwow::data::WowChunkFourCC("MMID");
    static constexpr uint32_t kMFBO = openwow::data::WowChunkFourCC("MFBO");
    static constexpr uint32_t kMTXF = openwow::data::WowChunkFourCC("MTXF");

    if (hdr.magic == kMVER) {
      if (!ParseMver(r, data_offset, hdr.size, result.adt, result.error))
        return result;
      got_mver = true;
    } else if (hdr.magic == kMHDR) {
      if (!ParseMhdr(r, data_offset, hdr.size, result.adt, result.error))
        return result;
      got_mhdr = true;
    } else if (hdr.magic == kMTEX) {
      ParseMtex(r, data_offset, hdr.size, result.adt, result.error);
    } else if (hdr.magic == kMMDX) {
      if (hdr.size != 0) {
        auto blob = r.ReadBytes(data_offset, hdr.size);
        if (blob) {
          mmdx_blob.assign(blob->begin(), blob->end());
        }
      }
    } else if (hdr.magic == kMMID) {
      const size_t count = hdr.size / sizeof(uint32_t);
      if (count != 0) {
        auto span = r.ReadSpan<uint32_t>(data_offset, count);
        if (span) {
          mmid_offsets.assign(span->begin(), span->end());
        }
      }
    } else if (hdr.magic == kMFBO) {

      constexpr size_t kMfboFloats = 162;
      if (hdr.size >= kMfboFloats * sizeof(float)) {
        auto span = r.ReadSpan<float>(data_offset, kMfboFloats);
        if (span) {
          std::copy(span->begin(), span->end(), result.adt.flight_bounds.begin());
        }
      }
    } else if (hdr.magic == kMWMO) {
      if (hdr.size != 0) {
        auto blob = r.ReadBytes(data_offset, hdr.size);
        if (blob) {
          mwmo_blob.assign(blob->begin(), blob->end());
        }
      }
    } else if (hdr.magic == kMWID) {
      const size_t count = hdr.size / sizeof(uint32_t);
      if (count != 0) {
        auto span = r.ReadSpan<uint32_t>(data_offset, count);
        if (span) {
          mwid_offsets.assign(span->begin(), span->end());
        }
      }
    } else if (hdr.magic == kMDDF) {
      ParseMddf(r, data_offset, hdr.size, result.adt, result.error);
    } else if (hdr.magic == kMODF) {
      ParseModf(r, data_offset, hdr.size, result.adt, result.error);
    } else if (hdr.magic == kMCNK) {
      if (mcnk_index >= kTotalChunks) {
        result.error = "ADT: too many MCNK chunks (>256)";
        return result;
      }
      if (!ParseMcnk(r, data_offset, hdr.size, result.adt.chunks[mcnk_index], result.error)) {
        return result;
      }
      ++mcnk_index;
    } else if (hdr.magic == kMH2O) {
      ParseMh2o(r, data_offset, hdr.size, result.adt, result.error);
    } else if (hdr.magic == kMTXF) {
      ParseMtxf(r, data_offset, hdr.size, result.adt, result.error);
    }

    cursor = chunk_end;
  }

  if (!got_mver) {
    result.error = "ADT: missing MVER chunk";
    return result;
  }
  if (!got_mhdr) {
    result.error = "ADT: missing MHDR chunk";
    return result;
  }
  if (mcnk_index != kTotalChunks) {
    result.error = "ADT: expected 256 MCNK chunks, got " + std::to_string(mcnk_index);
    return result;
  }

  result.adt.models = ResolveIndexedStringTable(mmdx_blob, mmid_offsets);
  result.adt.model_offsets = std::move(mmid_offsets);
  result.adt.wmos = ResolveIndexedStringTable(mwmo_blob, mwid_offsets);
  result.adt.wmo_offsets = std::move(mwid_offsets);
  PreparePlacementReferenceCatalogs(result.adt);

  result.ok = true;
  return result;
}

AdtLoadResult LoadAdt(const std::vector<uint8_t> &data) {
  return LoadAdt(data.data(), data.size());
}

bool DecompressAlphaMapInto(const uint8_t *raw_alpha, const size_t raw_size,
                            const uint32_t layer_flags, const bool big_alpha,
                            const bool fix_last_row_and_column, uint8_t *output,
                            const size_t output_size, const size_t pixel_stride,
                            const size_t row_stride) {
  constexpr size_t kAlphaSide = 64u;
  constexpr size_t kAlphaSize = kAlphaSide * kAlphaSide;

  if (raw_alpha == nullptr || raw_size == 0u || output == nullptr || output_size == 0u ||
      pixel_stride == 0u || row_stride == 0u || pixel_stride > row_stride / kAlphaSide) {
    return false;
  }
  const size_t last_valid_byte = output_size - 1u;
  if (kAlphaSide - 1u > last_valid_byte / row_stride) {
    return false;
  }
  const size_t last_row = (kAlphaSide - 1u) * row_stride;
  if (kAlphaSide - 1u > (last_valid_byte - last_row) / pixel_stride) {
    return false;
  }

  size_t produced = 0u;
  size_t row = 0u;
  size_t column = 0u;
  const auto emit = [&](const uint8_t value) {
    output[row * row_stride + column * pixel_stride] = value;
    ++produced;
    if (++column == kAlphaSide) {
      column = 0u;
      ++row;
    }
  };
  const auto emit_run = [&](const uint8_t value, const size_t count) {
    const size_t emit_count = std::min(count, kAlphaSize - produced);
    for (size_t index = 0u; index < emit_count; ++index) {
      emit(value);
    }
  };

  const bool compressed = (layer_flags & AlphaMapFlags::kCompressed) != 0u;

  if (compressed) {

    size_t offset = 0u;
    while (produced < kAlphaSize && offset < raw_size) {
      const uint8_t command = raw_alpha[offset++];
      const size_t count = static_cast<size_t>(command & 0x7Fu);
      if ((command & 0x80u) != 0u) {
        if (offset >= raw_size) {
          break;
        }
        emit_run(raw_alpha[offset++], count);
      } else {
        const size_t copy_count = std::min({count, kAlphaSize - produced, raw_size - offset});
        for (size_t index = 0u; index < copy_count; ++index) {
          emit(raw_alpha[offset + index]);
        }
        offset += copy_count;
      }
    }
  } else if (big_alpha) {
    const size_t available = std::min(raw_size, kAlphaSize);
    for (size_t index = 0u; index < available; ++index) {
      emit(raw_alpha[index]);
    }
  } else {
    constexpr size_t kPackedSize = kAlphaSize / 2u;
    const size_t available = std::min(raw_size, kPackedSize);
    for (size_t index = 0u; index < available; ++index) {
      const uint8_t packed = raw_alpha[index];
      emit(static_cast<uint8_t>((packed & 0x0Fu) * 17u));
      emit(static_cast<uint8_t>(((packed >> 4u) & 0x0Fu) * 17u));
    }
  }

  emit_run(0u, kAlphaSize - produced);

  if (fix_last_row_and_column) {
    for (size_t fix_column = 0u; fix_column < kAlphaSide; ++fix_column) {
      output[(kAlphaSide - 1u) * row_stride + fix_column * pixel_stride] =
          output[(kAlphaSide - 2u) * row_stride + fix_column * pixel_stride];
    }
    for (size_t fix_row = 0u; fix_row < kAlphaSide; ++fix_row) {
      output[fix_row * row_stride + (kAlphaSide - 1u) * pixel_stride] =
          output[fix_row * row_stride + (kAlphaSide - 2u) * pixel_stride];
    }
  }
  return true;
}

std::vector<uint8_t> DecompressAlphaMap(const uint8_t *raw_alpha, const size_t raw_size,
                                        const uint32_t layer_flags, const bool big_alpha,
                                        const bool fix_last_row_and_column) {
  constexpr size_t kAlphaSide = 64u;
  constexpr size_t kAlphaSize = kAlphaSide * kAlphaSide;
  if (raw_alpha == nullptr || raw_size == 0u) {
    return {};
  }
  std::vector<uint8_t> result(kAlphaSize);
  if (!DecompressAlphaMapInto(raw_alpha, raw_size, layer_flags, big_alpha,
                              fix_last_row_and_column, result.data(),
                              result.size(), 1u, kAlphaSide)) {
    result.clear();
  }
  return result;
}

std::vector<float> AlphaMapToFloat(const std::vector<uint8_t> &alpha) {
  std::vector<float> result;
  result.reserve(alpha.size());
  for (uint8_t v : alpha) {
    result.push_back(static_cast<float>(v) / 255.0f);
  }
  return result;
}

NormalFloat UnpackNormal(const PackedNormal &n) {
  return {
      static_cast<float>(n.x) / 127.0f,
      static_cast<float>(n.y) / 127.0f,
      static_cast<float>(n.z) / 127.0f,
  };
}

PlacementTransform DoodadToWorldTransform(const DoodadPlacement &d) {
  return {
      d.position[0],
      d.position[1],
      d.position[2],
      d.rotation[0],
      d.rotation[1],
      d.rotation[2],
      static_cast<float>(d.scale) / 1024.0f,
  };
}

PlacementTransform WmoToWorldTransform(const WmoPlacement &w) {
  return {
      w.position[0],
      w.position[1],
      w.position[2],
      w.rotation[0],
      w.rotation[1],
      w.rotation[2],
      DecodeWmoPlacementScale(w.scale),
  };
}

}
