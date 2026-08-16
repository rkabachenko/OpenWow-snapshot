#pragma once

#include <array>
#include <cstdint>

namespace openwow::data::terrain {
struct WdlFile;
}

namespace openwow::data::map {

struct CoarseTileHeights {
  std::array<std::int16_t, 9> layer0{};
  std::array<std::int16_t, 9> layer1{};
};

class CoarseHeightQuery {
 public:
  static CoarseHeightQuery& Instance() {
    static CoarseHeightQuery instance;
    return instance;
  }
  static constexpr int kTilesPerSide = 64;
  static constexpr int kVerticesPerTile = 9;
  static constexpr int kLayerCount = 2;
  static constexpr int kTriangleCount = 8;

  void SetTileData(int tileX, int tileY,
                   const std::int16_t layer0[9],
                   const std::int16_t layer1[9]);

  void ClearTile(int tileX, int tileY);

  void Clear();

  void PopulateFromWdl(const openwow::data::terrain::WdlFile& wdl);

  void SetDisabled(bool disabled);
  [[nodiscard]] bool IsDisabled() const;

  [[nodiscard]] bool QueryLayer(const float* pos, float* outHeight,
                                int layer) const;

  [[nodiscard]] bool QueryLayer0(const float* pos, float* outHeight) const;

  [[nodiscard]] bool QueryLayer1(const float* pos, float* outHeight) const;

  [[nodiscard]] bool HasTileData(int tileX, int tileY) const;
  [[nodiscard]] int GetLoadedTileCount() const;

 private:
  struct TileSlot {
    CoarseTileHeights heights;
    bool loaded = false;
  };

  std::array<std::array<TileSlot, kTilesPerSide>, kTilesPerSide> tiles_{};
  bool disabled_ = false;
};

}
