#include "openwow/data/image/jpeg_decoder.h"

#include <cstddef>
#include <cstdio>
#include <jerror.h>
#include <jpeglib.h>

#include <algorithm>
#include <array>
#include <csetjmp>
#include <cstring>
#include <vector>

namespace openwow::data {

namespace {

constexpr std::size_t kMaxHuffmanTableIndex = 3;
constexpr std::size_t kHuffmanCodeCountBytes = 16;
constexpr std::size_t kHuffmanSymbolLimit = 256;
constexpr std::size_t kHuffmanDefinitionHeaderSize = 17;

using HuffmanDefinitionMatrix =
    std::array<std::array<std::span<const std::uint8_t>, 4>, 2>;
using HuffmanBuiltMatrix = std::array<std::array<bool, 4>, 2>;
using HuffmanLookupMatrix =
    std::array<std::array<JpegHuffmanLookupTable, 4>, 2>;

struct JpegFrameComponentInfo {
    std::uint8_t id{0};
    std::uint8_t horizontal_sampling{0};
    std::uint8_t vertical_sampling{0};
};

struct JpegFrameInfo {
    bool defined{false};
    bool progressive{false};
    std::uint8_t component_count{0};
    std::array<JpegFrameComponentInfo, 4> components{};
};

struct JpegDecodeErrorManager {
    jpeg_error_mgr pub{};
    std::jmp_buf jump_buffer{};
};

struct JpegMemorySourceManager {
    jpeg_source_mgr pub{};
};

class JpegDecompressHandle {
public:
    jpeg_decompress_struct cinfo{};
    bool created{false};

    ~JpegDecompressHandle() {
        if (created) {
            jpeg_destroy_decompress(&cinfo);
        }
    }
};

void JpegDecodeErrorExit(j_common_ptr cinfo) {
    auto* error_manager =
        reinterpret_cast<JpegDecodeErrorManager*>(cinfo->err);
    std::longjmp(error_manager->jump_buffer, 1);
}

void InvokeJpegFormatMessage(char* const buffer, void* const user_data) {
    auto* common = static_cast<jpeg_common_struct*>(user_data);
    if (common == nullptr || common->err == nullptr
        || common->err->format_message == nullptr) {
        return;
    }

    common->err->format_message(reinterpret_cast<j_common_ptr>(common), buffer);
}

void JpegDecodeOutputMessage(j_common_ptr cinfo) {
    if (cinfo == nullptr || cinfo->err == nullptr
        || cinfo->err->format_message == nullptr) {
        return;
    }

    detail::DiscardJpegFormattedMessage(InvokeJpegFormatMessage, cinfo);
}

void JpegMemorySourceInit(j_decompress_ptr) {}

boolean JpegMemorySourceFill(j_decompress_ptr cinfo) {
    static constexpr JOCTET kEndOfImage[] = {0xFF, JPEG_EOI};
    auto* source = reinterpret_cast<JpegMemorySourceManager*>(cinfo->src);
    source->pub.next_input_byte = kEndOfImage;
    source->pub.bytes_in_buffer = sizeof(kEndOfImage);
    return TRUE;
}

void JpegMemorySourceSkip(j_decompress_ptr cinfo, long byte_count) {
    auto* source = reinterpret_cast<JpegMemorySourceManager*>(cinfo->src);
    if (byte_count <= 0) {
        return;
    }

    const auto available = static_cast<long>(source->pub.bytes_in_buffer);
    if (byte_count >= available) {
        JpegMemorySourceFill(cinfo);
        return;
    }

    source->pub.next_input_byte += byte_count;
    source->pub.bytes_in_buffer -= static_cast<std::size_t>(byte_count);
}

void JpegMemorySourceTerminate(j_decompress_ptr) {}

void AttachJpegMemorySource(j_decompress_ptr cinfo,
                            const std::span<const std::uint8_t> bytes) {
    if (cinfo->src == nullptr) {
        cinfo->src = reinterpret_cast<jpeg_source_mgr*>(
            (*cinfo->mem->alloc_small)(
                reinterpret_cast<j_common_ptr>(cinfo),
                JPOOL_PERMANENT,
                sizeof(JpegMemorySourceManager)));
    }

    auto* source = reinterpret_cast<JpegMemorySourceManager*>(cinfo->src);
    source->pub.init_source = JpegMemorySourceInit;
    source->pub.fill_input_buffer = JpegMemorySourceFill;
    source->pub.skip_input_data = JpegMemorySourceSkip;
    source->pub.resync_to_restart = jpeg_resync_to_restart;
    source->pub.term_source = JpegMemorySourceTerminate;
    source->pub.bytes_in_buffer = bytes.size();
    source->pub.next_input_byte = reinterpret_cast<const JOCTET*>(bytes.data());
}

[[nodiscard]] J_DCT_METHOD ToLibJpegIdctMethod(
    const JpegIdctMethod idct_method) {
    switch (idct_method) {
        case JpegIdctMethod::SlowInteger:
            return JDCT_ISLOW;
    }

    return JDCT_ISLOW;
}

[[nodiscard]] std::uint16_t ReadBigEndianWord(
    const std::span<const std::uint8_t> bytes,
    const std::size_t offset) {
    return static_cast<std::uint16_t>(bytes[offset] << 8)
         | static_cast<std::uint16_t>(bytes[offset + 1]);
}

[[nodiscard]] bool IsStandaloneMarker(const std::uint8_t marker) {
    return marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7);
}

[[nodiscard]] bool IsStartOfFrameMarker(const std::uint8_t marker) {
    switch (marker) {
        case 0xC0:
        case 0xC1:
        case 0xC2:
        case 0xC3:
        case 0xC5:
        case 0xC6:
        case 0xC7:
        case 0xC9:
        case 0xCA:
        case 0xCB:
        case 0xCD:
        case 0xCE:
        case 0xCF:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] bool ConsumeMarker(const std::span<const std::uint8_t> bytes,
                                 std::size_t& cursor,
                                 std::uint8_t& marker) {
    if (cursor >= bytes.size() || bytes[cursor] != 0xFF) {
        return false;
    }

    while (cursor < bytes.size() && bytes[cursor] == 0xFF) {
        ++cursor;
    }

    if (cursor >= bytes.size()) {
        return false;
    }

    marker = bytes[cursor++];
    return marker != 0x00;
}

[[nodiscard]] bool ReadMarkerPayload(
    const std::span<const std::uint8_t> bytes,
    std::size_t& cursor,
    std::span<const std::uint8_t>& payload) {
    if (cursor + 2 > bytes.size()) {
        return false;
    }

    const auto length = ReadBigEndianWord(bytes, cursor);
    cursor += 2;
    if (length < 2) {
        return false;
    }

    const auto payload_size = static_cast<std::size_t>(length - 2);
    if (cursor + payload_size > bytes.size()) {
        return false;
    }

    payload = bytes.subspan(cursor, payload_size);
    cursor += payload_size;
    return true;
}

[[nodiscard]] bool SkipEntropyCodedScanData(
    const std::span<const std::uint8_t> bytes,
    std::size_t& cursor) {
    while (cursor < bytes.size()) {
        if (bytes[cursor] != 0xFF) {
            ++cursor;
            continue;
        }

        const auto marker_prefix = cursor;
        while (cursor < bytes.size() && bytes[cursor] == 0xFF) {
            ++cursor;
        }

        if (cursor >= bytes.size()) {
            return false;
        }

        const auto marker = bytes[cursor];
        if (marker == 0x00 || (marker >= 0xD0 && marker <= 0xD7)) {
            ++cursor;
            continue;
        }

        cursor = marker_prefix;
        return true;
    }

    return false;
}

[[nodiscard]] JpegDecoderError ParseStartOfFramePayload(
    const std::span<const std::uint8_t> payload,
    const bool progressive_frame,
    JpegFrameInfo& frame_info) {
    if (payload.size() < 6) {
        return JpegDecoderError::MalformedStream;
    }

    const auto component_count = payload[5];
    const auto required_size = 6u + static_cast<std::size_t>(component_count) * 3u;
    if (payload.size() < required_size) {
        return JpegDecoderError::MalformedStream;
    }
    if (component_count > frame_info.components.size()) {
        return JpegDecoderError::InvalidComponentCount;
    }

    frame_info = {};
    frame_info.defined = true;
    frame_info.progressive = progressive_frame;
    frame_info.component_count = component_count;

    for (std::size_t component = 0; component < component_count; ++component) {
        const auto component_offset = 6u + component * 3u;
        const auto sampling = payload[component_offset + 1];
        frame_info.components[component] = JpegFrameComponentInfo{
            .id = payload[component_offset],
            .horizontal_sampling = static_cast<std::uint8_t>(sampling >> 4),
            .vertical_sampling = static_cast<std::uint8_t>(sampling & 0x0F),
        };
    }

    return JpegDecoderError::None;
}

[[nodiscard]] const JpegFrameComponentInfo* FindFrameComponent(
    const JpegFrameInfo& frame_info,
    const std::uint8_t component_id) {
    for (std::size_t component = 0; component < frame_info.component_count; ++component) {
        if (frame_info.components[component].id == component_id) {
            return &frame_info.components[component];
        }
    }

    return nullptr;
}

[[nodiscard]] JpegDecoderError ValidateReferencedTable(
    const std::uint8_t table_class,
    const std::uint8_t table_index,
    HuffmanDefinitionMatrix& definitions,
    HuffmanBuiltMatrix& built_tables,
    HuffmanLookupMatrix& lookup_tables) {
    if (table_index > kMaxHuffmanTableIndex) {
        return JpegDecoderError::InvalidTableIndex;
    }

    auto& definition = definitions[table_class][table_index];
    if (definition.empty()) {
        return JpegDecoderError::MissingTableDefinition;
    }

    if (!built_tables[table_class][table_index]) {
        const auto error = BuildJpegDecoderHuffmanLookup(
            definition,
            table_class == 0,
            lookup_tables[table_class][table_index]);
        if (error != JpegDecoderError::None) {
            return error;
        }

        built_tables[table_class][table_index] = true;
    }

    return JpegDecoderError::None;
}

[[nodiscard]] JpegDecoderError ParseDhtPayload(
    const std::span<const std::uint8_t> payload,
    HuffmanDefinitionMatrix& definitions,
    HuffmanBuiltMatrix& built_tables) {
    std::size_t offset = 0;
    while (offset < payload.size()) {
        if (payload.size() - offset < kHuffmanDefinitionHeaderSize) {
            return JpegDecoderError::MalformedStream;
        }

        const auto table_selector = payload[offset];
        const auto table_class = static_cast<std::uint8_t>(table_selector >> 4);
        const auto table_index = static_cast<std::uint8_t>(table_selector & 0x0F);
        if (table_class > 1) {
            return JpegDecoderError::InvalidHuffmanDefinition;
        }

        std::size_t symbol_count = 0;
        for (std::size_t bit_length = 1; bit_length <= kHuffmanCodeCountBytes; ++bit_length) {
            symbol_count += payload[offset + bit_length];
            if (symbol_count > kHuffmanSymbolLimit) {
                return JpegDecoderError::InvalidHuffmanDefinition;
            }
        }

        const auto definition_size = kHuffmanDefinitionHeaderSize + symbol_count;
        if (payload.size() - offset < definition_size) {
            return JpegDecoderError::MalformedStream;
        }

        if (table_index <= kMaxHuffmanTableIndex) {
            definitions[table_class][table_index] =
                payload.subspan(offset, definition_size);
            built_tables[table_class][table_index] = false;
        }

        offset += definition_size;
    }

    return JpegDecoderError::None;
}

[[nodiscard]] JpegDecoderError ValidateSosPayload(
    const std::span<const std::uint8_t> payload,
    const JpegFrameInfo& frame_info,
    HuffmanDefinitionMatrix& definitions,
    HuffmanBuiltMatrix& built_tables,
    HuffmanLookupMatrix& lookup_tables) {
    if (payload.empty()) {
        return JpegDecoderError::MalformedStream;
    }

    const auto component_count = static_cast<std::size_t>(payload[0]);
    const auto expected_size = 1u + (component_count * 2u) + 3u;
    if (payload.size() < expected_size) {
        return JpegDecoderError::MalformedStream;
    }
    if (component_count == 0 || component_count > frame_info.components.size()) {
        return JpegDecoderError::InvalidComponentCount;
    }
    if (!frame_info.defined) {
        return JpegDecoderError::MalformedStream;
    }

    const auto spectral_start = payload[1 + component_count * 2];
    const auto spectral_end = payload[2 + component_count * 2];
    const bool uses_dc_tables = !frame_info.progressive
        || (spectral_start == 0 && spectral_end == 0);
    const bool uses_ac_tables = !frame_info.progressive
        || (spectral_start != 0 || spectral_end != 0);
    std::size_t mcu_block_count = 0;

    for (std::size_t component = 0; component < component_count; ++component) {
        const auto* frame_component =
            FindFrameComponent(frame_info, payload[1 + component * 2]);
        if (frame_component == nullptr) {
            return JpegDecoderError::MalformedStream;
        }

        if (component_count != 1) {
            mcu_block_count += static_cast<std::size_t>(frame_component->horizontal_sampling)
                            * static_cast<std::size_t>(frame_component->vertical_sampling);
            if (mcu_block_count > 10) {
                return JpegDecoderError::InvalidScanLayout;
            }
        }

        const auto table_selectors = payload[1 + component * 2 + 1];
        const auto dc_table_index = static_cast<std::uint8_t>(table_selectors >> 4);
        const auto ac_table_index = static_cast<std::uint8_t>(table_selectors & 0x0F);

        if (uses_dc_tables) {
            const auto error = ValidateReferencedTable(
                0, dc_table_index, definitions, built_tables, lookup_tables);
            if (error != JpegDecoderError::None) {
                return error;
            }
        }

        if (uses_ac_tables) {
            const auto error = ValidateReferencedTable(
                1, ac_table_index, definitions, built_tables, lookup_tables);
            if (error != JpegDecoderError::None) {
                return error;
            }
        }
    }

    return JpegDecoderError::None;
}

}

namespace detail {

void DiscardJpegFormattedMessage(const JpegFormatMessageCallback format_message,
                                 void* const user_data) {
    if (format_message == nullptr) {
        return;
    }

    std::array<char, JMSG_LENGTH_MAX> buffer{};
    format_message(buffer.data(), user_data);
}

}

JpegDecodeSettings GetRetailJpegDecodeSettings() {
    return JpegDecodeSettings{
        .idct_method = JpegIdctMethod::SlowInteger,
    };
}

JpegDecoderError BuildJpegDecoderHuffmanLookup(
    const std::span<const std::uint8_t> definition,
    const bool is_dc_table,
    JpegHuffmanLookupTable& out) {
    if (definition.size() < kHuffmanDefinitionHeaderSize) {
        return JpegDecoderError::MalformedStream;
    }

    std::array<std::uint32_t, kHuffmanSymbolLimit + 1> canonical_codes{};
    std::array<std::uint8_t, kHuffmanSymbolLimit + 4> code_lengths{};

    std::size_t entry_count = 0;
    for (std::size_t bit_length = 1; bit_length <= kHuffmanCodeCountBytes; ++bit_length) {
        const auto count = static_cast<std::size_t>(definition[bit_length]);
        if (entry_count + count > kHuffmanSymbolLimit) {
            return JpegDecoderError::InvalidHuffmanDefinition;
        }

        std::fill_n(code_lengths.begin() + static_cast<std::ptrdiff_t>(entry_count),
                    count,
                    static_cast<std::uint8_t>(bit_length));
        entry_count += count;
    }

    if (definition.size() < kHuffmanDefinitionHeaderSize + entry_count) {
        return JpegDecoderError::MalformedStream;
    }

    code_lengths[entry_count] = 0;

    std::uint32_t code_value = 0;
    std::size_t entry_index = 0;
    auto current_length = static_cast<std::uint32_t>(code_lengths[0]);
    if (current_length != 0) {
        while (true) {
            while (code_lengths[entry_index] == current_length) {
                canonical_codes[entry_index] = code_value;
                ++entry_index;
                ++code_value;
            }

            if (code_value >= (1u << current_length)) {
                return JpegDecoderError::InvalidHuffmanDefinition;
            }

            code_value <<= 1;
            ++current_length;
            if (code_lengths[entry_index] == 0) {
                break;
            }
        }
    }

    out.codes.fill(0);
    out.lengths.fill(0);

    const auto max_symbol = static_cast<std::uint8_t>(is_dc_table ? 15 : 255);
    for (std::size_t entry = 0; entry < entry_count; ++entry) {
        const auto symbol = definition[kHuffmanDefinitionHeaderSize + entry];
        if (symbol > max_symbol || out.lengths[symbol] != 0) {
            return JpegDecoderError::InvalidHuffmanDefinition;
        }

        out.codes[symbol] = canonical_codes[entry];
        out.lengths[symbol] = code_lengths[entry];
    }

    return JpegDecoderError::None;
}

JpegDecoderError ValidateJpegHuffmanTables(
    const std::span<const std::uint8_t> jpeg_bytes) {
    if (jpeg_bytes.size() < 2 || jpeg_bytes[0] != 0xFF || jpeg_bytes[1] != 0xD8) {
        return JpegDecoderError::MalformedStream;
    }

    HuffmanDefinitionMatrix definitions{};
    HuffmanBuiltMatrix built_tables{};
    HuffmanLookupMatrix lookup_tables{};
    JpegFrameInfo frame_info{};
    std::size_t cursor = 0;
    std::uint8_t marker = 0;

    if (!ConsumeMarker(jpeg_bytes, cursor, marker) || marker != 0xD8) {
        return JpegDecoderError::MalformedStream;
    }

    while (cursor < jpeg_bytes.size()) {
        if (!ConsumeMarker(jpeg_bytes, cursor, marker)) {
            return JpegDecoderError::MalformedStream;
        }

        if (marker == 0xD9) {
            return JpegDecoderError::None;
        }

        if (IsStandaloneMarker(marker)) {
            continue;
        }

        std::span<const std::uint8_t> payload;
        if (!ReadMarkerPayload(jpeg_bytes, cursor, payload)) {
            return JpegDecoderError::MalformedStream;
        }

        if (marker == 0xC4) {
            const auto error = ParseDhtPayload(payload, definitions, built_tables);
            if (error != JpegDecoderError::None) {
                return error;
            }
            continue;
        }

        if (IsStartOfFrameMarker(marker)) {
            const auto error =
                ParseStartOfFramePayload(payload, marker == 0xC2, frame_info);
            if (error != JpegDecoderError::None) {
                return error;
            }
            continue;
        }

        if (marker == 0xDA) {
            const auto error = ValidateSosPayload(
                payload, frame_info, definitions, built_tables, lookup_tables);
            if (error != JpegDecoderError::None) {
                return error;
            }

            if (!SkipEntropyCodedScanData(jpeg_bytes, cursor)) {
                return JpegDecoderError::MalformedStream;
            }
        }
    }

    return JpegDecoderError::MalformedStream;
}

DecodedJpeg DecodeJpeg(const std::span<const std::uint8_t> jpeg_bytes) {
    DecodedJpeg result;
    result.error = ValidateJpegHuffmanTables(jpeg_bytes);
    if (result.error != JpegDecoderError::None) {
        return result;
    }

    JpegDecodeErrorManager error_manager;
    JpegDecompressHandle decompress_handle;

    std::vector<JSAMPLE> row_buffer;
    decompress_handle.cinfo.err = jpeg_std_error(&error_manager.pub);
    error_manager.pub.error_exit = JpegDecodeErrorExit;
    error_manager.pub.output_message = JpegDecodeOutputMessage;

    if (setjmp(error_manager.jump_buffer) != 0) {
        result.error = JpegDecoderError::DecodeFailure;
        return result;
    }

    jpeg_create_decompress(&decompress_handle.cinfo);
    decompress_handle.created = true;

    AttachJpegMemorySource(&decompress_handle.cinfo, jpeg_bytes);
    if (jpeg_read_header(&decompress_handle.cinfo, TRUE) != JPEG_HEADER_OK) {
        result.error = JpegDecoderError::DecodeFailure;
        return result;
    }

    const auto decode_settings = GetRetailJpegDecodeSettings();
    decompress_handle.cinfo.dct_method =
        ToLibJpegIdctMethod(decode_settings.idct_method);
    decompress_handle.cinfo.out_color_space = JCS_RGB;
    result.channels_in_file = static_cast<int>(decompress_handle.cinfo.num_components);

    if (jpeg_start_decompress(&decompress_handle.cinfo) == FALSE) {
        result.error = JpegDecoderError::DecodeFailure;
        return result;
    }

    result.width = static_cast<int>(decompress_handle.cinfo.output_width);
    result.height = static_cast<int>(decompress_handle.cinfo.output_height);
    if (result.width <= 0 || result.height <= 0) {
        result.error = JpegDecoderError::DecodeFailure;
        return result;
    }

    const auto row_components =
        static_cast<std::size_t>(decompress_handle.cinfo.out_color_components);
    if (row_components == 0) {
        result.error = JpegDecoderError::DecodeFailure;
        return result;
    }

    const auto width = static_cast<std::size_t>(result.width);
    const auto height = static_cast<std::size_t>(result.height);
    row_buffer.resize(width * row_components);
    result.pixels_rgba.resize(width * height * 4u);

    while (decompress_handle.cinfo.output_scanline
           < decompress_handle.cinfo.output_height) {
        JSAMPROW row_pointer = row_buffer.data();
        if (jpeg_read_scanlines(&decompress_handle.cinfo, &row_pointer, 1) != 1) {
            result.error = JpegDecoderError::DecodeFailure;
            return result;
        }

        const auto row_index =
            static_cast<std::size_t>(decompress_handle.cinfo.output_scanline - 1);
        auto* destination = result.pixels_rgba.data() + row_index * width * 4u;
        for (std::size_t x = 0; x < width; ++x) {
            const auto source_offset = x * row_components;
            const auto destination_offset = x * 4u;
            destination[destination_offset + 0] = row_buffer[source_offset + 0];
            destination[destination_offset + 1] =
                row_components > 1 ? row_buffer[source_offset + 1]
                                   : row_buffer[source_offset + 0];
            destination[destination_offset + 2] =
                row_components > 2 ? row_buffer[source_offset + 2]
                                   : row_buffer[source_offset + 0];
            destination[destination_offset + 3] = 255;
        }
    }

    jpeg_finish_decompress(&decompress_handle.cinfo);
    result.ok = true;
    return result;
}

std::string_view ToString(const JpegDecoderError error) {
    switch (error) {
        case JpegDecoderError::None:
            return "ok";
        case JpegDecoderError::InvalidComponentCount:
            return "invalid JPEG component count";
        case JpegDecoderError::InvalidScanLayout:
            return "invalid JPEG scan layout";
        case JpegDecoderError::InvalidScanScript:
            return "invalid JPEG scan script";
        case JpegDecoderError::InvalidTableIndex:
            return "invalid JPEG Huffman table index";
        case JpegDecoderError::MissingTableDefinition:
            return "missing JPEG Huffman table definition";
        case JpegDecoderError::InvalidHuffmanDefinition:
            return "invalid JPEG Huffman table definition";
        case JpegDecoderError::MalformedStream:
            return "malformed JPEG stream";
        case JpegDecoderError::DecodeFailure:
            return "JPEG pixel decode failed";
    }

    return "unknown JPEG decode error";
}

}
