
#include "openwow/game/callee_wrappers.h"

#include "openwow/runtime/time/game_clock.h"
#include "openwow/data/map/coarse_height_query.h"

#include <cmath>
#include <cstring>

namespace openwow::game {

bool Unit_C_Func45_Callee(uint32_t , const float* ,
                          int , float ,
                          int , int , int ) {

    return false;
}

bool Minimap_GetFirstTerrainTilePath_Callee(const int* area_info,
                                            const char** out_path) {
    if (!area_info) return false;

    (void)out_path;
    return false;
}

bool Minimap_UpdateTerrainTiles_Callee(const int* param1, const float* ,
                                       const uint32_t* ,
                                       uint32_t , int ,
                                       bool ) {
    if (!param1) return false;

    return false;
}

bool Minimap_GetWmoGroupMatrices_Callee(const int* wmo_list,
                                        float* ,
                                        float* ) {
    if (!wmo_list) return false;

    return false;
}

bool Mapobj_GetAreaTableId(const uint32_t* map_obj, uint32_t* out_area_table_id) {
    if (!map_obj) return false;

    constexpr uint32_t kAreaTableIdByteOffset = 0xB8u;
    const auto area_id = *reinterpret_cast<const uint32_t*>(
        reinterpret_cast<const uint8_t*>(map_obj) + kAreaTableIdByteOffset);
    if (area_id == 0xFFFFFFFFu) return false;

    *out_area_table_id = area_id;
    return true;
}

double Script_GetFramerate_Callee() {
    return openwow::core::GameClock::Instance().SmoothedFPS();
}

int Camera_FullUpdate_Callee(const float* position, float* out_height) {
    return static_cast<int>(
        openwow::data::map::CoarseHeightQuery::Instance().QueryLayer1(
            position, out_height));
}

static int s_client_global_CD7764 = 0;

int ClientInit_SetGlobal(int value) {
    s_client_global_CD7764 = value;
    return value;
}

}

namespace openwow::render {

void* SWModelFadeout_Alloc(void* , int , int extra_size,
                           bool ) {
    uint32_t alloc_size = static_cast<uint32_t>(extra_size) + 96;
    auto* ptr = new uint8_t[alloc_size]();

    if (!ptr) return nullptr;

    auto* f = reinterpret_cast<float*>(ptr);

    f[6]  = 1.0f; f[7]  = 0.0f; f[8]  = 0.0f; f[9]  = 0.0f;
    f[10] = 0.0f; f[11] = 1.0f; f[12] = 0.0f; f[13] = 0.0f;
    f[14] = 0.0f; f[15] = 0.0f; f[16] = 1.0f; f[17] = 0.0f;
    f[18] = 0.0f; f[19] = 0.0f; f[20] = 0.0f; f[21] = 1.0f;

    f[22] = 0.0f;
    f[23] = 0.0f;

    return ptr;
}

void SWModelFadeout_FreeList(void* ) {

}

}
