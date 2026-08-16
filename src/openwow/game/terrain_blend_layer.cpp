#include "openwow/game/terrain_blend_layer.h"

#include <algorithm>
#include <cmath>

namespace openwow::game {

void TerrainBlendLayer::SetChunkData(uint8_t chunkX, uint8_t chunkY,
                                     TerrainChunkBlend blend) {
    if (chunkX >= kMaxChunkAxis || chunkY >= kMaxChunkAxis) return;
    blend.chunkX = chunkX;
    blend.chunkY = chunkY;
    if (blend.layers.size() > kMaxLayers)
        blend.layers.resize(kMaxLayers);
    chunks_[ChunkKey(chunkX, chunkY)] = std::move(blend);
}

std::optional<TerrainChunkBlend> TerrainBlendLayer::GetChunkData(
    uint8_t chunkX, uint8_t chunkY) const {
    auto it = chunks_.find(ChunkKey(chunkX, chunkY));
    if (it == chunks_.end()) return std::nullopt;
    return it->second;
}

bool TerrainBlendLayer::IsChunkLoaded(uint8_t chunkX, uint8_t chunkY) const {
    return chunks_.count(ChunkKey(chunkX, chunkY)) > 0;
}

uint8_t TerrainBlendLayer::GetAlphaWeight(uint8_t chunkX, uint8_t chunkY,
                                           uint8_t layer,
                                           uint8_t texelX,
                                           uint8_t texelY) const {
    if (texelX >= kAlphaMapDim || texelY >= kAlphaMapDim) return 0;
    auto it = chunks_.find(ChunkKey(chunkX, chunkY));
    if (it == chunks_.end()) return 0;
    if (layer >= it->second.layers.size()) return 0;

    uint32_t idx = static_cast<uint32_t>(texelY) * kAlphaMapDim + texelX;
    if (idx >= kAlphaMapSize) return 0;
    return it->second.alphaMap[idx];
}

void TerrainBlendLayer::SetAlphaWeight(uint8_t chunkX, uint8_t chunkY,
                                        uint8_t layer,
                                        uint8_t texelX, uint8_t texelY,
                                        uint8_t weight) {
    if (texelX >= kAlphaMapDim || texelY >= kAlphaMapDim) return;
    auto it = chunks_.find(ChunkKey(chunkX, chunkY));
    if (it == chunks_.end()) return;
    if (layer >= it->second.layers.size()) return;
    uint32_t idx = static_cast<uint32_t>(texelY) * kAlphaMapDim + texelX;
    if (idx >= kAlphaMapSize) return;
    it->second.alphaMap[idx] = weight;
}

uint8_t TerrainBlendLayer::GetLayerCount(uint8_t chunkX,
                                          uint8_t chunkY) const {
    auto it = chunks_.find(ChunkKey(chunkX, chunkY));
    if (it == chunks_.end()) return 0;
    return static_cast<uint8_t>(it->second.layers.size());
}

uint32_t TerrainBlendLayer::GetTextureId(uint8_t chunkX, uint8_t chunkY,
                                          uint8_t layer) const {
    auto it = chunks_.find(ChunkKey(chunkX, chunkY));
    if (it == chunks_.end()) return 0;
    if (layer >= it->second.layers.size()) return 0;
    return it->second.layers[layer].textureFileId;
}

float TerrainBlendLayer::GetHeight(uint8_t chunkX, uint8_t chunkY,
                                    uint32_t vertexIndex) const {
    if (vertexIndex >= kHeightMapVerts) return 0.0f;
    auto it = chunks_.find(ChunkKey(chunkX, chunkY));
    if (it == chunks_.end()) return 0.0f;
    return it->second.heightMap[vertexIndex];
}

float TerrainBlendLayer::GetInterpolatedHeight(uint8_t chunkX, uint8_t chunkY,
                                                float localX,
                                                float localY) const {
    auto it = chunks_.find(ChunkKey(chunkX, chunkY));
    if (it == chunks_.end()) return 0.0f;

    localX = std::clamp(localX, 0.0f, 1.0f);
    localY = std::clamp(localY, 0.0f, 1.0f);

    float fx = localX * 8.0f;
    float fy = localY * 8.0f;
    int ix = std::min(static_cast<int>(fx), 7);
    int iy = std::min(static_cast<int>(fy), 7);
    float fracX = fx - static_cast<float>(ix);
    float fracY = fy - static_cast<float>(iy);

    const auto& hm = it->second.heightMap;

    auto outerIdx = [](int r, int c) -> uint32_t {
        return static_cast<uint32_t>(r * 17 + c);
    };
    auto innerIdx = [](int r, int c) -> uint32_t {
        return static_cast<uint32_t>(r * 17 + 9 + c);
    };

    float h00 = hm[outerIdx(iy, ix)];
    float h10 = hm[outerIdx(iy, ix + 1)];
    float h01 = hm[outerIdx(iy + 1, ix)];
    float h11 = hm[outerIdx(iy + 1, ix + 1)];

    float hCenter = hm[innerIdx(iy, ix)];

    if (fracX + fracY < 1.0f) {

        if (fracX < fracY) {

            return h00 + (hCenter - h00) * 2.0f * fracX +
                   (h01 - h00) * fracY;
        }

        return h00 + (h10 - h00) * fracX +
               (hCenter - h00) * 2.0f * fracY;
    }

    if (fracX > fracY) {

        return h10 + (h11 - h10) * fracY +
               (hCenter - h10) * 2.0f * (1.0f - fracX);
    }

    return h01 + (h11 - h01) * fracX +
           (hCenter - h01) * 2.0f * (1.0f - fracY);
}

uint32_t TerrainBlendLayer::GetChunkCount() const {
    return static_cast<uint32_t>(chunks_.size());
}

void TerrainBlendLayer::ClearAll() { chunks_.clear(); }

}
