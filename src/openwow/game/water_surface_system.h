#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace openwow::game {

enum class WaterType : uint8_t {
    Water  = 0,
    Ocean  = 1,
    Magma  = 2,
    Slime  = 3,
};

struct WaterLayerInfo {
    uint32_t  layerId       = 0;
    WaterType type          = WaterType::Water;
    float     minHeight     = 0.0f;
    float     maxHeight     = 0.0f;
    uint32_t  textureFileId = 0;
    uint32_t  flags         = 0;
    float     scrollSpeedU  = 0.0f;
    float     scrollSpeedV  = 0.0f;
    struct { float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f; } color;
    float     transparency  = 0.5f;
    float     fresnelBias   = 0.02f;
    float     fresnelPower  = 5.0f;
};

struct WaterChunkData {
    uint8_t                    chunkX     = 0;
    uint8_t                    chunkY     = 0;
    std::vector<WaterLayerInfo> layers;
    std::array<float, 81>      heightMap{};
    std::array<uint8_t, 81>    depthMap{};
    uint64_t                    existsMask = 0;
};

class WaterSurfaceSystem {
public:

    static constexpr float kWaveAmplitude  = 0.5f;
    static constexpr float kWaveFrequency  = 1.2f;
    static constexpr float kWaveLength     = 4.0f;
    static constexpr uint8_t kMaxLayers    = 4;
    static constexpr uint8_t kGridDim      = 9;
    static constexpr uint8_t kSubTileDim   = 8;

    WaterSurfaceSystem() = default;

    void SetChunkWater(uint8_t chunkX, uint8_t chunkY, WaterChunkData data);

    [[nodiscard]] std::optional<WaterChunkData> GetChunkWater(
        uint8_t chunkX, uint8_t chunkY) const;

    [[nodiscard]] std::optional<float> GetWaterHeight(
        uint8_t chunkX, uint8_t chunkY, float localX, float localY) const;

    [[nodiscard]] std::optional<uint8_t> GetWaterDepth(
        uint8_t chunkX, uint8_t chunkY, float localX, float localY) const;

    [[nodiscard]] std::optional<WaterType> GetWaterType(
        uint8_t chunkX, uint8_t chunkY) const;

    [[nodiscard]] bool HasWater(uint8_t chunkX, uint8_t chunkY,
                                uint8_t subTileX, uint8_t subTileY) const;

    [[nodiscard]] bool IsUnderwater(float worldX, float worldY,
                                    float worldZ) const;

    [[nodiscard]] float GetAnimatedHeight(uint8_t chunkX, uint8_t chunkY,
                                          float localX, float localY,
                                          float time) const;

    struct WaterColor { float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f; };
    [[nodiscard]] std::optional<WaterColor> GetWaterColor(
        uint8_t chunkX, uint8_t chunkY) const;

    [[nodiscard]] uint32_t GetChunkCount() const;
    void ClearAll();

private:
    [[nodiscard]] static uint32_t ChunkKey(uint8_t x, uint8_t y) {
        return (static_cast<uint32_t>(y) << 8) | x;
    }

    [[nodiscard]] static float InterpolateGrid9(
        const std::array<float, 81>& grid, float localX, float localY);

    [[nodiscard]] static float InterpolateGrid9u8(
        const std::array<uint8_t, 81>& grid, float localX, float localY);

    std::unordered_map<uint32_t, WaterChunkData> chunks_;
};

}
