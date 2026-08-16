#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace openwow::render {

[[nodiscard]] std::vector<std::uint8_t> NormalizeBgra32Readback(std::uint32_t width,
                                                                std::uint32_t height,
                                                                std::uint32_t pitch,
                                                                std::span<const std::uint8_t> data,
                                                                bool yflip);

[[nodiscard]] std::vector<std::uint8_t> EncodeBgra32Bmp(
    std::uint32_t width, std::uint32_t height, std::uint32_t pitch,
    std::span<const std::uint8_t> data, bool yflip);

[[nodiscard]] bool ReadbackAlphaIsFullyOpaque(std::span<const std::uint8_t> bgra_pixels) noexcept;

std::size_t ApplyAlphaPlaneToBgra32(std::span<std::uint8_t> bgra_pixels,
                                    std::span<const std::uint8_t> alpha_plane) noexcept;

}
