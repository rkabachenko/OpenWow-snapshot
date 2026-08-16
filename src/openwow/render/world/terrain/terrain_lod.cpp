
#include "openwow/render/world/terrain/terrain_lod.h"

#include <algorithm>

namespace openwow::render {

world::ChunkLodIndexSet TerrainLodManager::s_lodIndexSet_{};
bool TerrainLodManager::s_lodIndexSetReady_ = false;

const world::ChunkLodIndexSet& TerrainLodManager::GetLodIndexSet() {
    if (!s_lodIndexSetReady_) {
        s_lodIndexSet_ = world::BuildLodIndexSet();
        s_lodIndexSetReady_ = true;
    }
    return s_lodIndexSet_;
}

const world::LodIndexData& TerrainLodManager::GetLodIndexData(int lod) {
    const auto& set = GetLodIndexSet();
    const int clampedLod = std::max(0, std::min(lod, world::kTerrainLodCount - 1));
    return set.levels[clampedLod];
}

}
