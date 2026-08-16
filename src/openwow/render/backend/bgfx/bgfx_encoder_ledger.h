#pragma once

#include <cstdint>

namespace openwow::render {

[[nodiscard]] std::uint32_t ReserveBgfxFrameEncoders(std::uint32_t requested) noexcept;
void ReleaseBgfxFrameEncoders(std::uint32_t unspent) noexcept;

}
