#pragma once

#include <array>
#include <cstdint>

namespace openwow::world {

inline constexpr int kTerrainLodCount = 4;

inline constexpr int kMcnkGridStride = 17;

inline constexpr int kMcnkOuterSize = 9;

inline constexpr int kMcnkInnerSize = 8;

inline constexpr int kMcnkTotalVertices = 145;

inline constexpr int kLodVertexCounts[kTerrainLodCount] = {
    145,
    81,
    25,
    5,
};

inline constexpr int kLodIndexCounts[kTerrainLodCount] = {
    768,
    384,
    96,
    12,
};

struct LodIndexData {
    std::array<uint16_t, 768> indices{};
    uint32_t count = 0;

    [[nodiscard]] uint32_t triangleCount() const noexcept { return count / 3; }
};

struct ChunkLodIndexSet {
    std::array<LodIndexData, kTerrainLodCount> levels{};
};

[[nodiscard]] ChunkLodIndexSet BuildLodIndexSet();

[[nodiscard]] LodIndexData BuildLodIndices(int lodLevel);

}
