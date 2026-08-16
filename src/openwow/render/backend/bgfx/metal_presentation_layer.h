#pragma once

#include <cstdint>

namespace openwow::render {

void EnsureMetalMaximumDrawableCount(
    void* metal_layer, std::uint8_t requested_count) noexcept;

}
