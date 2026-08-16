
#pragma once

#include <cstdint>
#include <string>

namespace openwow::platform {

int WriteRegistryStringValue(const char* subkey,
                             const char* valueName,
                             uint8_t     flags,
                             const char* data);

int WriteRegistryValue(const char* subkey,
                       const char* valueName,
                       uint8_t     flags,
                       uint32_t    value);

int WriteRegistryBinaryValue(const char* subkey,
                             const char* valueName,
                             uint8_t flags,
                             const void* data,
                             uint32_t size);

int ReadRegistryStringValue(const char* subkey,
                            const char* valueName,
                            uint8_t     flags,
                            char*       buffer,
                            uint32_t    bufferSize);

int ReadRegistryValue(const char* subkey,
                      const char* valueName,
                      uint8_t     flags,
                      uint32_t*   outValue);

int FlushRegistryValues();

namespace detail {

void SetRegistryStorageRootForTests(const char* root);
void ResetRegistryCacheForTests();
std::string RegistryPreferencePathForTests(const char* subkey, uint8_t flags);

}

}
