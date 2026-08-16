#include "openwow/game/water_surface_system.h"

#include <algorithm>
#include <cmath>

namespace openwow::game {

void WaterSurfaceSystem::SetChunkWater(uint8_t chunkX, uint8_t chunkY,
                                        WaterChunkData data) {
    data.chunkX = chunkX;
    data.chunkY = chunkY;
    if (data.layers.size() > kMaxLayers)
        data.layers.resize(kMaxLayers);
    chunks_[ChunkKey(chunkX, chunkY)] = std::move(data);
}

std::optional<WaterChunkData> WaterSurfaceSystem::GetChunkWater(
    uint8_t chunkX, uint8_t chunkY) const {
    auto it = chunks_.find(ChunkKey(chunkX, chunkY));
    if (it == chunks_.end()) return std::nullopt;
    return it->second;
}

float WaterSurfaceSystem::InterpolateGrid9(const std::array<float, 81>& grid,
                                            float localX, float localY) {
    localX = std::clamp(localX, 0.0f, 1.0f);
    localY = std::clamp(localY, 0.0f, 1.0f);
    float fx = localX * 8.0f;
    float fy = localY * 8.0f;
    int ix = std::min(static_cast<int>(fx), 7);
    int iy = std::min(static_cast<int>(fy), 7);
    float fracX = fx - static_cast<float>(ix);
    float fracY = fy - static_cast<float>(iy);

    float h00 = grid[static_cast<size_t>(iy * 9 + ix)];
    float h10 = grid[static_cast<size_t>(iy * 9 + ix + 1)];
    float h01 = grid[static_cast<size_t>((iy + 1) * 9 + ix)];
    float h11 = grid[static_cast<size_t>((iy + 1) * 9 + ix + 1)];

    float top    = h00 + (h10 - h00) * fracX;
    float bottom = h01 + (h11 - h01) * fracX;
    return top + (bottom - top) * fracY;
}

float WaterSurfaceSystem::InterpolateGrid9u8(
    const std::array<uint8_t, 81>& grid, float localX, float localY) {
    localX = std::clamp(localX, 0.0f, 1.0f);
    localY = std::clamp(localY, 0.0f, 1.0f);
    float fx = localX * 8.0f;
    float fy = localY * 8.0f;
    int ix = std::min(static_cast<int>(fx), 7);
    int iy = std::min(static_cast<int>(fy), 7);
    float fracX = fx - static_cast<float>(ix);
    float fracY = fy - static_cast<float>(iy);

    float h00 = static_cast<float>(grid[static_cast<size_t>(iy * 9 + ix)]);
    float h10 = static_cast<float>(grid[static_cast<size_t>(iy * 9 + ix + 1)]);
    float h01 = static_cast<float>(grid[static_cast<size_t>((iy + 1) * 9 + ix)]);
    float h11 = static_cast<float>(grid[static_cast<size_t>((iy + 1) * 9 + ix + 1)]);

    float top    = h00 + (h10 - h00) * fracX;
    float bottom = h01 + (h11 - h01) * fracX;
    return top + (bottom - top) * fracY;
}

std::optional<float> WaterSurfaceSystem::GetWaterHeight(
    uint8_t chunkX, uint8_t chunkY, float localX, float localY) const {
    auto it = chunks_.find(ChunkKey(chunkX, chunkY));
    if (it == chunks_.end() || it->second.layers.empty()) return std::nullopt;
    return InterpolateGrid9(it->second.heightMap, localX, localY);
}

std::optional<uint8_t> WaterSurfaceSystem::GetWaterDepth(
    uint8_t chunkX, uint8_t chunkY, float localX, float localY) const {
    auto it = chunks_.find(ChunkKey(chunkX, chunkY));
    if (it == chunks_.end() || it->second.layers.empty()) return std::nullopt;
    float v = InterpolateGrid9u8(it->second.depthMap, localX, localY);
    return static_cast<uint8_t>(std::clamp(v, 0.0f, 255.0f));
}

std::optional<WaterType> WaterSurfaceSystem::GetWaterType(
    uint8_t chunkX, uint8_t chunkY) const {
    auto it = chunks_.find(ChunkKey(chunkX, chunkY));
    if (it == chunks_.end() || it->second.layers.empty()) return std::nullopt;
    return it->second.layers.front().type;
}

bool WaterSurfaceSystem::HasWater(uint8_t chunkX, uint8_t chunkY,
                                   uint8_t subTileX,
                                   uint8_t subTileY) const {
    if (subTileX >= kSubTileDim || subTileY >= kSubTileDim) return false;
    auto it = chunks_.find(ChunkKey(chunkX, chunkY));
    if (it == chunks_.end()) return false;
    uint32_t bit = static_cast<uint32_t>(subTileY * kSubTileDim + subTileX);
    return (it->second.existsMask & (uint64_t{1} << bit)) != 0;
}

bool WaterSurfaceSystem::IsUnderwater(float , float ,
                                       float worldZ) const {

    for (auto& [key, chunk] : chunks_) {
        if (chunk.layers.empty()) continue;
        if (worldZ < chunk.layers.front().maxHeight) return true;
    }
    return false;
}

float WaterSurfaceSystem::GetAnimatedHeight(uint8_t chunkX, uint8_t chunkY,
                                             float localX, float localY,
                                             float time) const {
    auto base = GetWaterHeight(chunkX, chunkY, localX, localY);
    float h = base.value_or(0.0f);
    return h + kWaveAmplitude *
               std::sin(time * kWaveFrequency + localX * kWaveLength);
}

std::optional<WaterSurfaceSystem::WaterColor>
WaterSurfaceSystem::GetWaterColor(uint8_t chunkX, uint8_t chunkY) const {
    auto it = chunks_.find(ChunkKey(chunkX, chunkY));
    if (it == chunks_.end() || it->second.layers.empty()) return std::nullopt;
    auto& c = it->second.layers.front().color;
    return WaterColor{c.r, c.g, c.b, c.a};
}

uint32_t WaterSurfaceSystem::GetChunkCount() const {
    return static_cast<uint32_t>(chunks_.size());
}

void WaterSurfaceSystem::ClearAll() { chunks_.clear(); }

}
