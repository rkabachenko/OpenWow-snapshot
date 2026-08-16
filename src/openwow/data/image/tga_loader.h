#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace openwow::data {

[[nodiscard]] bool WriteBgraScreenshotTga(
    std::string_view file_path, std::span<const std::uint8_t> bgra_pixels,
    std::uint16_t width, std::uint16_t height);

}
