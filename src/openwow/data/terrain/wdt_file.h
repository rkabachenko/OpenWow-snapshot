#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace openwow::data::terrain {

constexpr int kWdtTilesPerSide = 64;

namespace WdtFlags {
inline constexpr uint32_t kGlobalWmo = 0x01;
inline constexpr uint32_t kMccvInAdts = 0x02;
inline constexpr uint32_t kBigAlpha = 0x04;
inline constexpr uint32_t kHeightTexSorting = 0x08;
}

struct WdtTileEntry {
  uint32_t flags;
  uint32_t async_id;
};
static_assert(sizeof(WdtTileEntry) == 8, "WdtTileEntry must be 8 bytes.");

struct WdtWmoPlacement {
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
static_assert(sizeof(WdtWmoPlacement) == 64, "WdtWmoPlacement must be 64 bytes.");

struct WdtFile {
  uint32_t version{0};
  uint32_t flags{0};
  std::array<std::array<WdtTileEntry, 64>, 64> tiles{};
  std::string global_wmo_path;
  WdtWmoPlacement global_wmo_placement{};
  bool has_global_wmo{false};

  [[nodiscard]] bool TileExists(uint32_t x, uint32_t y) const {
    if (x >= 64 || y >= 64)
      return false;
    return (tiles[y][x].flags & 1) != 0;
  }

  [[nodiscard]] uint32_t CountExistingTiles() const {
    uint32_t count = 0;
    for (uint32_t y = 0; y < 64; ++y)
      for (uint32_t x = 0; x < 64; ++x)
        if (TileExists(x, y))
          ++count;
    return count;
  }
};

struct WdtLoadResult {
  bool ok{false};
  std::string error;
  WdtFile wdt;
};

WdtLoadResult LoadWdt(const uint8_t *data, size_t size);

WdtLoadResult LoadWdt(const std::vector<uint8_t> &data);

}
