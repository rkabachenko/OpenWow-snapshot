
#include "warden_pe_loader.h"

namespace openwow::game {

void WardenModule_Unload(WardenModuleImage& img) {
    img.base = nullptr;
    img.size = 0;
    img.import_count = 0;
}

void* WardenModule_GetExport(const WardenModuleImage& img, int ordinal) {
    (void)img; (void)ordinal;
    return nullptr;
}

bool WardenModule_ValidateAndLoad(WardenModuleImage& img,
                                   const void* module_data, int data_size) {
    (void)img; (void)module_data; (void)data_size;

    return false;
}

}
