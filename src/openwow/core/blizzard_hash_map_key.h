#pragma once

#include <cstdint>
#include <cstring>

namespace openwow::core {

using BlizzardHashMapFreeFn = void (*)(void* buffer);
extern BlizzardHashMapFreeFn g_blizzardHashMapFree;

struct BlizzardHashMapKey {
    std::uint32_t  hash{0};
    std::uint32_t* refcounted_buffer{nullptr};
    const char*    string_ptr{nullptr};

    void Clear();

    [[nodiscard]] bool IsOccupied() const { return (hash & 0x80000000u) != 0; }

    [[nodiscard]] bool IsEmpty() const { return hash == 0; }
};

[[nodiscard]] inline std::uint32_t BlizzardHashMapKey_ComputeHash(const char* str) {
    if (!str) {
        str = "";
    }
    std::uint32_t h = 0x811C9DC5u;
    while (*str) {
        h = 0x01000193u * (static_cast<std::uint8_t>(*str) ^ h);
        ++str;
    }
    return h | 0x80000000u;
}

}
