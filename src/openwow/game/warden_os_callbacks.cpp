
#include "openwow/game/warden_os_callbacks.h"

#include <cstring>
#include <vector>

namespace openwow::game {

uintptr_t Warden_DeobfuscateWideAndDispatch(
    const uint16_t* obfuscated_wide_string,
    uintptr_t (*callback)(const uint16_t*)) {

    if (!obfuscated_wide_string || !callback) {
        return 0;
    }

    size_t len = 0;
    while (obfuscated_wide_string[len] != 0) {
        ++len;
    }

    std::vector<uint16_t> decoded(len + 1);
    for (size_t i = 0; i <= len; ++i) {
        decoded[i] = static_cast<uint16_t>(
            obfuscated_wide_string[i] * kWardenWideDeobfuscationKey);
    }

    return callback(decoded.data());
}

uintptr_t Warden_DeobfuscateWideAndGetModuleHandle(
    const uint16_t* obfuscated_module_name) {

    return Warden_DeobfuscateWideAndDispatch(
        obfuscated_module_name,
        [](const uint16_t* ) -> uintptr_t {

            return 0;
        });
}

void* WardenOSCallback_Alloc(size_t ) {
    return nullptr;
}

void WardenOSCallback_Free(void* ) {

}

void WardenOSCallback_FreeLibrary(uintptr_t ) {

}

uintptr_t WardenOSCallback_GetModuleHandle(
    const uint16_t* obfuscated_module_name) {
    return Warden_DeobfuscateWideAndGetModuleHandle(obfuscated_module_name);
}

void* WardenOSCallback_GetProcAddress(
    uintptr_t ,
    const uint8_t* ) {
    return nullptr;
}

}
