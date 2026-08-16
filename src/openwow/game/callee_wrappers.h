#pragma once

#include <cstdint>

namespace openwow::game {

bool Unit_C_Func45_Callee(uint32_t unit_guid, const float* pos, int param3,
                          float param4, int param5, int param6, int param7);

bool Minimap_GetFirstTerrainTilePath_Callee(const int* area_info,
                                            const char** out_path);

bool Minimap_UpdateTerrainTiles_Callee(const int* param1, const float* param2,
                                       const uint32_t* param3, uint32_t param4,
                                       int param5, bool param6);

bool Minimap_GetWmoGroupMatrices_Callee(const int* wmo_list,
                                        float* out_model_to_world,
                                        float* out_world_to_model);

bool Mapobj_GetAreaTableId(const uint32_t* map_obj, uint32_t* out_area_table_id);

double Script_GetFramerate_Callee();

int Camera_FullUpdate_Callee(const float* position, float* out_height);

int ClientInit_SetGlobal(int value);

}

namespace openwow::render {

void* SWModelFadeout_Alloc(void* list, int link_target, int extra_size,
                           uint8_t alloc_flags);

void SWModelFadeout_FreeList(void* list);

}
