
#include "openwow/core/dynamic_string.h"

#include <cstring>

#include "openwow/core/storm_string.h"

namespace openwow::core {

DynamicString* DynamicString_Init(DynamicString* ds) {
    ds->m_length   = 0;
    ds->m_capacity = 0;
    ds->m_buffer   = nullptr;
    return ds;
}

void DynamicString_Free(DynamicString* ds) {
    if (ds->m_buffer) {
        SMemFree(ds->m_buffer, ".\\DynamicString.cpp", 72, 0);
    }
}

void DynamicString_Reset(DynamicString* ds) {
    char* const buffer = ds->m_buffer;
    ds->m_length   = 0;
    ds->m_capacity = 0;
    if (buffer) {
        SMemFree(buffer, ".\\DynamicString.cpp", 79, 0);
    }
    ds->m_buffer = nullptr;
}

void DynamicString_Grow(DynamicString* ds) {
    const std::int32_t new_capacity =
        ((3 * ds->m_length) >> 1) + 16;

    ds->m_capacity = new_capacity;

    if (ds->m_buffer) {
        ds->m_buffer = static_cast<char*>(SMemReAlloc(
            ds->m_buffer,
            static_cast<std::size_t>(new_capacity),
            ".\\DynamicString.cpp", 0x13B, 0));
    } else {
        ds->m_buffer = static_cast<char*>(SMemAlloc(
            static_cast<std::size_t>(new_capacity),
            ".\\DynamicString.cpp", 0x136, 0));
        ds->m_buffer[0] = '\0';
    }
}

void DynamicString_Append(DynamicString* ds, const void* src,
                          std::int32_t size) {
    if (!src || size <= 0) {
        return;
    }

    ds->m_length += size;

    if (ds->m_capacity <= ds->m_length) {
        DynamicString_Grow(ds);
    }

    std::memcpy(
        ds->m_buffer + ds->m_length - size,
        src,
        static_cast<std::size_t>(size));

    ds->m_buffer[ds->m_length] = '\0';
}

void DynamicString_AppendChar(DynamicString* ds, char c) {
    if (c) {
        ++ds->m_length;

        if (ds->m_capacity <= ds->m_length) {
            DynamicString_Grow(ds);
        }

        ds->m_buffer[ds->m_length - 1] = c;
        ds->m_buffer[ds->m_length] = '\0';
    }
}

void DynamicString_AppendCString(DynamicString* ds, const char* str) {
    if (str) {
        const std::int32_t old_length = ds->m_length;
        ds->m_length += static_cast<std::int32_t>(SStrLen(str));

        if (ds->m_capacity <= ds->m_length) {
            DynamicString_Grow(ds);
        }

        SStrCopy(ds->m_buffer + old_length, str, 0x7FFFFFFF);
    }
}

void DynamicString_AppendInt(DynamicString* ds, std::int32_t value) {
    if (ds->m_capacity <= ds->m_length + 15) {
        DynamicString_Grow(ds);
    }

    SStrPrintf(ds->m_buffer + ds->m_length, 15, "%d", value);
    ds->m_length = static_cast<std::int32_t>(SStrLen(ds->m_buffer));
}

void DynamicString_Set(DynamicString* ds, const char* str) {
    if (str) {
        const auto len = static_cast<std::int32_t>(SStrLen(str));
        char* const old_buffer = ds->m_buffer;
        ds->m_length = len;

        const auto new_capacity =
            static_cast<std::size_t>(((3 * len) >> 1) + 16);
        ds->m_capacity = static_cast<std::int32_t>(new_capacity);

        ds->m_buffer = static_cast<char*>(SMemReAlloc(
            old_buffer, new_capacity,
            ".\\DynamicString.cpp", 0x70, 0));

        SStrCopy(ds->m_buffer, str, 0x7FFFFFFF);
    } else {
        char* const old_buffer = ds->m_buffer;
        ds->m_length   = 0;
        ds->m_capacity = 0;
        if (old_buffer) {
            SMemFree(old_buffer, ".\\DynamicString.cpp", 106, 0);
        }
        ds->m_buffer = nullptr;
    }
}

void DynamicString_Reserve(DynamicString* ds, std::int32_t size) {
    if (size >= 1) {
        char* const old_buffer = ds->m_buffer;
        if (old_buffer) {
            char* result;
            if (size > ds->m_length) {
                result = static_cast<char*>(SMemReAlloc(
                    old_buffer, static_cast<std::size_t>(size),
                    ".\\DynamicString.cpp", 0x124, 0));
            } else {
                ds->m_length = size - 1;
                old_buffer[size - 1] = '\0';
                result = static_cast<char*>(SMemReAlloc(
                    ds->m_buffer, static_cast<std::size_t>(size),
                    ".\\DynamicString.cpp", 0x11F, 0));
            }
            ds->m_capacity = size;
            ds->m_buffer = result;
        } else {
            auto* result = static_cast<char*>(SMemAlloc(
                static_cast<std::size_t>(size),
                ".\\DynamicString.cpp", 0x12A, 0));
            ds->m_capacity = size;
            ds->m_buffer = result;
            ds->m_length = 0;
            result[0] = '\0';
        }
    } else {
        char* const old_buffer = ds->m_buffer;
        ds->m_length   = 0;
        ds->m_capacity = 0;
        if (old_buffer) {
            SMemFree(old_buffer, ".\\DynamicString.cpp", 278, 0);
        }
        ds->m_buffer = nullptr;
    }
}

}
