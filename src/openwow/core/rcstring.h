
#pragma once

#include <cstddef>
#include <cstdint>

namespace openwow::core {

constexpr uint32_t kRCStringHashBuckets = 521;
constexpr uint32_t kRCStringPoolSize = 2108;
constexpr uint32_t kRCStringMinBlockSize = 0x10000;
constexpr std::size_t kRCStringBlockManagerOffset = 2084;
constexpr std::size_t kRCStringValueOffset = 8;

void RCString_FreeAll(void* block_manager);

void* RCString_AllocBlock(void* block_manager, uint32_t size);

void* RCString_Lookup(void* pool, const char* str);

void* RCString_PoolInit(void* pool);

void RCString_Set(void* rcstring_this, const char* str);

}
