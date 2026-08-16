
#include "openwow/world/terrain/terrain_lod_subdivision.h"

namespace openwow::world {

static uint16_t O(uint16_t row, uint16_t col) {
    return static_cast<uint16_t>(row * kMcnkGridStride + col);
}

static uint16_t I(uint16_t row, uint16_t col) {
    return static_cast<uint16_t>(row * kMcnkGridStride + 9 + col);
}

static void BuildLod0Indices(LodIndexData& data) {
    uint32_t idx = 0;
    for (int r = 0; r < kMcnkInnerSize; ++r) {
        for (int c = 0; c < kMcnkInnerSize; ++c) {
            const uint16_t tl = O(r,     c);
            const uint16_t tr = O(r,     c + 1);
            const uint16_t bl = O(r + 1, c);
            const uint16_t br = O(r + 1, c + 1);
            const uint16_t ct = I(r,     c);

            data.indices[idx++] = bl;
            data.indices[idx++] = ct;
            data.indices[idx++] = tl;

            data.indices[idx++] = ct;
            data.indices[idx++] = tr;
            data.indices[idx++] = tl;

            data.indices[idx++] = ct;
            data.indices[idx++] = bl;
            data.indices[idx++] = br;

            data.indices[idx++] = ct;
            data.indices[idx++] = br;
            data.indices[idx++] = tr;
        }
    }
    data.count = idx;
}

static void BuildLod1Indices(LodIndexData& data) {
    uint32_t idx = 0;
    for (int r = 0; r < kMcnkInnerSize; ++r) {
        for (int c = 0; c < kMcnkInnerSize; ++c) {
            const uint16_t tl = O(r,     c);
            const uint16_t tr = O(r,     c + 1);
            const uint16_t bl = O(r + 1, c);
            const uint16_t br = O(r + 1, c + 1);

            data.indices[idx++] = bl;
            data.indices[idx++] = tr;
            data.indices[idx++] = tl;

            data.indices[idx++] = tr;
            data.indices[idx++] = bl;
            data.indices[idx++] = br;
        }
    }
    data.count = idx;
}

static void BuildLod2Indices(LodIndexData& data) {
    uint32_t idx = 0;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            const uint16_t tl = O(r * 2,     c * 2);
            const uint16_t tr = O(r * 2,     (c + 1) * 2);
            const uint16_t bl = O((r + 1) * 2, c * 2);
            const uint16_t br = O((r + 1) * 2, (c + 1) * 2);

            data.indices[idx++] = bl;
            data.indices[idx++] = tr;
            data.indices[idx++] = tl;

            data.indices[idx++] = tr;
            data.indices[idx++] = bl;
            data.indices[idx++] = br;
        }
    }
    data.count = idx;
}

static void BuildLod3Indices(LodIndexData& data) {
    const uint16_t tl = O(0, 0);
    const uint16_t tr = O(0, 8);
    const uint16_t bl = O(8, 0);
    const uint16_t br = O(8, 8);
    const uint16_t cn = O(4, 4);

    data.indices[0] = cn;
    data.indices[1] = tr;
    data.indices[2] = tl;

    data.indices[3] = cn;
    data.indices[4] = br;
    data.indices[5] = tr;

    data.indices[6] = cn;
    data.indices[7] = bl;
    data.indices[8] = br;

    data.indices[9]  = cn;
    data.indices[10] = tl;
    data.indices[11] = bl;

    data.count = 12;
}

LodIndexData BuildLodIndices(int lodLevel) {
    LodIndexData data{};
    switch (lodLevel) {
        case 0: BuildLod0Indices(data); break;
        case 1: BuildLod1Indices(data); break;
        case 2: BuildLod2Indices(data); break;
        case 3: BuildLod3Indices(data); break;
        default: BuildLod3Indices(data); break;
    }
    return data;
}

ChunkLodIndexSet BuildLodIndexSet() {
    ChunkLodIndexSet set{};
    for (int i = 0; i < kTerrainLodCount; ++i) {
        set.levels[i] = BuildLodIndices(i);
    }
    return set;
}

}
