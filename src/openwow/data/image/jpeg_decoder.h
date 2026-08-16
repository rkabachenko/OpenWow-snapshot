#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace openwow::data {

enum class JpegDecoderError {
    None,
    InvalidComponentCount,
    InvalidScanLayout,
    InvalidScanScript,
    InvalidTableIndex,
    MissingTableDefinition,
    InvalidHuffmanDefinition,
    MalformedStream,
    DecodeFailure,
};

struct JpegHuffmanLookupTable {
    std::array<std::uint32_t, 256> codes{};
    std::array<std::uint8_t, 256> lengths{};
};

struct DecodedJpeg {
    bool ok{false};
    JpegDecoderError error{JpegDecoderError::None};
    int width{0};
    int height{0};
    int channels_in_file{0};
    std::vector<std::uint8_t> pixels_rgba;
};

enum class JpegIdctMethod {
    SlowInteger,
};

struct JpegDecodeSettings {
    JpegIdctMethod idct_method{JpegIdctMethod::SlowInteger};
};

namespace detail {

using JpegFormatMessageCallback = void (*)(char* buffer, void* user_data);

void DiscardJpegFormattedMessage(JpegFormatMessageCallback format_message,
                                 void* user_data);

}

[[nodiscard]] JpegDecoderError BuildJpegDecoderHuffmanLookup(
    std::span<const std::uint8_t> definition,
    bool is_dc_table,
    JpegHuffmanLookupTable& out);

[[nodiscard]] JpegDecodeSettings GetRetailJpegDecodeSettings();

[[nodiscard]] JpegDecoderError ValidateJpegHuffmanTables(
    std::span<const std::uint8_t> jpeg_bytes);

[[nodiscard]] DecodedJpeg DecodeJpeg(
    std::span<const std::uint8_t> jpeg_bytes);

[[nodiscard]] std::string_view ToString(JpegDecoderError error);

}
