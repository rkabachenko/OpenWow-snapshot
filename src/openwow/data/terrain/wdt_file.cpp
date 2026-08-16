
#include "openwow/data/terrain/wdt_file.h"

#include "openwow/data/model/binary_reader.h"
#include "openwow/data/wow_chunk_fourcc.h"

#include <cstring>

namespace openwow::data::terrain {

using openwow::data::model::BinaryReader;

namespace {

struct ChunkHdr {
  uint32_t magic;
  uint32_t size;
};

bool ReadChunkHdr(const BinaryReader& r, size_t& offset, ChunkHdr& out) {
  auto magic = r.ReadU32(offset);
  auto size = r.ReadU32(offset + 4);
  if (!magic || !size) return false;
  out.magic = *magic;
  out.size = *size;
  offset += 8;
  return true;
}

std::vector<std::string> SplitStringBlob(const uint8_t* blob, size_t blob_size) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < blob_size) {
    const auto* start = reinterpret_cast<const char*>(blob + i);
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

constexpr uint32_t kMVER = openwow::data::WowChunkFourCC("MVER");
constexpr uint32_t kMPHD = openwow::data::WowChunkFourCC("MPHD");
constexpr uint32_t kMAIN = openwow::data::WowChunkFourCC("MAIN");
constexpr uint32_t kMWMO = openwow::data::WowChunkFourCC("MWMO");
constexpr uint32_t kMODF = openwow::data::WowChunkFourCC("MODF");

}

WdtLoadResult LoadWdt(const uint8_t* data, size_t size) {
  WdtLoadResult result;

  if (data == nullptr || size < 8) {
    result.error = "WDT data is null or too small";
    return result;
  }

  BinaryReader r(data, size);
  size_t offset = 0;

  bool found_mver = false;
  bool found_mphd = false;
  bool found_main = false;

  while (offset + 8 <= size) {
    ChunkHdr hdr{};
    if (!ReadChunkHdr(r, offset, hdr)) break;

    const size_t data_start = offset;
    const size_t data_end = data_start + hdr.size;
    if (data_end > size) {
      result.error = "WDT chunk extends past end of file";
      return result;
    }

    if (hdr.magic == kMVER) {
      auto ver = r.ReadU32(data_start);
      if (!ver) {
        result.error = "WDT: failed to read MVER version";
        return result;
      }
      result.wdt.version = *ver;
      if (*ver != 18) {
        result.error = "WDT: unexpected version " + std::to_string(*ver)
                     + " (expected 18)";
        return result;
      }
      found_mver = true;
    }
    else if (hdr.magic == kMPHD) {

      if (hdr.size < 4) {
        result.error = "WDT: MPHD chunk too small";
        return result;
      }
      auto flags = r.ReadU32(data_start);
      if (!flags) {
        result.error = "WDT: failed to read MPHD flags";
        return result;
      }
      result.wdt.flags = *flags;
      result.wdt.has_global_wmo = (*flags & WdtFlags::kGlobalWmo) != 0;
      found_mphd = true;
    }
    else if (hdr.magic == kMAIN) {

      constexpr size_t kExpectedSize = 64 * 64 * sizeof(WdtTileEntry);
      if (hdr.size < kExpectedSize) {
        result.error = "WDT: MAIN chunk too small ("
                     + std::to_string(hdr.size) + " < "
                     + std::to_string(kExpectedSize) + ")";
        return result;
      }

      for (uint32_t y = 0; y < 64; ++y) {
        for (uint32_t x = 0; x < 64; ++x) {
          const size_t entry_offset = data_start + (y * 64 + x) * 8;
          auto f = r.ReadU32(entry_offset);
          auto a = r.ReadU32(entry_offset + 4);
          if (!f || !a) {
            result.error = "WDT: failed to read MAIN entry at ("
                         + std::to_string(x) + "," + std::to_string(y) + ")";
            return result;
          }
          result.wdt.tiles[y][x].flags = *f;
          result.wdt.tiles[y][x].async_id = *a;
        }
      }
      found_main = true;
    }
    else if (hdr.magic == kMWMO) {

      if (hdr.size > 0) {
        auto strings = SplitStringBlob(data + data_start, hdr.size);
        if (!strings.empty()) {
          result.wdt.global_wmo_path = strings[0];
        }
      }
    }
    else if (hdr.magic == kMODF) {

      if (hdr.size >= sizeof(WdtWmoPlacement)) {
        std::memcpy(&result.wdt.global_wmo_placement,
                    data + data_start,
                    sizeof(WdtWmoPlacement));
      }
    }

    offset = data_end;
  }

  if (!found_mver) {
    result.error = "WDT: missing MVER chunk";
    return result;
  }
  if (!found_mphd) {
    result.error = "WDT: missing MPHD chunk";
    return result;
  }
  if (!found_main) {
    result.error = "WDT: missing MAIN chunk";
    return result;
  }

  result.ok = true;
  return result;
}

WdtLoadResult LoadWdt(const std::vector<uint8_t>& data) {
  return LoadWdt(data.data(), data.size());
}

}
