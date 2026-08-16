
#include "openwow/net/wotlk/grunt_bnet_module_info.h"

#include <array>

namespace openwow::net::wotlk {

namespace {

void PutUInt16(std::vector<std::uint8_t>& buf, std::uint16_t v) {
  buf.push_back(static_cast<std::uint8_t>(v & 0xFFu));
  buf.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
}

void PutUInt32(std::vector<std::uint8_t>& buf, std::uint32_t v) {
  buf.push_back(static_cast<std::uint8_t>(v & 0xFFu));
  buf.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
  buf.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
  buf.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
}

constexpr std::array<BNetModuleEntry, 7> kDefaultModules = {{
    {0x00030001u, 14},
    {0x00030002u, 14},
    {0x00030003u, 14},
    {0x00030004u, 13},
    {0x00030005u,  1},
    {0x00030006u, 14},
    {0x00040001u,  1},
}};

constexpr std::array<std::uint32_t, 4> kDefaultCacheTypes = {
    kFourCC_Relm,
    kFourCC_Race,
    kFourCC_Clss,
    kFourCC_Area,
};

}

std::vector<BNetModuleEntry> GetDefaultBNetModuleTable() {
  return {kDefaultModules.begin(), kDefaultModules.end()};
}

static void WriteBNetModuleInfoPayload(
    std::vector<std::uint8_t>& buf,
    const std::vector<BNetModuleEntry>& modules,
    const std::vector<std::uint32_t>& cache_types) {

  PutUInt16(buf, static_cast<std::uint16_t>(modules.size()));

  for (const auto& m : modules) {
    PutUInt32(buf, m.module_id);
    buf.push_back(m.version);
    buf.push_back(0);
    buf.push_back(0);
  }

  const std::size_t len_offset = buf.size();
  PutUInt16(buf, 0);

  for (const auto fourcc : cache_types) {
    PutUInt32(buf, fourcc);
  }

  const auto count = static_cast<std::uint16_t>(cache_types.size());
  buf[len_offset] = static_cast<std::uint8_t>(count & 0xFFu);
  buf[len_offset + 1] = static_cast<std::uint8_t>((count >> 8) & 0xFFu);
}

std::vector<std::uint8_t> BuildBNetModuleInfoPacket() {
  const std::vector<BNetModuleEntry> modules{kDefaultModules.begin(),
                                              kDefaultModules.end()};
  const std::vector<std::uint32_t> cache_types{kDefaultCacheTypes.begin(),
                                                kDefaultCacheTypes.end()};
  return BuildBNetModuleInfoPacket(modules, cache_types);
}

std::vector<std::uint8_t> BuildBNetModuleInfoPacket(
    const std::vector<BNetModuleEntry>& modules,
    const std::vector<std::uint32_t>& cache_types) {

  std::vector<std::uint8_t> packet;
  packet.reserve(1 + 2 + modules.size() * 7 + 2 + cache_types.size() * 4);

  packet.push_back(kBNetModuleInfoOpcode);

  WriteBNetModuleInfoPayload(packet, modules, cache_types);

  return packet;
}

}
