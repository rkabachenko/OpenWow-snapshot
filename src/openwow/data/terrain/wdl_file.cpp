
#include "openwow/data/terrain/wdl_file.h"
#include "openwow/data/wow_chunk_fourcc.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <cstring>

namespace openwow::data::terrain {

namespace {

constexpr std::size_t kWdlTileCount =
    static_cast<std::size_t>(kWdlTilesPerSide) * kWdlTilesPerSide;

void ResetTilePointers(WdlFile& wdl) {
  for (int y = 0; y < kWdlTilesPerSide; ++y) {
    for (int x = 0; x < kWdlTilesPerSide; ++x) {
      wdl.tile_heights[y][x] = nullptr;
      wdl.tile_holes[y][x] = nullptr;
    }
  }
}

void MoveTilePointers(WdlFile& dst, WdlFile& src) {
  for (int y = 0; y < kWdlTilesPerSide; ++y) {
    for (int x = 0; x < kWdlTilesPerSide; ++x) {
      dst.tile_heights[y][x] = src.tile_heights[y][x];
      dst.tile_holes[y][x] = src.tile_holes[y][x];
      src.tile_heights[y][x] = nullptr;
      src.tile_holes[y][x] = nullptr;
    }
  }
}

std::vector<std::string> SplitStringBlob(const uint8_t* blob, const size_t blob_size) {
  std::vector<std::string> out;
  size_t offset = 0;
  while (offset < blob_size) {
    const char* start = reinterpret_cast<const char*>(blob + offset);
    size_t length = 0;
    while (offset + length < blob_size && start[length] != '\0') {
      ++length;
    }
    if (length != 0) {
      out.emplace_back(start, length);
    }
    offset += length + 1;
  }
  return out;
}

std::vector<std::string> ResolveIndexedStringTable(
    const std::vector<uint8_t>& blob, const std::vector<uint32_t>& offsets) {
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

    const char* start = reinterpret_cast<const char*>(blob.data() + offset);
    size_t length = 0;
    while (offset + length < blob.size() && start[length] != '\0') {
      ++length;
    }
    out.emplace_back(start, length);
  }
  return out;
}

}

WdlFile::WdlFile(WdlFile&& other) noexcept
    : version(other.version),
      wmos(std::move(other.wmos)),
      wmo_placements(std::move(other.wmo_placements)) {
  ResetTilePointers(*this);
  MoveTilePointers(*this, other);
  other.version = 0;
}

WdlFile& WdlFile::operator=(WdlFile&& other) noexcept {
  if (this == &other) {
    return *this;
  }

  for (int y = 0; y < kWdlTilesPerSide; ++y) {
    for (int x = 0; x < kWdlTilesPerSide; ++x) {
      delete tile_heights[y][x];
      delete tile_holes[y][x];
    }
  }

  version = other.version;
  wmos = std::move(other.wmos);
  wmo_placements = std::move(other.wmo_placements);
  MoveTilePointers(*this, other);
  other.version = 0;
  return *this;
}

WdlFile::~WdlFile() {
  for (int y = 0; y < kWdlTilesPerSide; ++y) {
    for (int x = 0; x < kWdlTilesPerSide; ++x) {
      delete tile_heights[y][x];
      tile_heights[y][x] = nullptr;
      delete tile_holes[y][x];
      tile_holes[y][x] = nullptr;
    }
  }
}

static bool ChunkIs(const ChunkHeader& h, const char (&tag)[5]) {
  return h.magic == openwow::data::WowChunkFourCC(tag);
}

WdlLoadResult LoadWdl(const uint8_t* data, size_t size) {
  WdlLoadResult result;

  if (!data || size < sizeof(ChunkHeader) + 4) {
    result.error = "WDL: data too small";
    return result;
  }

  size_t offset = 0;

  auto ReadChunk = [&](ChunkHeader& out) -> bool {
    if (offset + sizeof(ChunkHeader) > size) return false;
    std::memcpy(&out, data + offset, sizeof(ChunkHeader));
    return true;
  };

  {
    ChunkHeader hdr;
    if (!ReadChunk(hdr) || !ChunkIs(hdr, "MVER")) {
      result.error = "WDL: missing MVER chunk";
      return result;
    }
    offset += sizeof(ChunkHeader);
    if (offset + 4 > size) {
      result.error = "WDL: truncated MVER";
      return result;
    }
    std::memcpy(&result.wdl.version, data + offset, 4);
    offset += hdr.size;
  }

  std::array<std::uint32_t, kWdlTileCount> mare_offsets{};
  bool has_maof = false;
  std::vector<uint8_t> mwmo_blob;
  std::vector<uint32_t> mwid_offsets;

  while (offset + sizeof(ChunkHeader) <= size) {
    ChunkHeader hdr;
    std::memcpy(&hdr, data + offset, sizeof(ChunkHeader));
    offset += sizeof(ChunkHeader);

    const size_t chunk_end = offset + hdr.size;
    if (chunk_end > size) break;

    if (ChunkIs(hdr, "MAOF")) {
      if (hdr.size >= sizeof(mare_offsets)) {
        std::memcpy(mare_offsets.data(), data + offset, sizeof(mare_offsets));
        has_maof = true;
      }
    } else if (ChunkIs(hdr, "MWMO")) {
      mwmo_blob.assign(data + offset, data + offset + hdr.size);
    } else if (ChunkIs(hdr, "MWID")) {
      const size_t count = hdr.size / sizeof(uint32_t);
      mwid_offsets.resize(count);
      std::memcpy(mwid_offsets.data(), data + offset, count * sizeof(uint32_t));
    } else if (ChunkIs(hdr, "MODF")) {
      const size_t count = hdr.size / sizeof(WmoPlacement);
      result.wdl.wmo_placements.resize(count);
      std::memcpy(result.wdl.wmo_placements.data(), data + offset,
                  count * sizeof(WmoPlacement));
    }

    offset = chunk_end;
  }

  if (has_maof) {
    for (std::size_t tile_index = 0; tile_index < mare_offsets.size();
         ++tile_index) {
      const std::size_t mare_offset = mare_offsets[tile_index];
      if (mare_offset == 0 || mare_offset > size - sizeof(ChunkHeader)) {
        continue;
      }

      ChunkHeader mare_header{};
      std::memcpy(&mare_header, data + mare_offset, sizeof(mare_header));
      const std::size_t mare_data_offset = mare_offset + sizeof(ChunkHeader);
      if (!ChunkIs(mare_header, "MARE") ||
          mare_header.size < static_cast<std::uint32_t>(kWdlMareSize) ||
          mare_header.size > size - mare_data_offset) {
        continue;
      }

      const std::size_t tile_y = tile_index / kWdlTilesPerSide;
      const std::size_t tile_x = tile_index % kWdlTilesPerSide;
      auto* tile = new WdlTileHeights{};
      const std::uint8_t* heights = data + mare_data_offset;

      for (auto& row : tile->outer) {
        std::memcpy(row.data(), heights, sizeof(row));
        heights += sizeof(row);
      }
      for (auto& row : tile->inner) {
        std::memcpy(row.data(), heights, sizeof(row));
        heights += sizeof(row);
      }
      result.wdl.tile_heights[tile_y][tile_x] = tile;

      const std::size_t maho_offset = mare_data_offset + mare_header.size;
      if (maho_offset > size - sizeof(ChunkHeader)) {
        continue;
      }

      ChunkHeader maho_header{};
      std::memcpy(&maho_header, data + maho_offset, sizeof(maho_header));
      const std::size_t maho_data_offset = maho_offset + sizeof(ChunkHeader);
      constexpr std::size_t kMahoBytes = 16 * sizeof(std::uint16_t);
      if (!ChunkIs(maho_header, "MAHO") || maho_header.size < kMahoBytes ||
          maho_header.size > size - maho_data_offset) {
        continue;
      }

      auto* holes = new WdlTileHoles{};
      std::memcpy(holes->rows.data(), data + maho_data_offset, kMahoBytes);
      result.wdl.tile_holes[tile_y][tile_x] = holes;
    }
  }

  result.wdl.wmos = ResolveIndexedStringTable(mwmo_blob, mwid_offsets);

  result.ok = true;
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "WDL: loaded version " +
                         std::to_string(result.wdl.version) + ", " +
                         std::to_string(result.wdl.CountTilesWithData()) +
                         " tiles with low-res heights");
  return result;
}

WdlLoadResult LoadWdl(const std::vector<uint8_t>& data) {
  return LoadWdl(data.data(), data.size());
}

}
