#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace openwow::core {

inline constexpr std::size_t kWotlkScreenshotWatermarkPayloadSize = 88;

[[nodiscard]] std::array<std::uint8_t, kWotlkScreenshotWatermarkPayloadSize>
BuildWotlkScreenshotWatermarkPayload(std::string_view account_name,
                                    std::string_view realm_address,
                                    std::uint32_t packed_time);

[[nodiscard]] bool ApplyWotlkScreenshotWatermark(
    std::vector<std::uint8_t>& bgra_pixels,
    std::uint32_t width,
    std::uint32_t height,
    const std::uint8_t* payload,
    std::size_t payload_size);

}
