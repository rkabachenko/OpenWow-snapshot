#pragma once

#include <cstdint>

namespace openwow::game {

struct WardenModuleImage {
    void* base = nullptr;
    uint32_t size = 0;
    int import_count = 0;
};

void WardenModule_Unload(WardenModuleImage& img);

void* WardenModule_GetExport(const WardenModuleImage& img, int ordinal);

bool WardenModule_ValidateAndLoad(WardenModuleImage& img,
                                   const void* module_data, int data_size);

}
