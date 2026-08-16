#pragma once

#include <cstdint>
#include <vector>

namespace openwow::net::wotlk {

struct BNetModuleEntry {
  std::uint32_t module_id{0};
  std::uint8_t version{0};
};

inline constexpr std::uint32_t kFourCC_Relm = 0x52656C6Du;
inline constexpr std::uint32_t kFourCC_Race = 0x52616365u;
inline constexpr std::uint32_t kFourCC_Clss = 0x436C7373u;
inline constexpr std::uint32_t kFourCC_Area = 0x41726561u;

inline constexpr std::uint8_t kBNetModuleInfoOpcode = 0x45;

[[nodiscard]] std::vector<BNetModuleEntry> GetDefaultBNetModuleTable();

[[nodiscard]] std::vector<std::uint8_t> BuildBNetModuleInfoPacket();

[[nodiscard]] std::vector<std::uint8_t> BuildBNetModuleInfoPacket(
    const std::vector<BNetModuleEntry>& modules,
    const std::vector<std::uint32_t>& cache_types);

}
