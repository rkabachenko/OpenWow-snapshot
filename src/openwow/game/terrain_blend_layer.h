#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace openwow::game {

struct TerrainTextureLayer {
    uint32_t textureFileId  = 0;
    uint32_t flags          = 0;
    uint32_t effectId       = 0;
    uint32_t alphaMapOffset = 0;
    uint32_t alphaMapSize   = 0;

    [[nodiscard]] bool IsCompressedAlpha() const { return (flags & 0x1) != 0; }
    [[nodiscard]] bool IsWrapTexture()     const { return (flags & 0x100) != 0; }
};

struct TerrainChunkBlend {
    uint8_t chunkX = 0;
    uint8_t chunkY = 0;
    std::vector<TerrainTextureLayer> layers;
    std::array<uint8_t, 4096>        alphaMap{};
    std::array<float, 145>           heightMap{};
};

class TerrainBlendLayer {
public:
    static constexpr uint8_t  kMaxChunkAxis   = 16;
    static constexpr uint32_t kMaxChunks      = 256;
    static constexpr uint32_t kAlphaMapSize   = 4096;
    static constexpr uint8_t  kAlphaMapDim    = 64;
    static constexpr uint8_t  kMaxLayers      = 4;
    static constexpr uint32_t kHeightMapVerts = 145;

    TerrainBlendLayer() = default;

    void SetChunkData(uint8_t chunkX, uint8_t chunkY,
                      TerrainChunkBlend blend);

    [[nodiscard]] std::optional<TerrainChunkBlend> GetChunkData(
        uint8_t chunkX, uint8_t chunkY) const;

    [[nodiscard]] bool IsChunkLoaded(uint8_t chunkX, uint8_t chunkY) const;

    [[nodiscard]] uint8_t GetAlphaWeight(uint8_t chunkX, uint8_t chunkY,
                                         uint8_t layer,
                                         uint8_t texelX, uint8_t texelY) const;

    void SetAlphaWeight(uint8_t chunkX, uint8_t chunkY,
                        uint8_t layer,
                        uint8_t texelX, uint8_t texelY, uint8_t weight);

    [[nodiscard]] uint8_t  GetLayerCount(uint8_t chunkX, uint8_t chunkY) const;
    [[nodiscard]] uint32_t GetTextureId(uint8_t chunkX, uint8_t chunkY,
                                         uint8_t layer) const;

    [[nodiscard]] float GetHeight(uint8_t chunkX, uint8_t chunkY,
                                  uint32_t vertexIndex) const;

    [[nodiscard]] float GetInterpolatedHeight(uint8_t chunkX, uint8_t chunkY,
                                              float localX, float localY) const;

    [[nodiscard]] uint32_t GetChunkCount() const;
    void ClearAll();

private:
    [[nodiscard]] static uint32_t ChunkKey(uint8_t x, uint8_t y) {
        return (static_cast<uint32_t>(y) << 8) | x;
    }

    std::unordered_map<uint32_t, TerrainChunkBlend> chunks_;
};

}
