#pragma once

#include "openwow/world/terrain/terrain_lod_subdivision.h"

#include <cstdint>

namespace openwow::render {

class TerrainLodManager {
public:

    [[nodiscard]] static const world::ChunkLodIndexSet& GetLodIndexSet();

    [[nodiscard]] static const world::LodIndexData& GetLodIndexData(int lod);

private:

    static world::ChunkLodIndexSet s_lodIndexSet_;
    static bool s_lodIndexSetReady_;
};

}
