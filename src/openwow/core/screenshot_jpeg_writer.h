#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace openwow::core {

[[nodiscard]] bool WriteWotlkScreenshotJpeg(
    const std::filesystem::path& path,
    const std::vector<std::uint8_t>& bgra_pixels,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t quality,
    std::string_view comment = {});

namespace detail {

struct ScreenshotJpegEntropyCode {
    std::uint16_t code = 0;
    std::uint16_t size = 0;
};

struct ScreenshotJpegYccSample {
    std::uint8_t y = 0;
    std::uint8_t cb = 0;
    std::uint8_t cr = 0;
};

struct ScreenshotJpegBufferedWriteTrace {
    std::vector<std::size_t> flush_sizes;
    std::vector<std::uint8_t> flushed_bytes;
    std::vector<std::uint8_t> pending_bytes;
    std::size_t free_in_buffer = 0;
    bool ok = false;
};

[[nodiscard]] bool EmitScreenshotJpegEntropyBitsForTests(
    std::vector<std::uint8_t>& output,
    std::uint32_t& bit_buffer,
    int& bit_count,
    ScreenshotJpegEntropyCode bits);

[[nodiscard]] ScreenshotJpegBufferedWriteTrace
TraceScreenshotJpegBufferedByteWritesForTests(
    const std::vector<std::uint8_t>& bytes,
    std::size_t buffer_size);

[[nodiscard]] ScreenshotJpegBufferedWriteTrace
TraceScreenshotJpegBufferedDataWriteForTests(
    const std::vector<std::uint8_t>& bytes,
    std::size_t buffer_size);

[[nodiscard]] std::array<int, 64> BuildScreenshotJpegSlowDivisorsForTests(
    const std::array<std::uint8_t, 64>& natural_quant);

[[nodiscard]] std::array<int, 64> ProcessScreenshotJpegSlowBlockForTests(
    const std::array<int, 64>& level_shifted_samples,
    const std::array<std::uint8_t, 64>& natural_quant);

[[nodiscard]] int QuantizeScreenshotJpegCoefficientForTests(
    int value,
    int divisor);

[[nodiscard]] ScreenshotJpegYccSample ConvertScreenshotJpegRgbToYccForTests(
    std::uint8_t red,
    std::uint8_t green,
    std::uint8_t blue);

}

}
