#pragma once

#include "openwow/data/terrain/adt_file.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace openwow::data::terrain {

constexpr int kWdlTilesPerSide = 64;

constexpr int kWdlOuterGridSize = 17;

constexpr int kWdlInnerGridSize = 16;

constexpr int kWdlMareSize =
    kWdlOuterGridSize * kWdlOuterGridSize * 2 +
    kWdlInnerGridSize * kWdlInnerGridSize * 2;

struct WdlTileHeights {

  std::array<std::array<std::int16_t, kWdlOuterGridSize>, kWdlOuterGridSize>
      outer{};

  std::array<std::array<std::int16_t, kWdlInnerGridSize>, kWdlInnerGridSize>
      inner{};
};

struct WdlTileHoles {
  std::array<std::uint16_t, 16> rows{};
};

struct WdlFile {
  std::uint32_t version{0};

  std::vector<std::string> wmos{};

  std::vector<WmoPlacement> wmo_placements{};

  std::array<std::array<WdlTileHeights*, kWdlTilesPerSide>, kWdlTilesPerSide>
      tile_heights{};

  std::array<std::array<WdlTileHoles*, kWdlTilesPerSide>, kWdlTilesPerSide>
      tile_holes{};

  [[nodiscard]] bool HasTile(uint32_t x, uint32_t y) const {
    if (x >= kWdlTilesPerSide || y >= kWdlTilesPerSide) return false;
    return tile_heights[y][x] != nullptr;
  }

  [[nodiscard]] std::int16_t GetLowResHeight(int tile_x, int tile_y,
                                             int local_x, int local_y) const {
    if (tile_x < 0 || tile_x >= kWdlTilesPerSide ||
        tile_y < 0 || tile_y >= kWdlTilesPerSide)
      return 0;
    const auto* th = tile_heights[tile_y][tile_x];
    if (!th) return 0;
    if (local_x < 0 || local_x >= kWdlOuterGridSize ||
        local_y < 0 || local_y >= kWdlOuterGridSize)
      return 0;
    return th->outer[local_y][local_x];
  }

  [[nodiscard]] std::uint32_t CountTilesWithData() const {
    std::uint32_t count = 0;
    for (int y = 0; y < kWdlTilesPerSide; ++y)
      for (int x = 0; x < kWdlTilesPerSide; ++x)
        if (tile_heights[y][x] != nullptr) ++count;
    return count;
  }

  WdlFile() = default;
  WdlFile(const WdlFile&) = delete;
  WdlFile& operator=(const WdlFile&) = delete;
  WdlFile(WdlFile&& other) noexcept;
  WdlFile& operator=(WdlFile&& other) noexcept;
  ~WdlFile();
};

struct WdlLoadResult {
  bool ok{false};
  std::string error;
  WdlFile wdl;
};

WdlLoadResult LoadWdl(const uint8_t* data, size_t size);

WdlLoadResult LoadWdl(const std::vector<uint8_t>& data);

}
