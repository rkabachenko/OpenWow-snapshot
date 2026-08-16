#pragma once

#include <cstddef>
#include <cstdint>

namespace openwow::game {

static constexpr uint16_t kWardenWideDeobfuscationKey = 0xE07B;

static constexpr uint8_t kWardenNarrowDeobfuscationKey = 0x7B;

static constexpr size_t kWardenOSCallbackCount = 5;

enum class WardenOSCallbackIndex : size_t {
    kAlloc            = 0,

    kFree             = 1,

    kFreeLibrary      = 2,

    kGetModuleHandle  = 3,
    kGetProcAddress   = 4,

};

uintptr_t Warden_DeobfuscateWideAndDispatch(
    const uint16_t* obfuscated_wide_string,
    uintptr_t (*callback)(const uint16_t*));

uintptr_t Warden_DeobfuscateWideAndGetModuleHandle(
    const uint16_t* obfuscated_module_name);

void* WardenOSCallback_Alloc(size_t size);

void WardenOSCallback_Free(void* ptr);

void WardenOSCallback_FreeLibrary(uintptr_t module_handle);

uintptr_t WardenOSCallback_GetModuleHandle(
    const uint16_t* obfuscated_module_name);

void* WardenOSCallback_GetProcAddress(
    uintptr_t module_handle,
    const uint8_t* obfuscated_proc_name);

}
