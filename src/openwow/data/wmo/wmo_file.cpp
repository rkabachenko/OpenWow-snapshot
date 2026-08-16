#include "openwow/data/wmo/wmo_file.h"

#include "openwow/data/model/binary_reader.h"
#include "openwow/data/wow_chunk_fourcc.h"
#include <cmath>
#include <cstring>
#include <limits>

namespace openwow::data::wmo {

using openwow::data::model::BinaryReader;

namespace {

bool ReadChunkHeader(const BinaryReader& r, size_t& offset, ChunkHeader& out) {
  if (offset > std::numeric_limits<size_t>::max() - sizeof(ChunkHeader)) {
    return false;
  }
  auto magic = r.ReadU32(offset);
  auto size = r.ReadU32(offset + 4);
  if (!magic || !size) return false;
  out.magic = *magic;
  out.size = *size;
  offset += sizeof(ChunkHeader);
  return true;
}

bool CheckedChunkEnd(const size_t data_offset, const uint32_t data_size,
                     const size_t limit, size_t& chunk_end) {
  if (data_size > limit || data_offset > limit - data_size) {
    return false;
  }
  chunk_end = data_offset + data_size;
  return true;
}

template <typename Record>
bool ParseRootRecords(const BinaryReader& reader, const size_t data_offset,
                      const uint32_t data_size, const char* chunk_name,
                      std::vector<Record>& target, std::string& error) {
  const std::size_t record_count = data_size / sizeof(Record);
  if (record_count == 0u) {
    target.clear();
    return true;
  }
  const std::size_t record_bytes = record_count * sizeof(Record);
  const auto records = reader.ReadBytes(data_offset, record_bytes);
  if (!records) {
    error = std::string("WMO root: failed to read ") + chunk_name;
    return false;
  }
  target.resize(record_count);
  std::memcpy(target.data(), records->data(), record_bytes);
  return true;
}

bool ParseRootBytes(const BinaryReader& reader, const size_t data_offset,
                    const uint32_t data_size, const char* chunk_name,
                    std::vector<uint8_t>& target, std::string& error) {
  if (data_size == 0u) {
    target.clear();
    return true;
  }
  const auto bytes = reader.ReadBytes(data_offset, data_size);
  if (!bytes) {
    error = std::string("WMO root: failed to read ") + chunk_name;
    return false;
  }
  target.assign(bytes->begin(), bytes->end());
  return true;
}

bool ParseMver(const BinaryReader& r, size_t data_offset, uint32_t data_size,
               WmoRoot& root, std::string& error) {
  if (data_size < 4) {
    error = "MVER chunk too small";
    return false;
  }
  auto v = r.ReadU32(data_offset);
  if (!v) { error = "MVER: read failed"; return false; }
  root.version = *v;
  if (root.version != 17) {
    error = "MVER: unsupported WMO version " + std::to_string(root.version);
    return false;
  }
  return true;
}

bool ParseMohd(const BinaryReader& r, size_t data_offset, uint32_t data_size,
               WmoRoot& root, std::string& error) {
  if (data_size < sizeof(WmoHeader)) {
    error = "MOHD chunk too small (need 64 bytes, got " + std::to_string(data_size) + ")";
    return false;
  }
  auto span = r.ReadBytes(data_offset, sizeof(WmoHeader));
  if (!span) { error = "MOHD: read failed"; return false; }
  std::memcpy(&root.header, span->data(), sizeof(WmoHeader));
  return true;
}

bool ParseMotx(const BinaryReader& r, size_t data_offset, uint32_t data_size,
               WmoRoot& root, std::string& error) {
  return ParseRootBytes(r, data_offset, data_size, "MOTX",
                        root.textureNames, error);
}

bool ParseMomt(const BinaryReader& r, size_t data_offset, uint32_t data_size,
               WmoRoot& root, std::string& error) {
  return ParseRootRecords(r, data_offset, data_size, "MOMT",
                          root.materials, error);
}

bool ParseMogn(const BinaryReader& r, size_t data_offset, uint32_t data_size,
               WmoRoot& root, std::string& error) {
  return ParseRootBytes(r, data_offset, data_size, "MOGN",
                        root.groupNames, error);
}

bool ParseMogi(const BinaryReader& r, size_t data_offset, uint32_t data_size,
               WmoRoot& root, std::string& error) {
  return ParseRootRecords(r, data_offset, data_size, "MOGI",
                          root.groupInfos, error);
}

bool ParseMosb(const BinaryReader& r, size_t data_offset, uint32_t data_size,
               WmoRoot& root, std::string& error) {
  if (data_size == 0) { root.skyboxName.clear(); return true; }
  auto str = r.ReadCString(data_offset, data_size);
  if (!str) {
    error = "WMO root: failed to read MOSB";
    return false;
  }
  if (str->empty()) {
    root.skyboxName.clear();
  } else {
    root.skyboxName = std::move(*str);
  }
  return true;
}

bool ParseMopv(const BinaryReader& r, size_t data_offset, uint32_t data_size,
               WmoRoot& root, std::string& error) {
  return ParseRootRecords(r, data_offset, data_size, "MOPV",
                          root.portalVertices, error);
}

bool ParseMopt(const BinaryReader& r, size_t data_offset, uint32_t data_size,
               WmoRoot& root, std::string& error) {
  if (!ParseRootRecords(r, data_offset, data_size, "MOPT", root.portals,
                        error)) {
    return false;
  }

  for (auto& p : root.portals) {
    if (std::isnan(p.distance)) {
      p.normal[0] = 0.0f;
      p.normal[1] = 0.0f;
      p.normal[2] = 1.0f;
      p.distance = 1e10f;
    }
  }
  return true;
}

bool ParseMopr(const BinaryReader& r, size_t data_offset, uint32_t data_size,
               WmoRoot& root, std::string& error) {
  return ParseRootRecords(r, data_offset, data_size, "MOPR",
                          root.portalRefs, error);
}

bool ParseMovv(const BinaryReader& r, size_t data_offset, uint32_t data_size,
               WmoRoot& root, std::string& error) {
  return ParseRootRecords(r, data_offset, data_size, "MOVV",
                          root.visibleVertices, error);
}

bool ParseMovb(const BinaryReader& r, size_t data_offset, uint32_t data_size,
                WmoRoot& root, std::string& error) {
  return ParseRootRecords(r, data_offset, data_size, "MOVB",
                          root.visibleBlocks, error);
}

bool ParseMolt(const BinaryReader& r, size_t data_offset, uint32_t data_size,
               WmoRoot& root, std::string& error) {
  return ParseRootRecords(r, data_offset, data_size, "MOLT", root.lights,
                          error);
}

bool ParseMods(const BinaryReader& r, size_t data_offset, uint32_t data_size,
               WmoRoot& root, std::string& error) {
  return ParseRootRecords(r, data_offset, data_size, "MODS",
                          root.doodadSets, error);
}

bool ParseModn(const BinaryReader& r, size_t data_offset, uint32_t data_size,
               WmoRoot& root, std::string& error) {
  return ParseRootBytes(r, data_offset, data_size, "MODN",
                        root.doodadNames, error);
}

bool ParseModd(const BinaryReader& r, size_t data_offset, uint32_t data_size,
               WmoRoot& root, std::string& error) {
  return ParseRootRecords(r, data_offset, data_size, "MODD",
                          root.doodadDefs, error);
}

bool ParseMfog(const BinaryReader& r, size_t data_offset, uint32_t data_size,
               WmoRoot& root, std::string& error) {
  return ParseRootRecords(r, data_offset, data_size, "MFOG", root.fogs,
                          error);
}

bool ParseMcvp(const BinaryReader& r, size_t data_offset, uint32_t data_size,
               WmoRoot& root, std::string& error) {
  return ParseRootRecords(r, data_offset, data_size, "MCVP",
                          root.convexVolumePlanes, error);
}

template <typename Record>
void ParseGroupRecords(const BinaryReader& reader, const size_t data_offset,
                       const uint32_t data_size,
                       std::vector<Record>& target) {
  const std::size_t record_count = data_size / sizeof(Record);
  if (record_count == 0u) {
    target.clear();
    return;
  }
  const std::size_t record_bytes = record_count * sizeof(Record);
  const auto records = reader.ReadBytes(data_offset, record_bytes);
  if (!records) {
    return;
  }
  target.resize(record_count);
  std::memcpy(target.data(), records->data(), record_bytes);
}

void ParseMopy(const BinaryReader& r, size_t data_offset, uint32_t data_size,
               WmoGroup& group) {
  ParseGroupRecords(r, data_offset, data_size, group.triangleMaterials);
}

void ParseMovi(const BinaryReader& r, size_t data_offset, uint32_t data_size,
               WmoGroup& group) {
  ParseGroupRecords(r, data_offset, data_size, group.indices);
}

void ParseMovt(const BinaryReader& r, size_t data_offset, uint32_t data_size,
               WmoGroup& group) {
  ParseGroupRecords(r, data_offset, data_size, group.vertices);
}

void ParseMonr(const BinaryReader& r, size_t data_offset, uint32_t data_size,
               WmoGroup& group) {
  ParseGroupRecords(r, data_offset, data_size, group.normals);
}

void ParseMotv(const BinaryReader& r, size_t data_offset, uint32_t data_size,
               std::vector<Vec2f>& target) {
  ParseGroupRecords(r, data_offset, data_size, target);
}

void ParseMoba(const BinaryReader& r, size_t data_offset, uint32_t data_size,
               WmoGroup& group) {
  ParseGroupRecords(r, data_offset, data_size, group.renderBatches);
}

void ParseMolr(const BinaryReader& r, size_t data_offset, uint32_t data_size,
               WmoGroup& group) {
  ParseGroupRecords(r, data_offset, data_size, group.lightRefs);
}

void ParseModr(const BinaryReader& r, size_t data_offset, uint32_t data_size,
               WmoGroup& group) {
  ParseGroupRecords(r, data_offset, data_size, group.doodadRefs);
}

void ParseMobn(const BinaryReader& r, size_t data_offset, uint32_t data_size,
               WmoGroup& group) {
  ParseGroupRecords(r, data_offset, data_size, group.bspNodes);
}

void ParseMobr(const BinaryReader& r, size_t data_offset, uint32_t data_size,
               WmoGroup& group) {
  ParseGroupRecords(r, data_offset, data_size, group.bspFaceIndices);
}

void ParseMori(const BinaryReader& r, size_t data_offset, uint32_t data_size,
               WmoGroup& group) {
  ParseGroupRecords(r, data_offset, data_size, group.alternateIndices);
}

void ParseMorb(const BinaryReader& r, size_t data_offset, uint32_t data_size,
               WmoGroup& group) {
  ParseGroupRecords(r, data_offset, data_size,
                    group.alternateRenderBatches);
}

void ParseMocv(const BinaryReader& r, size_t data_offset, uint32_t data_size,
               std::vector<WmoVertexColor>& target) {
  ParseGroupRecords(r, data_offset, data_size, target);
}

bool ParseMliq(const BinaryReader& r, size_t data_offset, uint32_t data_size,
               WmoGroup& group, std::string& error) {

  constexpr size_t kHeaderSize = 0x1e;
  if (data_size < kHeaderSize) {
    error = "WMO group: MLIQ too small for header";
    return false;
  }

  auto xVerts = r.ReadU32(data_offset + 0x00);
  auto yVerts = r.ReadU32(data_offset + 0x04);
  auto xTiles = r.ReadU32(data_offset + 0x08);
  auto yTiles = r.ReadU32(data_offset + 0x0c);
  auto baseX  = r.ReadF32(data_offset + 0x10);
  auto baseY  = r.ReadF32(data_offset + 0x14);
  auto baseZ  = r.ReadF32(data_offset + 0x18);
  auto matId  = r.ReadU16(data_offset + 0x1c);

  if (!xVerts || !yVerts || !xTiles || !yTiles || !baseX || !baseY ||
      !baseZ || !matId) {
    error = "WMO group: failed to read MLIQ header";
    return false;
  }

  const auto checked_product = [](const size_t lhs, const size_t rhs,
                                  size_t& product) {
    if (lhs != 0u && rhs > std::numeric_limits<size_t>::max() / lhs) {
      return false;
    }
    product = lhs * rhs;
    return true;
  };
  size_t vertexCount = 0u;
  size_t tileBytes = 0u;
  size_t vertexBytes = 0u;
  if (!checked_product(*xVerts, *yVerts, vertexCount) ||
      !checked_product(vertexCount, 8u, vertexBytes) ||
      !checked_product(*xTiles, *yTiles, tileBytes)) {
    error = "WMO group: MLIQ dimensions overflow";
    return false;
  }
  if (vertexBytes > data_size - kHeaderSize ||
      tileBytes > data_size - kHeaderSize - vertexBytes) {
    error = "WMO group: incomplete MLIQ vertex or tile payload";
    return false;
  }

  const size_t vertexStart = data_offset + kHeaderSize;
  const size_t tileStart = vertexStart + vertexBytes;
  const auto vblob = r.ReadBytes(vertexStart, vertexBytes);
  const auto tblob = r.ReadBytes(tileStart, tileBytes);
  if (!vblob || !tblob) {
    error = "WMO group: failed to read MLIQ payload";
    return false;
  }

  WmoLiquidData liq{};
  liq.header.xVerts     = *xVerts;
  liq.header.yVerts     = *yVerts;
  liq.header.xTiles     = *xTiles;
  liq.header.yTiles     = *yTiles;
  liq.header.baseCoord[0] = *baseX;
  liq.header.baseCoord[1] = *baseY;
  liq.header.baseCoord[2] = *baseZ;
  liq.header.materialId = *matId;

  liq.vertexData.assign(vblob->begin(), vblob->end());
  liq.tileFlags.assign(tblob->begin(), tblob->end());
  group.liquid = std::move(liq);
  group.hasLiquid = true;
  return true;
}

}

WmoLoadResult LoadWmoRoot(const uint8_t* data, size_t size) {
  WmoLoadResult result;
  if (data == nullptr || size < sizeof(ChunkHeader)) {
    result.error = "WMO root: null or empty data";
    return result;
  }

  BinaryReader r(data, size);

  bool got_mver = false;
  bool got_mohd = false;

  static constexpr uint32_t kMVER = openwow::data::WowChunkFourCC("MVER");
  static constexpr uint32_t kMOHD = openwow::data::WowChunkFourCC("MOHD");
  static constexpr uint32_t kMOTX = openwow::data::WowChunkFourCC("MOTX");
  static constexpr uint32_t kMOMT = openwow::data::WowChunkFourCC("MOMT");
  static constexpr uint32_t kMOGN = openwow::data::WowChunkFourCC("MOGN");
  static constexpr uint32_t kMOGI = openwow::data::WowChunkFourCC("MOGI");
  static constexpr uint32_t kMOSB = openwow::data::WowChunkFourCC("MOSB");
  static constexpr uint32_t kMOPV = openwow::data::WowChunkFourCC("MOPV");
  static constexpr uint32_t kMOPT = openwow::data::WowChunkFourCC("MOPT");
  static constexpr uint32_t kMOPR = openwow::data::WowChunkFourCC("MOPR");
  static constexpr uint32_t kMOVV = openwow::data::WowChunkFourCC("MOVV");
  static constexpr uint32_t kMOVB = openwow::data::WowChunkFourCC("MOVB");
  static constexpr uint32_t kMOLT = openwow::data::WowChunkFourCC("MOLT");
  static constexpr uint32_t kMODS = openwow::data::WowChunkFourCC("MODS");
  static constexpr uint32_t kMODN = openwow::data::WowChunkFourCC("MODN");
  static constexpr uint32_t kMODD = openwow::data::WowChunkFourCC("MODD");
  static constexpr uint32_t kMFOG = openwow::data::WowChunkFourCC("MFOG");
  static constexpr uint32_t kMCVP = openwow::data::WowChunkFourCC("MCVP");

  size_t cursor = 0;
  while (cursor + sizeof(ChunkHeader) <= size) {
    ChunkHeader hdr{};
    if (!ReadChunkHeader(r, cursor, hdr)) break;

    const size_t data_offset = cursor;
    size_t chunk_end = 0u;
    if (!CheckedChunkEnd(cursor, hdr.size, size, chunk_end)) {
      result.error = "WMO root: chunk extends past end of file";
      return result;
    }

    if (hdr.magic == kMVER) {
      if (!ParseMver(r, data_offset, hdr.size, result.root, result.error)) return result;
      got_mver = true;
    } else if (hdr.magic == kMOHD) {
      if (!ParseMohd(r, data_offset, hdr.size, result.root, result.error)) return result;
      got_mohd = true;
    } else if (hdr.magic == kMOTX) {
      if (!ParseMotx(r, data_offset, hdr.size, result.root, result.error)) return result;
    } else if (hdr.magic == kMOMT) {
      if (!ParseMomt(r, data_offset, hdr.size, result.root, result.error)) return result;
    } else if (hdr.magic == kMOGN) {
      if (!ParseMogn(r, data_offset, hdr.size, result.root, result.error)) return result;
    } else if (hdr.magic == kMOSB) {
      if (!ParseMosb(r, data_offset, hdr.size, result.root, result.error)) return result;
    } else if (hdr.magic == kMOGI) {
      if (!ParseMogi(r, data_offset, hdr.size, result.root, result.error)) return result;
    } else if (hdr.magic == kMOPV) {
      if (!ParseMopv(r, data_offset, hdr.size, result.root, result.error)) return result;
    } else if (hdr.magic == kMOPT) {
      if (!ParseMopt(r, data_offset, hdr.size, result.root, result.error)) return result;
    } else if (hdr.magic == kMOPR) {
      if (!ParseMopr(r, data_offset, hdr.size, result.root, result.error)) return result;
    } else if (hdr.magic == kMOVV) {
      if (!ParseMovv(r, data_offset, hdr.size, result.root, result.error)) return result;
    } else if (hdr.magic == kMOVB) {
      if (!ParseMovb(r, data_offset, hdr.size, result.root, result.error)) return result;
    } else if (hdr.magic == kMOLT) {
      if (!ParseMolt(r, data_offset, hdr.size, result.root, result.error)) return result;
    } else if (hdr.magic == kMODS) {
      if (!ParseMods(r, data_offset, hdr.size, result.root, result.error)) return result;
    } else if (hdr.magic == kMODN) {
      if (!ParseModn(r, data_offset, hdr.size, result.root, result.error)) return result;
    } else if (hdr.magic == kMODD) {
      if (!ParseModd(r, data_offset, hdr.size, result.root, result.error)) return result;
    } else if (hdr.magic == kMFOG) {
      if (!ParseMfog(r, data_offset, hdr.size, result.root, result.error)) return result;
    } else if (hdr.magic == kMCVP) {
      if (!ParseMcvp(r, data_offset, hdr.size, result.root, result.error)) return result;
    }

    cursor = chunk_end;
  }

  if (cursor != size) {
    result.error = "WMO root: truncated chunk header";
    return result;
  }

  if (!got_mver) { result.error = "WMO root: missing MVER chunk"; return result; }
  if (!got_mohd) { result.error = "WMO root: missing MOHD chunk"; return result; }

  if (result.root.skyboxName.empty()) {
    for (auto& group_info : result.root.groupInfos) {
      group_info.flags &= ~static_cast<std::uint32_t>(kMogpShowSkybox);
    }
  }

  result.ok = true;
  return result;
}

WmoLoadResult LoadWmoRoot(const std::vector<uint8_t>& data) {
  return LoadWmoRoot(data.data(), data.size());
}

WmoGroupLoadResult LoadWmoGroup(const uint8_t* data, size_t size) {
  WmoGroupLoadResult result;
  if (data == nullptr || size < sizeof(ChunkHeader)) {
    result.error = "WMO group: null or empty data";
    return result;
  }

  BinaryReader r(data, size);

  size_t cursor = 0;
  ChunkHeader mverHdr{};
  if (!ReadChunkHeader(r, cursor, mverHdr)) {
    result.error = "WMO group: failed to read MVER header";
    return result;
  }
  static constexpr uint32_t kMVER = openwow::data::WowChunkFourCC("MVER");
  if (mverHdr.magic != kMVER) {
    result.error = "WMO group: expected MVER, got 0x" + std::to_string(mverHdr.magic);
    return result;
  }
  if (mverHdr.size > size - cursor) {
    result.error = "WMO group: MVER extends past end of file";
    return result;
  }
  if (mverHdr.size < sizeof(std::uint32_t)) {
    result.error = "WMO group: MVER chunk too small";
    return result;
  }
  const auto group_version = r.ReadU32(cursor);
  if (!group_version || *group_version != 17u) {
    result.error = "WMO group: unsupported WMO version";
    return result;
  }

  cursor += mverHdr.size;

  ChunkHeader mogpHdr{};
  if (!ReadChunkHeader(r, cursor, mogpHdr)) {
    result.error = "WMO group: failed to read MOGP header";
    return result;
  }
  static constexpr uint32_t kMOGP = openwow::data::WowChunkFourCC("MOGP");
  if (mogpHdr.magic != kMOGP) {
    result.error = "WMO group: expected MOGP, got 0x" + std::to_string(mogpHdr.magic);
    return result;
  }

  const size_t mogp_data_start = cursor;
  size_t mogp_end = 0u;
  if (!CheckedChunkEnd(cursor, mogpHdr.size, size, mogp_end)) {
    result.error = "WMO group: MOGP extends past end of file";
    return result;
  }

  if (mogpHdr.size < sizeof(WmoGroupHeader)) {
    result.error = "WMO group: MOGP too small for group header";
    return result;
  }
  auto ghdr_span = r.ReadBytes(mogp_data_start, sizeof(WmoGroupHeader));
  if (!ghdr_span) {
    result.error = "WMO group: failed to read group header";
    return result;
  }
  std::memcpy(&result.group.header, ghdr_span->data(), sizeof(WmoGroupHeader));

  cursor = mogp_data_start + sizeof(WmoGroupHeader);

  static constexpr uint32_t kMOPY = openwow::data::WowChunkFourCC("MOPY");
  static constexpr uint32_t kMOVI = openwow::data::WowChunkFourCC("MOVI");
  static constexpr uint32_t kMOVT = openwow::data::WowChunkFourCC("MOVT");
  static constexpr uint32_t kMONR = openwow::data::WowChunkFourCC("MONR");
  static constexpr uint32_t kMOTV = openwow::data::WowChunkFourCC("MOTV");
  static constexpr uint32_t kMOBA = openwow::data::WowChunkFourCC("MOBA");
  static constexpr uint32_t kMOLR = openwow::data::WowChunkFourCC("MOLR");
  static constexpr uint32_t kMODR = openwow::data::WowChunkFourCC("MODR");
  static constexpr uint32_t kMOBN = openwow::data::WowChunkFourCC("MOBN");
  static constexpr uint32_t kMOBR = openwow::data::WowChunkFourCC("MOBR");
  static constexpr uint32_t kMORI = openwow::data::WowChunkFourCC("MORI");
  static constexpr uint32_t kMORB = openwow::data::WowChunkFourCC("MORB");
  static constexpr uint32_t kMLIQ = openwow::data::WowChunkFourCC("MLIQ");
  static constexpr uint32_t kMOCV = openwow::data::WowChunkFourCC("MOCV");

  const uint32_t flags = result.group.header.flags;
  size_t motv_count = 0;
  size_t mocv_count = 0;
  while (cursor + sizeof(ChunkHeader) <= mogp_end) {
    ChunkHeader sub{};
    if (!ReadChunkHeader(r, cursor, sub)) {
      result.error = "WMO group: failed to read sub-chunk header";
      return result;
    }

    const size_t sub_data = cursor;
    size_t sub_end = 0u;
    if (!CheckedChunkEnd(cursor, sub.size, mogp_end, sub_end)) {
      result.error = "WMO group: sub-chunk extends past MOGP boundary";
      return result;
    }

    if (sub.magic == kMOPY) {
      ParseMopy(r, sub_data, sub.size, result.group);
    } else if (sub.magic == kMOVI) {
      ParseMovi(r, sub_data, sub.size, result.group);
    } else if (sub.magic == kMOVT) {
      ParseMovt(r, sub_data, sub.size, result.group);
    } else if (sub.magic == kMONR) {
      ParseMonr(r, sub_data, sub.size, result.group);
    } else if (sub.magic == kMOTV) {
      if (motv_count == 0) {
        ParseMotv(r, sub_data, sub.size, result.group.texCoords);
      } else if (motv_count == 1 && (flags & kMogpHasSecondMotv)) {
        ParseMotv(r, sub_data, sub.size, result.group.texCoords2);
      }
      ++motv_count;
    } else if (sub.magic == kMOBA) {
      ParseMoba(r, sub_data, sub.size, result.group);
    } else if (sub.magic == kMOLR && (flags & kMogpHasLights)) {
      ParseMolr(r, sub_data, sub.size, result.group);
    } else if (sub.magic == kMODR && (flags & kMogpHasDoodads)) {
      ParseModr(r, sub_data, sub.size, result.group);
    } else if (sub.magic == kMOBN && (flags & kMogpHasBsp)) {
      ParseMobn(r, sub_data, sub.size, result.group);
    } else if (sub.magic == kMOBR && (flags & kMogpHasBsp)) {
      ParseMobr(r, sub_data, sub.size, result.group);
    } else if (sub.magic == kMORI && (flags & kMogpHasMoriMorb)) {
      ParseMori(r, sub_data, sub.size, result.group);
    } else if (sub.magic == kMORB && (flags & kMogpHasMoriMorb)) {
      ParseMorb(r, sub_data, sub.size, result.group);
    } else if (sub.magic == kMLIQ && (flags & kMogpHasWater)) {
      if (!ParseMliq(r, sub_data, sub.size, result.group, result.error))
        return result;
    } else if (sub.magic == kMOCV) {
      if (mocv_count == 0 && (flags & kMogpHasVertexColors)) {
        ParseMocv(r, sub_data, sub.size, result.group.vertexColors);
      } else if (mocv_count == 1 && (flags & kMogpHasSecondMocv)) {
        ParseMocv(r, sub_data, sub.size, result.group.vertexColors2);
      }
      ++mocv_count;
    }

    cursor = sub_end;
  }

  if (cursor != mogp_end) {
    result.error = "WMO group: truncated sub-chunk header";
    return result;
  }

  result.ok = true;
  return result;
}

WmoGroupLoadResult LoadWmoGroup(const std::vector<uint8_t>& data) {
  return LoadWmoGroup(data.data(), data.size());
}

std::string LookupStringInBlock(const std::vector<uint8_t>& block, uint32_t offset) {
  if (offset >= block.size()) return {};
  const char* start = reinterpret_cast<const char*>(block.data() + offset);

  size_t maxLen = block.size() - offset;
  size_t len = 0;
  while (len < maxLen && start[len] != '\0') {
    ++len;
  }
  return std::string(start, len);
}

}
