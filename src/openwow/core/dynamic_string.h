
#pragma once

#include <cstddef>
#include <cstdint>

namespace openwow::core {

struct DynamicString {
    std::int32_t m_length   = 0;
    std::int32_t m_capacity = 0;
    char*        m_buffer   = nullptr;
};

#if INTPTR_MAX == INT32_MAX
static_assert(sizeof(DynamicString) == 12);
static_assert(offsetof(DynamicString, m_length) == 0);
static_assert(offsetof(DynamicString, m_capacity) == 4);
static_assert(offsetof(DynamicString, m_buffer) == 8);
#endif

DynamicString* DynamicString_Init(DynamicString* ds);

void DynamicString_Free(DynamicString* ds);

void DynamicString_Reset(DynamicString* ds);

void DynamicString_Grow(DynamicString* ds);

void DynamicString_Append(DynamicString* ds, const void* src, std::int32_t size);

void DynamicString_AppendChar(DynamicString* ds, char c);

void DynamicString_AppendCString(DynamicString* ds, const char* str);

void DynamicString_AppendInt(DynamicString* ds, std::int32_t value);

void DynamicString_Set(DynamicString* ds, const char* str);

void DynamicString_Reserve(DynamicString* ds, std::int32_t size);

}
