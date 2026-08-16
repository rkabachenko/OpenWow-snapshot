
#include "mpq_internals.h"

#include "openwow/core/storm_string.h"
#include "openwow/data/streaming_init.h"
#include "openwow/core/storm_path.h"
#include "openwow/vfs/mpq_hash.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

#include <bzlib.h>
#include <zlib.h>

#if OPENWOW_HAS_STORMLIB && __has_include("StormLib/src/lzma/C/LzmaDec.h") \
    && __has_include("StormLib/src/pklib/pklib.h")
#include "StormLib/src/lzma/C/LzmaDec.h"
#include "StormLib/src/pklib/pklib.h"
#define OPENWOW_HAS_SCOMP_LEGACY_CODECS 1
#else
#define OPENWOW_HAS_SCOMP_LEGACY_CODECS 0
#endif

namespace openwow::core {

namespace {

constexpr int kStormStatusContextSCompDecompress = 4;
constexpr char kSCompUnknownCompressionTypeMessage[] =
    "System_SComp::Decompress - Unrecognized Compression Type";

struct SCompLzmaProperties {
    std::uint8_t lc = 0;
    std::uint8_t lp = 0;
    std::uint8_t pb = 0;
};

[[noreturn]] void AbortInvalidParameter() {
    std::abort();
}

[[nodiscard]] const std::vector<BlockTableEntry>&
GetEmptyBlockTableForHashLookup() {
    static const std::vector<BlockTableEntry> empty_table;
    return empty_table;
}

[[nodiscard]] constexpr std::uint8_t GetHashEntryPlatformByte(
    const openwow::vfs::MPQHashTableEntry& entry) noexcept {
    return static_cast<std::uint8_t>(entry.platform & 0xFFu);
}

[[nodiscard]] constexpr std::uint32_t RotateLeft32(std::uint32_t value,
                                                   int shift) noexcept {
    return (value << shift) | (value >> (32 - shift));
}

[[nodiscard]] constexpr std::uint32_t RotateRight32(std::uint32_t value,
                                                    int shift) noexcept {
    return (value >> shift) | (value << (32 - shift));
}

[[nodiscard]] constexpr std::uint32_t LoadLittleEndian32(
    const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0])
           | (static_cast<std::uint32_t>(bytes[1]) << 8)
           | (static_cast<std::uint32_t>(bytes[2]) << 16)
           | (static_cast<std::uint32_t>(bytes[3]) << 24);
}

[[nodiscard]] constexpr std::uint32_t LoadBigEndian32(
    const std::uint8_t* bytes) noexcept {
    return (static_cast<std::uint32_t>(bytes[0]) << 24)
           | (static_cast<std::uint32_t>(bytes[1]) << 16)
           | (static_cast<std::uint32_t>(bytes[2]) << 8)
           | static_cast<std::uint32_t>(bytes[3]);
}

[[nodiscard]] constexpr bool DecodeSCompLzmaProperties(
    const std::uint8_t packed,
    SCompLzmaProperties& properties) noexcept {
    if (packed >= 0xE1u) {
        return false;
    }

    properties.pb = static_cast<std::uint8_t>(packed / 45u);
    const auto remainder_after_pb =
        static_cast<std::uint8_t>(packed % 45u);
    properties.lp = static_cast<std::uint8_t>(remainder_after_pb / 9u);
    properties.lc = static_cast<std::uint8_t>(remainder_after_pb % 9u);
    return true;
}

[[nodiscard]] constexpr std::uint32_t GetSCompLzmaProbCount(
    const SCompLzmaProperties& properties) noexcept {
    return 1846u + (768u << (properties.lc + properties.lp));
}

[[nodiscard]] bool RangesOverlap(const std::uint8_t* first,
                                 std::uint32_t first_size,
                                 const std::uint8_t* second,
                                 std::uint32_t second_size) noexcept {
    if (first_size == 0 || second_size == 0) {
        return false;
    }

    const auto first_begin = reinterpret_cast<std::uintptr_t>(first);
    const auto second_begin = reinterpret_cast<std::uintptr_t>(second);
    const auto first_end = first_begin + first_size;
    const auto second_end = second_begin + second_size;
    return first_begin < second_end && second_begin < first_end;
}

using SCompCompressionStageFunction =
    void (*)(std::uint8_t* dest, std::uint32_t* dest_size,
             const std::uint8_t* src, std::uint32_t src_size,
             std::uint32_t* status, std::uint8_t quality);

struct SCompCompressionStageDescriptor {
    std::uint8_t header = 0;
    SCompCompressionStageFunction function = nullptr;
};

struct SCompCompressionPlan {
    std::array<SCompCompressionStageDescriptor, 2> stages{};
    std::size_t count = 0;
};

[[nodiscard]] constexpr std::uint8_t ComposeSCompHeader(
    const std::array<SCompCompressionStageDescriptor, 2>& stages,
    const std::size_t count) noexcept {
    if (count == 0) {
        return 0;
    }
    if (count == 1) {
        return stages[0].header;
    }
    if (stages[0].header == 0x20u && stages[1].header == 0x02u) {
        return 0x22u;
    }
    if (stages[0].header == 0x20u && stages[1].header == 0x10u) {
        return 0x30u;
    }
    return 0;
}

[[nodiscard]] constexpr bool SCompNeedsChecksumFlag(
    const std::uint8_t header) noexcept {
    return header != 0x02u && header != 0x10u && header != 0x12u;
}

[[nodiscard]] constexpr int SelectSCompZlibLevel(
    const std::uint8_t quality) noexcept {
    if (quality == 1u) {
        return 9;
    }
    if (quality == 2u) {
        return 1;
    }
    return Z_DEFAULT_COMPRESSION;
}

[[nodiscard]] constexpr int SelectSCompZlibWindowBits(
    const std::uint32_t size) noexcept {
    if (size <= 0x100u) {
        return 8;
    }
    if (size <= 0x200u) {
        return 9;
    }
    if (size <= 0x400u) {
        return 10;
    }
    if (size <= 0x800u) {
        return 11;
    }
    if (size <= 0x1000u) {
        return 12;
    }
    if (size <= 0x2000u) {
        return 13;
    }
    if (size <= 0x4000u) {
        return 14;
    }
    return 15;
}

#if OPENWOW_HAS_SCOMP_LEGACY_CODECS
struct PklibBufferCursor {
    const char* input = nullptr;
    const char* input_end = nullptr;
    char* output = nullptr;
    char* output_end = nullptr;
    bool output_overflowed = false;
};

unsigned int ReadPklibInputForSComp(char* buffer,
                                    unsigned int* size,
                                    void* param);

void WritePklibOutputForSComp(char* buffer,
                              unsigned int* size,
                              void* param);
#endif

void SCompCompressStageZlib(std::uint8_t* dest, std::uint32_t* dest_size,
                            const std::uint8_t* src,
                            const std::uint32_t src_size,
                            std::uint32_t* status,
                            const std::uint8_t quality) {
    if (status != nullptr) {
        *status = 0;
    }

    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(src);
    stream.avail_in = src_size;
    stream.next_out = dest;
    stream.avail_out = *dest_size;

    const int init_result = deflateInit2(
        &stream, SelectSCompZlibLevel(quality), Z_DEFLATED,
        SelectSCompZlibWindowBits(src_size), 8, Z_DEFAULT_STRATEGY);
    if (init_result != Z_OK) {
        return;
    }

    const int deflate_result = deflate(&stream, Z_FINISH);
    const int end_result = deflateEnd(&stream);
    if (deflate_result == Z_STREAM_END && end_result == Z_OK) {
        *dest_size = static_cast<std::uint32_t>(stream.total_out);
    }
}

void SCompCompressStageIdentity(std::uint8_t*,
                                std::uint32_t* dest_size,
                                const std::uint8_t*,
                                const std::uint32_t src_size,
                                std::uint32_t* status,
                                const std::uint8_t) {

    *dest_size = src_size;
    if (status != nullptr) {
        *status = 0;
    }
}

void SCompCompressStageBzip2(std::uint8_t* dest, std::uint32_t* dest_size,
                             const std::uint8_t* src,
                             const std::uint32_t src_size,
                             std::uint32_t* status,
                             const std::uint8_t) {
    unsigned int requested_size = *dest_size;
    const int result = BZ2_bzBuffToBuffCompress(
        reinterpret_cast<char*>(dest), &requested_size,
        const_cast<char*>(reinterpret_cast<const char*>(src)),
        src_size, 9, 0, 0);

    if (result == BZ_OK) {
        *dest_size = requested_size;
    } else {
        *dest_size = src_size;
    }

    if (status != nullptr) {
        *status = 0;
    }
}

[[nodiscard]] bool SCompWriteSparseLiteralChunk(
    const std::uint8_t*& input,
    std::uint32_t literal_length,
    std::uint8_t*& output,
    const std::uint8_t* const shrink_threshold_end) {

    while (literal_length > 0x81u) {
        if (output + 129 >= shrink_threshold_end) {
            return false;
        }

        *output++ = 0xFFu;
        std::memcpy(output, input, 128);
        input += 128;
        output += 128;
        literal_length -= 128;
    }

    if (literal_length > 0x80u) {
        if (output + 2 >= shrink_threshold_end) {
            return false;
        }

        *output++ = 0x80u;
        *output++ = *input++;
        --literal_length;
    }

    if (literal_length != 0u) {
        if (output + literal_length + 1 >= shrink_threshold_end) {
            return false;
        }

        *output++ = static_cast<std::uint8_t>(0x80u | (literal_length - 1u));
        std::memcpy(output, input, literal_length);
        output += literal_length;
        input += literal_length;
    }

    return true;
}

[[nodiscard]] bool SCompWriteSparseZeroChunk(
    std::uint32_t& zero_run_length,
    std::uint8_t*& output,
    const std::uint8_t* const shrink_threshold_end) {
    while (zero_run_length > 0x85u) {
        if (output + 1 >= shrink_threshold_end) {
            return false;
        }

        *output++ = 0x7Fu;
        zero_run_length -= 130;
    }

    if (zero_run_length > 0x82u) {
        if (output + 1 >= shrink_threshold_end) {
            return false;
        }

        *output++ = 0x00u;
        zero_run_length -= 3;
    }

    if (zero_run_length >= 3u) {
        if (output + 1 >= shrink_threshold_end) {
            return false;
        }

        *output++ = static_cast<std::uint8_t>(zero_run_length - 3u);
    }

    return true;
}

void SCompCompressStageSparse(std::uint8_t* dest, std::uint32_t* dest_size,
                              const std::uint8_t* src,
                              const std::uint32_t src_size,
                              std::uint32_t* status,
                              const std::uint8_t) {
    const std::uint32_t capacity = *dest_size;
    if (status != nullptr) {
        *status = 0;
    }

    if (capacity <= 4u) {
        return;
    }

    dest[0] = static_cast<std::uint8_t>((src_size >> 24) & 0xFFu);
    dest[1] = static_cast<std::uint8_t>((src_size >> 16) & 0xFFu);
    dest[2] = static_cast<std::uint8_t>((src_size >> 8) & 0xFFu);
    dest[3] = static_cast<std::uint8_t>(src_size & 0xFFu);

    const auto* input_begin = src;
    const auto* input_end = src + src_size;
    auto* output = dest + 4;
    const auto* shrink_threshold_end = dest + capacity;
    auto* current_input = input_begin;

    const auto* main_loop_end =
        (src_size >= 3u) ? input_end - 3 : current_input;
    while (current_input < main_loop_end) {
        const auto* literal_input = current_input;
        const auto* literal_end = current_input;
        std::uint32_t zero_run_length = 0;
        std::uint32_t trailing_zero_run = 0;

        const auto* scan = current_input;
        for (; scan < input_end; ++scan) {
            if (*scan != 0u) {
                if (zero_run_length >= 3u) {
                    break;
                }

                trailing_zero_run = 0;
                zero_run_length = 0;
                literal_end = scan + 1;
            } else {
                trailing_zero_run = ++zero_run_length;
            }
        }

        const auto literal_length = static_cast<std::uint32_t>(
            literal_end - literal_input);
        if (literal_length != 0u
            && !SCompWriteSparseLiteralChunk(
                current_input, literal_length, output,
                shrink_threshold_end)) {
            return;
        }

        zero_run_length = trailing_zero_run;
        if (!SCompWriteSparseZeroChunk(
                zero_run_length, output, shrink_threshold_end)) {
            return;
        }
        if (trailing_zero_run >= 3u) {
            current_input += trailing_zero_run;
        }
    }

    if (current_input < input_end) {
        const auto tail_length =
            static_cast<std::uint32_t>(input_end - current_input);
        const bool tail_has_non_zero = std::find_if(
            current_input, input_end,
            [](const std::uint8_t byte) { return byte != 0u; }) != input_end;

        if (output + 1 >= shrink_threshold_end) {
            return;
        }

        if (!tail_has_non_zero) {
            *output++ = 0x7Fu;
        } else {
            if (output + 1 + tail_length >= shrink_threshold_end) {
                return;
            }

            *output++ = 0xFFu;
            std::memcpy(output, current_input, tail_length);
            output += tail_length;
        }
    }

    *dest_size = static_cast<std::uint32_t>(output - dest);
}

[[nodiscard]] SCompCompressionPlan BuildSCompCompressionPlan(
    const std::uint8_t type) noexcept {
    SCompCompressionPlan plan{};

    switch (type) {
    case 0x02u:
        plan.stages[0] = {0x02u, &SCompCompressStageZlib};
        plan.count = 1;
        break;
    case 0x08u:
        plan.stages[0] = {0x08u, &SCompCompressStageIdentity};
        plan.count = 1;
        break;
    case 0x10u:
        plan.stages[0] = {0x10u, &SCompCompressStageBzip2};
        plan.count = 1;
        break;
    case 0x12u:
        break;
    case 0x20u:
        plan.stages[0] = {0x20u, &SCompCompressStageSparse};
        plan.count = 1;
        break;
    case 0x22u:
        plan.stages[0] = {0x20u, &SCompCompressStageSparse};
        plan.stages[1] = {0x02u, &SCompCompressStageZlib};
        plan.count = 2;
        break;
    case 0x30u:
        plan.stages[0] = {0x20u, &SCompCompressStageSparse};
        plan.stages[1] = {0x10u, &SCompCompressStageBzip2};
        plan.count = 2;
        break;
    default:
        break;
    }

    return plan;
}

[[nodiscard]] std::size_t AcquireScratchSlice(
    const std::size_t scratch_count,
    const std::array<bool, 3>& unavailable) noexcept {
    for (std::size_t index = 0; index < scratch_count; ++index) {
        if (!unavailable[index]) {
            return index;
        }
    }
    return scratch_count;
}

#if OPENWOW_HAS_SCOMP_LEGACY_CODECS
unsigned int ReadPklibInputForSComp(char* buffer,
                                    unsigned int* size,
                                    void* param) {
    auto* cursor = static_cast<PklibBufferCursor*>(param);
    const auto available =
        static_cast<unsigned int>(cursor->input_end - cursor->input);
    const auto to_read = std::min(*size, available);
    std::memcpy(buffer, cursor->input, to_read);
    cursor->input += to_read;
    return to_read;
}

void WritePklibOutputForSComp(char* buffer,
                              unsigned int* size,
                              void* param) {
    auto* cursor = static_cast<PklibBufferCursor*>(param);
    const auto available =
        static_cast<unsigned int>(cursor->output_end - cursor->output);
    const auto to_write = std::min(*size, available);
    cursor->output_overflowed |= (to_write != *size);
    std::memcpy(cursor->output, buffer, to_write);
    cursor->output += to_write;
}

void* LzmaAllocForSComp(void*, size_t size) {
    return std::malloc(size);
}

void LzmaFreeForSComp(void*, void* address) {
    std::free(address);
}
#endif

bool SCompDecompressPklib(void* dest, std::uint32_t* dest_size,
                          const void* src, std::uint32_t src_size) {
    if (dest == nullptr || dest_size == nullptr || src == nullptr) {
        return false;
    }

#if !OPENWOW_HAS_SCOMP_LEGACY_CODECS
    (void)dest;
    (void)dest_size;
    (void)src;
    (void)src_size;
    return false;
#else
    auto work_buffer = std::vector<char>(EXP_BUFFER_SIZE, 0);
    PklibBufferCursor cursor{
        .input = static_cast<const char*>(src),
        .input_end = static_cast<const char*>(src) + src_size,
        .output = static_cast<char*>(dest),
        .output_end = static_cast<char*>(dest) + *dest_size,
    };

    const auto explode_result = explode(
        &ReadPklibInputForSComp, &WritePklibOutputForSComp,
        work_buffer.data(), &cursor);
    if (explode_result != CMP_NO_ERROR || cursor.output_overflowed) {
        return false;
    }

    *dest_size = static_cast<std::uint32_t>(
        cursor.output - static_cast<char*>(dest));
    return true;
#endif
}

bool SCompDecompressBzip2(void* dest, std::uint32_t* dest_size,
                          const void* src, std::uint32_t src_size) {
    if (dest == nullptr || dest_size == nullptr || src == nullptr) {
        return false;
    }

    unsigned int requested_size = *dest_size;
    const int result = BZ2_bzBuffToBuffDecompress(
        static_cast<char*>(dest), &requested_size,
        const_cast<char*>(static_cast<const char*>(src)),
        src_size, 0, 0);
    if (result != BZ_OK) {
        return false;
    }

    *dest_size = requested_size;
    return true;
}

bool SCompDecompressSparse(void* dest, std::uint32_t* dest_size,
                           const void* src, std::uint32_t src_size) {
    if (dest == nullptr || dest_size == nullptr || src == nullptr
        || src_size < 5u) {
        return false;
    }

    const auto* src_bytes = static_cast<const std::uint8_t*>(src);
    const std::uint32_t output_size = LoadBigEndian32(src_bytes);
    if (output_size > *dest_size) {
        return false;
    }

    auto remaining = output_size;
    auto* output = static_cast<std::uint8_t*>(dest);
    const auto* cursor = src_bytes + 4;
    const auto* end = src_bytes + src_size;

    while (cursor < end) {
        const std::uint8_t control = *cursor++;
        const std::uint32_t chunk_length = std::min<std::uint32_t>(
            remaining,
            (control & 0x80u) != 0u ? (control & 0x7Fu) + 1u
                                    : (control & 0x7Fu) + 3u);
        if ((control & 0x80u) != 0u) {
            if (static_cast<std::size_t>(end - cursor) < chunk_length) {
                return false;
            }

            std::memcpy(output, cursor, chunk_length);
            cursor += chunk_length;
        } else {
            std::memset(output, 0, chunk_length);
        }

        output += chunk_length;
        remaining -= chunk_length;
    }

    if (remaining != 0u) {
        return false;
    }

    *dest_size = output_size;
    return true;
}

}

bool SCompDecompressLzma(void* dest, std::uint32_t* dest_size,
                         const void* src, std::uint32_t src_size) {
    if (dest == nullptr || dest_size == nullptr || src == nullptr) {
        return false;
    }

    constexpr std::uint32_t kLzmaHeaderSize = 14u;
    if (src_size < kLzmaHeaderSize) {
        return false;
    }

    const auto output_capacity = *dest_size;
    *dest_size = 0u;

#if !OPENWOW_HAS_SCOMP_LEGACY_CODECS
    (void)dest;
    (void)dest_size;
    (void)src;
    (void)src_size;
    return false;
#else
    constexpr std::uint32_t kLzmaRangeInitSize = 5u;
    const auto* src_bytes = static_cast<const Byte*>(src);
    if (src_size < kLzmaHeaderSize + kLzmaRangeInitSize
        || src_bytes[0] != 0u) {
        return false;
    }

    SCompLzmaProperties properties{};
    if (!DecodeSCompLzmaProperties(src_bytes[1], properties)) {
        return false;
    }

    ISzAlloc allocator{
        .Alloc = &LzmaAllocForSComp,
        .Free = &LzmaFreeForSComp,
    };
    CLzmaDec decoder;
    LzmaDec_Construct(&decoder);

    const auto prob_count = GetSCompLzmaProbCount(properties);
    decoder.probs = static_cast<CLzmaProb*>(allocator.Alloc(
        &allocator, static_cast<size_t>(prob_count) * sizeof(CLzmaProb)));
    if (decoder.probs == nullptr) {
        return false;
    }

    decoder.numProbs = prob_count;
    decoder.prop.lc = properties.lc;
    decoder.prop.lp = properties.lp;
    decoder.prop.pb = properties.pb;
    decoder.prop.dicSize = output_capacity;
    decoder.dic = static_cast<Byte*>(dest);
    decoder.dicBufSize = output_capacity;
    LzmaDec_Init(&decoder);

    decoder.needFlush = 0;
    decoder.tempBufSize = 0;
    decoder.range = 0xFFFFFFFFu;
    decoder.code = 0u;
    for (std::uint32_t index = 0; index < kLzmaRangeInitSize; ++index) {
        decoder.code = static_cast<UInt32>(
            src_bytes[kLzmaHeaderSize + index]) | (decoder.code << 8);
    }

    const Byte* compressed_data =
        src_bytes + kLzmaHeaderSize + kLzmaRangeInitSize;
    SizeT compressed_size =
        src_size - kLzmaHeaderSize - kLzmaRangeInitSize;
    ELzmaStatus status = LZMA_STATUS_NOT_SPECIFIED;
    const SRes result = LzmaDec_DecodeToDic(
        &decoder, decoder.dicBufSize, compressed_data, &compressed_size,
        LZMA_FINISH_ANY, &status);

    const bool success =
        result == SZ_OK && status != LZMA_STATUS_NEEDS_MORE_INPUT;
    if (success) {
        *dest_size = static_cast<std::uint32_t>(decoder.dicPos);
    }

    LzmaDec_FreeProbs(&decoder, &allocator);
    return success;
#endif
}

namespace {

void MixHashLittle2(std::uint32_t& a, std::uint32_t& b,
                    std::uint32_t& c) noexcept {
    a -= c;
    a ^= RotateLeft32(c, 4);
    c += b;
    b -= a;
    b ^= RotateLeft32(a, 6);
    a += c;
    c -= b;
    c ^= RotateLeft32(b, 8);
    b += a;
    a -= c;
    a ^= RotateLeft32(c, 16);
    c += b;
    b -= a;
    b ^= RotateRight32(a, 13);
    a += c;
    c -= b;
    c ^= RotateLeft32(b, 4);
    b += a;
}

void FinalizeHashLittle2(std::uint32_t& a, std::uint32_t& b,
                         std::uint32_t& c) noexcept {
    c ^= b;
    c -= RotateLeft32(b, 14);
    a ^= c;
    a -= RotateLeft32(c, 11);
    b ^= a;
    b -= RotateRight32(a, 7);
    c ^= b;
    c -= RotateLeft32(b, 16);
    a ^= c;
    a -= RotateLeft32(c, 4);
    b ^= a;
    b -= RotateLeft32(a, 14);
    c ^= b;
    c -= RotateRight32(b, 8);
}

}

JenkinsHashLittle2Result JenkinsHashLittle2(
    const std::span<const std::uint8_t> bytes,
    const std::uint32_t first_seed,
    const std::uint32_t second_seed) noexcept {
    const std::uint32_t length = static_cast<std::uint32_t>(bytes.size());
    std::uint32_t a = 0xDEADBEEFu + length + first_seed;
    std::uint32_t b = a;
    std::uint32_t c = a + second_seed;
    std::uint32_t offset = 0;
    std::uint32_t remaining = length;

    while (remaining > 12) {
        a += LoadLittleEndian32(bytes.data() + offset);
        b += LoadLittleEndian32(bytes.data() + offset + 4);
        c += LoadLittleEndian32(bytes.data() + offset + 8);
        MixHashLittle2(a, b, c);
        offset += 12;
        remaining -= 12;
    }

    switch (remaining) {
    case 12:
        c += static_cast<std::uint32_t>(bytes[offset + 11]) << 24;
        [[fallthrough]];
    case 11:
        c += static_cast<std::uint32_t>(bytes[offset + 10]) << 16;
        [[fallthrough]];
    case 10:
        c += static_cast<std::uint32_t>(bytes[offset + 9]) << 8;
        [[fallthrough]];
    case 9:
        c += bytes[offset + 8];
        [[fallthrough]];
    case 8:
        b += static_cast<std::uint32_t>(bytes[offset + 7]) << 24;
        [[fallthrough]];
    case 7:
        b += static_cast<std::uint32_t>(bytes[offset + 6]) << 16;
        [[fallthrough]];
    case 6:
        b += static_cast<std::uint32_t>(bytes[offset + 5]) << 8;
        [[fallthrough]];
    case 5:
        b += bytes[offset + 4];
        [[fallthrough]];
    case 4:
        a += static_cast<std::uint32_t>(bytes[offset + 3]) << 24;
        [[fallthrough]];
    case 3:
        a += static_cast<std::uint32_t>(bytes[offset + 2]) << 16;
        [[fallthrough]];
    case 2:
        a += static_cast<std::uint32_t>(bytes[offset + 1]) << 8;
        [[fallthrough]];
    case 1:
        a += bytes[offset];
        break;
    case 0:
        return {c, b};
    }

    FinalizeHashLittle2(a, b, c);
    return {c, b};
}

namespace {

[[nodiscard]] std::uint32_t NormalizeMpqPathForHashPair(
    const char* path, std::array<std::uint8_t, 260>& normalized) noexcept {
    std::uint32_t length = 0;

    if (path != nullptr) {
        while (*path != '\0' && length < 259) {
            std::uint8_t ch = static_cast<std::uint8_t>(*path++);
            if (ch >= 'A' && ch <= 'Z') {
                ch = static_cast<std::uint8_t>(ch + ('a' - 'A'));
            } else if (ch == '/') {
                ch = '\\';
            }
            normalized[length++] = ch;
        }
    }

    normalized[length] = 0;
    return length;
}

}

uint32_t MPQ_HashString(const uint8_t* str, uint32_t hash_type) {

    return openwow::vfs::MPQ_HashString(
        reinterpret_cast<const char*>(str),
        static_cast<int>(hash_type));
}

uint64_t ComputeFileHashPair(const char* path) {
    std::array<std::uint8_t, 260> normalized{};
    const std::uint32_t length = NormalizeMpqPathForHashPair(path, normalized);
    const JenkinsHashLittle2Result hash_pair = JenkinsHashLittle2(
        std::span<const std::uint8_t>(normalized.data(), length), 2u, 1u);
    return (static_cast<std::uint64_t>(hash_pair.second) << 32)
           | hash_pair.first;
}

namespace {

constexpr std::array<std::uint8_t, 16> kInvalidAttributeMd5Digest{
    0xFFu, 0xFFu, 0xFFu, 0xFFu,
    0xFFu, 0xFFu, 0xFFu, 0xFFu,
    0xFFu, 0xFFu, 0xFFu, 0xFFu,
    0xFFu, 0xFFu, 0xFFu, 0xFFu,
};

}

bool BuffersDiffer16(const std::uint8_t* left,
                     const std::uint8_t* right) {
    return std::memcmp(left, right, 16) != 0;
}

bool BuffersEqual16(const std::uint8_t* left,
                    const std::uint8_t* right) {
    return !BuffersDiffer16(left, right);
}

bool IsInvalidAttributeMd5Digest(const std::uint8_t* digest16) {
    return BuffersEqual16(digest16, kInvalidAttributeMd5Digest.data());
}

bool SFileFreeBlock(void* block) {
    SMemFree(block, ".\\SFile.cpp", 565, 0);
    return true;
}

void* AllocSFileHandle(uint32_t size, uint8_t flags,
                       const char* source_file, int source_line) {
    return SMemAlloc(size, source_file, source_line, (flags & 1u) != 0u ? 0x8 : 0);
}

char* NormalizePath(const char* input, char* output, uint32_t output_size) {
    openwow::core::NormalizePathToBackslashes(
        input, output, static_cast<int>(output_size));
    return output;
}

void UpdateBlockTableEntry(std::vector<BlockTableEntry>& table,
                           uint32_t index, const BlockTableEntry& entry) {

    if (index >= table.size()) {
        AbortInvalidParameter();
    }
    table[index] = entry;
}

namespace {

void ReadBitPackedField(const BitPackedBufferView& buffer,
                        const std::uint32_t bit_offset,
                        const std::uint32_t bit_count,
                        void* const dest) {
    auto* const dest_bytes = static_cast<std::uint8_t*>(dest);
    const auto source_byte_index = bit_offset >> 3;
    const auto source_shift = bit_offset & 7u;
    const auto whole_byte_count = bit_count >> 3;

    std::uint32_t read_byte_index = source_byte_index;
    std::uint32_t next_byte_index = source_byte_index + 1u;
    for (std::uint32_t index = 0; index < whole_byte_count; ++index) {
        std::uint8_t value = buffer.data[read_byte_index];
        if (source_shift != 0u) {
            value = static_cast<std::uint8_t>(
                (buffer.data[read_byte_index] >> source_shift)
                | (buffer.data[next_byte_index] << (8u - source_shift)));
        }

        dest_bytes[index] = value;
        ++read_byte_index;
        ++next_byte_index;
    }

    const auto trailing_bit_count = bit_count & 7u;
    if (trailing_bit_count == 0u) {
        return;
    }

    std::uint8_t trailing_value =
        static_cast<std::uint8_t>(buffer.data[read_byte_index] >> source_shift);
    const auto carry_bit_count = 8u - source_shift;
    if (carry_bit_count < trailing_bit_count) {
        trailing_value = static_cast<std::uint8_t>(
            trailing_value
            | (buffer.data[next_byte_index] << carry_bit_count));
    }

    dest_bytes[whole_byte_count] = static_cast<std::uint8_t>(
        trailing_value & ((1u << trailing_bit_count) - 1u));
}

template <typename Value>
bool ReadPackedValue(const PackedBitValueReader& reader,
                     const std::uint32_t entry_index,
                     Value* const out_value) {
    if (out_value == nullptr) {
        AbortInvalidParameter();
    }
    if (reader.packed_bits.data == nullptr) {
        return false;
    }

    *out_value = 0;
    ReadBitPackedField(reader.packed_bits,
                       entry_index * reader.entry_stride_bits,
                       reader.value_bit_count, out_value);
    return true;
}

}

bool BETTable_ReadBlockTableEntry(const BETBlockTableReader& reader,
                                  const std::uint32_t entry_index,
                                  BlockTableEntry* const out_entry) {
    if (out_entry == nullptr) {
        AbortInvalidParameter();
    }
    if (reader.packed_fields.data == nullptr) {
        return false;
    }

    std::memset(out_entry, 0, sizeof(*out_entry));

    const std::uint32_t entry_bit_base = entry_index * reader.entry_stride_bits;
    ReadBitPackedField(reader.packed_fields,
                       entry_bit_base + reader.file_offset_bit_offset,
                       reader.file_offset_bit_count,
                       &out_entry->file_offset_low);
    ReadBitPackedField(reader.packed_fields,
                       entry_bit_base + reader.file_size_bit_offset,
                       reader.file_size_bit_count,
                       &out_entry->file_size_lo);
    ReadBitPackedField(reader.packed_fields,
                       entry_bit_base + reader.compressed_size_bit_offset,
                       reader.compressed_size_bit_count,
                       &out_entry->compressed_size_lo);

    const auto flag_value_count =
        openwow::vfs::GetUint32VectorCount(&reader.flags_by_index);
    if (reader.flags_by_index.begin == nullptr || flag_value_count == 0u) {
        return false;
    }

    if (flag_value_count == 1u) {
        out_entry->flags =
            *openwow::vfs::GetUint32VectorEntryAt(&reader.flags_by_index, 0u);
    } else {
        ReadBitPackedField(reader.packed_fields,
                           entry_bit_base + reader.flags_index_bit_offset,
                           reader.flags_index_bit_count,
                           &out_entry->flags);
        if (out_entry->flags >= flag_value_count) {
            return false;
        }

        out_entry->flags =
            *openwow::vfs::GetUint32VectorEntryAt(&reader.flags_by_index,
                                                  out_entry->flags);
    }

    if ((reader.reader_flags & 0x200u) != 0u) {
        std::uint8_t raw_lookup_flag = 0;
        ReadBitPackedField(reader.packed_fields,
                           entry_bit_base + reader.lookup_flag_bit_offset,
                           reader.lookup_flag_bit_count,
                           &raw_lookup_flag);
        out_entry->attribute_lookup_flag =
            static_cast<std::uint8_t>(raw_lookup_flag != 0u);
    }

    return true;
}

template <typename Value>
const Value& GetArchiveBlockTableOverlayValueOrAbort(
    const std::span<const Value> values, const std::uint32_t index) {
    if (index >= values.size()) {
        AbortInvalidParameter();
    }
    return values[index];
}

BlockTableEntry* GetBlockTableEntry(
    const std::vector<BlockTableEntry>& block_table,
    const ArchiveBlockTableSource& source, BlockTableEntry* const out_entry,
    const std::uint32_t index) {
    if (out_entry == nullptr) {
        AbortInvalidParameter();
    }

    if (source.bet_reader == nullptr) {
        if (index >= block_table.size()) {
            AbortInvalidParameter();
        }

        std::memcpy(out_entry, &block_table[index], sizeof(*out_entry));
        return out_entry;
    }

    BlockTableEntry synthesized_entry{};
    (void)BETTable_ReadBlockTableEntry(*source.bet_reader, index,
                                       &synthesized_entry);

    if ((source.archive_flags & 0x80u) != 0u) {
        synthesized_entry.attribute_dword =
            GetArchiveBlockTableOverlayValueOrAbort(source.attribute_dwords,
                                                    index);
    }

    if ((source.archive_flags & 0x40u) != 0u) {
        const auto& digest =
            GetArchiveBlockTableOverlayValueOrAbort(source.attribute_md5_digests,
                                                    index);
        std::memcpy(synthesized_entry.attribute_md5, digest.data(),
                    sizeof(synthesized_entry.attribute_md5));
    }

    if ((source.archive_flags & 0x100u) != 0u) {
        synthesized_entry.attribute_qword =
            GetArchiveBlockTableOverlayValueOrAbort(source.attribute_qwords,
                                                    index);
    }

    std::memcpy(out_entry, &synthesized_entry, sizeof(*out_entry));
    return out_entry;
}

void ReadArchiveTablesExtendedBlockWordBuffer::Reset(
    const std::uint32_t entry_count) {
    entry_count_ = entry_count;
    words_.clear();
    if (entry_count == 0) {
        return;
    }

    words_.resize(entry_count);
}

namespace {

BlockTableEntry& GetReadArchiveTablesEntry(std::vector<BlockTableEntry>& table,
                                           std::uint32_t index) {
    if (index >= table.size()) {
        AbortInvalidParameter();
    }
    return table[index];
}

}

void ReadArchiveTables_SetAttributeDword(std::vector<BlockTableEntry>& table,
                                         std::uint32_t index,
                                         std::uint32_t value) {
    GetReadArchiveTablesEntry(table, index).attribute_dword = value;
}

void ReadArchiveTables_SetAttributeQword(std::vector<BlockTableEntry>& table,
                                         std::uint32_t index,
                                         std::uint32_t low,
                                         std::uint32_t high) {
    auto& entry = GetReadArchiveTablesEntry(table, index);
    entry.attribute_qword = static_cast<std::uint64_t>(low)
                            | (static_cast<std::uint64_t>(high) << 32);
}

void ReadArchiveTables_SetAttributeMd5Digest(std::vector<BlockTableEntry>& table,
                                             std::uint32_t index,
                                             const std::uint8_t* digest16) {
    if (digest16 == nullptr) {
        AbortInvalidParameter();
    }
    auto& entry = GetReadArchiveTablesEntry(table, index);
    std::memcpy(entry.attribute_md5, digest16, sizeof(entry.attribute_md5));
}

void ReadArchiveTables_SetAttributeLookupFlag(std::vector<BlockTableEntry>& table,
                                              std::uint32_t index,
                                              std::uint8_t value) {
    GetReadArchiveTablesEntry(table, index).attribute_lookup_flag = value;
}

bool ReadArchiveTables_ApplyAttributes(
    const std::uint8_t* bytes, std::size_t size,
    std::vector<BlockTableEntry>& table,
    ReadArchiveTablesAttributesHeader* out_header) {
    if (bytes == nullptr || size < 8) {
        return false;
    }

    ReadArchiveTablesAttributesHeader header{
        .version = LoadLittleEndian32(bytes),
        .flags = LoadLittleEndian32(bytes + 4),
    };
    if (out_header != nullptr) {
        *out_header = header;
    }

    const auto entry_count = static_cast<std::uint32_t>(table.size());
    const std::size_t payload_size = size - 8;
    std::size_t required_payload_size = 0;
    std::uint32_t remaining_flags = header.flags;

    if ((header.flags & 0x1u) != 0) {
        required_payload_size += static_cast<std::size_t>(entry_count) * sizeof(std::uint32_t);
        remaining_flags &= ~0x1u;
    }
    if ((header.flags & 0x2u) != 0) {
        required_payload_size += static_cast<std::size_t>(entry_count) * sizeof(std::uint64_t);
        remaining_flags &= ~0x2u;
    }
    if ((header.flags & 0x4u) != 0) {
        required_payload_size += static_cast<std::size_t>(entry_count) * 16u;
        remaining_flags &= ~0x4u;
    }

    if (payload_size < required_payload_size
        || (remaining_flags == 0 && payload_size != required_payload_size)) {
        return false;
    }

    const std::uint8_t* cursor = bytes + 8;
    if ((header.flags & 0x1u) != 0) {
        for (std::uint32_t index = 0; index < entry_count; ++index) {
            ReadArchiveTables_SetAttributeDword(
                table, index, LoadLittleEndian32(cursor));
            cursor += sizeof(std::uint32_t);
        }
    }
    if ((header.flags & 0x2u) != 0) {
        for (std::uint32_t index = 0; index < entry_count; ++index) {
            ReadArchiveTables_SetAttributeQword(
                table, index, LoadLittleEndian32(cursor),
                LoadLittleEndian32(cursor + sizeof(std::uint32_t)));
            cursor += sizeof(std::uint64_t);
        }
    }
    if ((header.flags & 0x4u) != 0) {
        for (std::uint32_t index = 0; index < entry_count; ++index) {
            ReadArchiveTables_SetAttributeMd5Digest(table, index, cursor);
            cursor += 16;
        }
    }

    for (std::uint32_t index = 0; index < entry_count; ++index) {
        ReadArchiveTables_SetAttributeLookupFlag(table, index, 0);
    }

    return true;
}

SFileHashTable::~SFileHashTable() {
    Destroy();
}

void SFileHashTable::Destroy() {
    packed_entries_initialized_ = false;
    archive_packed_block_indices_ = {};

    if (!tags_.empty()) {
        std::fill(tags_.begin(), tags_.end(), 0);
    }
    std::vector<std::uint8_t>().swap(tags_);
    std::vector<std::uint32_t>().swap(packed_block_indices_);
    std::vector<std::uint64_t>().swap(stored_hash_prefixes_);

    requested_entry_count_ = 0;
    slot_count_ = 0;
    packed_entry_bit_count_ = 0;
    rebuild_shift_ = 0;
    reserved_28_ = 0;
    probe_mask_ = 0;
    probe_midpoint_ = 0;
}

void SFileHashTable::ConfigureArchiveBackedLookup(
    const std::uint8_t* const tags, const std::uint32_t tag_count,
    const PackedBitValueReader& packed_block_indices) {
    if ((tag_count != 0u && tags == nullptr) || tag_count != slot_count_) {
        AbortInvalidParameter();
    }

    packed_entries_initialized_ = false;
    packed_block_indices_.clear();
    stored_hash_prefixes_.clear();
    archive_packed_block_indices_ = packed_block_indices;

    if (tag_count == 0u) {
        tags_.clear();
        return;
    }

    tags_.assign(tags, tags + tag_count);
}

void SFileHashTable::ClearArchiveBackedLookup() {
    archive_packed_block_indices_ = {};
}

void SFileHashTable::EnsurePackedEntriesAllocated() {
    if (packed_entries_initialized_ || slot_count_ == 0) {
        return;
    }

    const bool had_archive_backed_lookup =
        archive_packed_block_indices_.packed_bits.data != nullptr;
    ClearArchiveBackedLookup();
    if (had_archive_backed_lookup && !tags_.empty()) {
        std::fill(tags_.begin(), tags_.end(), 0);
    }
    packed_block_indices_.assign(slot_count_, 0);
    stored_hash_prefixes_.assign(slot_count_, 0);
    packed_entries_initialized_ = true;
}

uint64_t SFileHashTable::ComputeProbeValue(uint64_t hash_pair) const {
    return probe_midpoint_ | (hash_pair & probe_mask_);
}

uint32_t SFileHashTable::ComputeSlotIndex(uint64_t probe_value) const {
    if (slot_count_ == 0) {
        return 0;
    }
    return static_cast<uint32_t>(probe_value % slot_count_);
}

uint8_t SFileHashTable::ComputeTagByte(uint64_t probe_value) const {
    const unsigned shift =
        static_cast<unsigned>(static_cast<std::uint8_t>(rebuild_shift_ - 8)) & 63u;
    return static_cast<std::uint8_t>(probe_value >> shift);
}

uint64_t SFileHashTable::ComputeStoredHashPrefix(uint64_t hash_pair) const {
    return hash_pair & (probe_mask_ >> 8);
}

void SFileHashTable::Init(uint32_t requested_entry_count, int rebuild_shift) {
    Destroy();

    requested_entry_count_ = requested_entry_count;
    slot_count_ = (100u * requested_entry_count) / 75u;
    for (uint32_t remaining = requested_entry_count; remaining != 0; remaining >>= 1) {
        ++packed_entry_bit_count_;
    }

    tags_.assign(slot_count_, 0);

    openwow::vfs::HashTableState probe_state{};
    openwow::vfs::HashTable_Rebuild(&probe_state, rebuild_shift);
    rebuild_shift_ = probe_state.shift;
    reserved_28_ = probe_state.reserved_28;
    probe_mask_ = probe_state.mask;
    probe_midpoint_ = probe_state.midpoint;
}

int32_t SFileHashTable::Lookup(uint64_t hash_pair, void* archive_ctx) const {
    const bool archive_backed_lookup =
        archive_packed_block_indices_.packed_bits.data != nullptr;
    if ((!archive_backed_lookup && !packed_entries_initialized_)
        || slot_count_ == 0) {
        return -1;
    }

    const uint64_t probe_value = ComputeProbeValue(hash_pair);
    const uint32_t start_slot = ComputeSlotIndex(probe_value);
    const uint8_t tag = ComputeTagByte(probe_value);
    const uint64_t expected_hash_prefix = ComputeStoredHashPrefix(hash_pair);
    const auto* const lookup_context =
        static_cast<const LookupContext*>(archive_ctx);

    uint32_t pos = start_slot;
    do {
        if (tags_[pos] == 0) {
            break;
        }
        if (tags_[pos] == tag) {
            std::uint32_t block_index = 0;
            std::uint64_t stored_hash_prefix = 0;

            if (archive_backed_lookup) {
                if (lookup_context == nullptr
                    || !ReadPackedValue(archive_packed_block_indices_, pos,
                                        &block_index)
                    || !ReadPackedValue(
                        lookup_context->stored_hash_prefixes, block_index,
                        &stored_hash_prefix)) {
                    return -1;
                }
            } else {
                block_index = packed_block_indices_[pos];
                stored_hash_prefix = stored_hash_prefixes_[pos];
            }

            if (stored_hash_prefix == expected_hash_prefix) {
                return static_cast<int32_t>(block_index);
            }
        }
        pos = (pos + 1) % slot_count_;
    } while (pos != start_slot);

    return -1;
}

bool SFileHashTable::Insert(const char* filename, uint32_t block_index,
                             void* archive_ctx) {
    (void)archive_ctx;
    if (filename == nullptr || slot_count_ == 0) {
        return false;
    }

    EnsurePackedEntriesAllocated();

    const uint64_t hash_pair = ComputeFileHashPair(filename);
    const uint64_t probe_value = ComputeProbeValue(hash_pair);
    const uint32_t start_slot = ComputeSlotIndex(probe_value);
    const uint8_t tag = ComputeTagByte(probe_value);
    const uint64_t stored_hash_prefix = ComputeStoredHashPrefix(hash_pair);

    uint32_t pos = start_slot;
    do {
        if (tags_[pos] == 0) {
            tags_[pos] = tag;
            packed_block_indices_[pos] = block_index;
            stored_hash_prefixes_[pos] = stored_hash_prefix;
            return true;
        }
        if (tags_[pos] == tag && stored_hash_prefixes_[pos] == stored_hash_prefix) {
            return false;
        }
        pos = (pos + 1) % slot_count_;
    } while (pos != start_slot);

    return false;
}

bool SFileHashTable::Resize(int current_rebuild_shift) {
    if (current_rebuild_shift == 64) {
        return false;
    }

    packed_block_indices_.clear();
    stored_hash_prefixes_.clear();
    packed_entries_initialized_ = false;
    std::fill(tags_.begin(), tags_.end(), 0);

    openwow::vfs::HashTableState probe_state{};
    probe_state.reserved_28 = reserved_28_;
    openwow::vfs::HashTable_Rebuild(&probe_state, current_rebuild_shift + 1);
    rebuild_shift_ = probe_state.shift;
    reserved_28_ = probe_state.reserved_28;
    probe_mask_ = probe_state.mask;
    probe_midpoint_ = probe_state.midpoint;
    return true;
}

uint32_t SFileHashLookup(void* archive, const uint8_t* filename,
                          bool fuzzy_match, bool* is_patch_file) {
    if (archive == nullptr || filename == nullptr) {
        AbortInvalidParameter();
    }

    auto* const archive_view =
        static_cast<SFileHashLookupArchiveView*>(archive);
    if (archive_view->ensure_tables_loaded != nullptr) {
        archive_view->ensure_tables_loaded(archive_view);
    }

    const auto table_capacity =
        openwow::vfs::GetHashTableCapacity(&archive_view->hash_table);
    const std::uint32_t table_mask = table_capacity - 1u;
    const std::uint32_t start_slot = MPQ_HashString(filename, 0) & table_mask;

    const auto* entry =
        openwow::vfs::GetHashTableEntryAt(&archive_view->hash_table, start_slot);
    if (entry->block_index == 0xFFFFFFFFu) {
        return 0xFFFFFFFFu;
    }

    const std::uint32_t hash_a = MPQ_HashString(filename, 1);
    const std::uint32_t hash_b = MPQ_HashString(filename, 2);
    std::uint32_t best_match = 0xFFFFFFFFu;
    std::uint32_t slot = start_slot;

    for (;;) {
        entry = openwow::vfs::GetHashTableEntryAt(&archive_view->hash_table, slot);
        if (entry->hash_a == hash_a && entry->hash_b == hash_b
            && entry->block_index != 0xFFFFFFFEu) {
            const auto entry_platform = GetHashEntryPlatformByte(*entry);
            if (entry->locale == archive_view->locale
                && entry_platform == archive_view->platform) {
                best_match = slot;
                break;
            }

            if (fuzzy_match
                && (entry->locale == 0u || entry->locale == archive_view->locale)
                && (entry_platform == 0u
                    || entry_platform == archive_view->platform)) {
                best_match = slot;
            }
        }

        slot = (slot + 1u) & table_mask;
        if (slot == start_slot) {
            break;
        }

        entry = openwow::vfs::GetHashTableEntryAt(&archive_view->hash_table, slot);
        if (entry->block_index == 0xFFFFFFFFu) {
            break;
        }
    }

    if (best_match != 0xFFFFFFFFu && is_patch_file != nullptr) {
        const auto* const matched_entry =
            openwow::vfs::GetHashTableEntryAt(&archive_view->hash_table,
                                              best_match);
        BlockTableEntry matched_block{};
        const auto& block_table = archive_view->block_table != nullptr
                                      ? *archive_view->block_table
                                      : GetEmptyBlockTableForHashLookup();
        GetBlockTableEntry(block_table, archive_view->block_table_source,
                           &matched_block, matched_entry->block_index);
        *is_patch_file = (matched_block.flags & 0x02000000u) != 0u;
    }

    return best_match;
}

std::size_t ArchiveAttributeMd5Lookup::KeyHash::operator()(
    const Key& key) const noexcept {
    std::uint64_t low = 0;
    std::uint64_t high = 0;
    std::memcpy(&low, key.bytes.data(), sizeof(low));
    std::memcpy(&high, key.bytes.data() + sizeof(low), sizeof(high));

    const auto low_hash = std::hash<std::uint64_t>{}(low);
    const auto high_hash = std::hash<std::uint64_t>{}(high);
    return low_hash ^ (high_hash + 0x9E3779B97F4A7C15ull + (low_hash << 6)
                       + (low_hash >> 2));
}

void ArchiveAttributeMd5Lookup::EnsureInitialized(
    ArchiveAttributeMd5LookupArchiveView* archive) {
    if (archive == nullptr) {
        AbortInvalidParameter();
    }

    std::call_once(init_once_, [this, archive]() {
        if (archive->ensure_tables_loaded != nullptr) {
            archive->ensure_tables_loaded(archive);
        }

        const auto entry_count =
            archive->block_entry_count != 0u
                ? archive->block_entry_count
                : static_cast<std::uint32_t>(
                      archive->block_table != nullptr
                          ? archive->block_table->size()
                          : 0u);

        block_indices_by_digest_.reserve(entry_count);

        const auto& block_table =
            archive->block_table != nullptr ? *archive->block_table
                                            : GetEmptyBlockTableForHashLookup();
        for (std::uint32_t index = 0; index < entry_count; ++index) {
            BlockTableEntry entry{};
            GetBlockTableEntry(block_table, archive->block_table_source, &entry,
                               index);

            Key key{};
            std::memcpy(key.bytes.data(), entry.attribute_md5,
                        key.bytes.size());
            block_indices_by_digest_.try_emplace(std::move(key), index);
        }

        initialized_ = true;
    });
}

int32_t ArchiveAttributeMd5Lookup::Lookup(
    ArchiveAttributeMd5LookupArchiveView* archive, const std::uint8_t* digest16,
    bool* is_patch_file) {
    if (archive == nullptr || digest16 == nullptr || is_patch_file == nullptr) {
        AbortInvalidParameter();
    }

    EnsureInitialized(archive);

    Key key{};
    std::memcpy(key.bytes.data(), digest16, key.bytes.size());

    const auto it = block_indices_by_digest_.find(key);
    if (it == block_indices_by_digest_.end()) {
        return -1;
    }

    const auto& block_table =
        archive->block_table != nullptr ? *archive->block_table
                                        : GetEmptyBlockTableForHashLookup();
    BlockTableEntry entry{};
    GetBlockTableEntry(block_table, archive->block_table_source, &entry,
                       it->second);
    *is_patch_file = (entry.flags & 0x02000000u) != 0u;
    return static_cast<int32_t>(it->second);
}

bool SCompCompress(void* dest, uint32_t* dest_size,
                   std::uint8_t* out_needs_checksum,
                   const void* src, uint32_t src_size,
                   uint32_t stage_count,
                   const SCompCompressionStageSpec* stage_specs) {

    if (dest_size == nullptr || out_needs_checksum == nullptr) {
        return false;
    }

    *out_needs_checksum = 0;
    if ((src_size != 0u && (dest == nullptr || src == nullptr))
        || (stage_count != 0u && stage_specs == nullptr)
        || *dest_size < src_size) {
        return false;
    }

    if (src_size == 0u) {
        *dest_size = 0u;
        *out_needs_checksum = 1u;
        return true;
    }

    auto* dest_bytes = static_cast<std::uint8_t*>(dest);
    const auto* src_bytes = static_cast<const std::uint8_t*>(src);

    const std::uint32_t output_capacity = *dest_size;
    const bool overlap =
        RangesOverlap(src_bytes, src_size, dest_bytes, output_capacity);

    std::uint32_t best_size = src_size;
    const std::uint8_t* best_data = src_bytes;
    std::uint8_t best_header = 0;
    std::size_t best_scratch_index = 3;

    if (stage_count == 0u) {
        if (src_bytes != dest_bytes) {
            std::memcpy(dest_bytes, src_bytes, src_size);
        }

        *dest_size = src_size;
        *out_needs_checksum = 1;
        return true;
    }

    bool has_non_primitive_request = false;
    for (std::uint32_t i = 0; i < stage_count; ++i) {
        switch (stage_specs[i].type) {
        case 0x02u:
        case 0x08u:
        case 0x10u:
        case 0x12u:
        case 0x20u:
            break;
        default:
            has_non_primitive_request = true;
            break;
        }
    }

    const std::size_t scratch_slice_count =
        (overlap ? 1u : 0u)
        + (stage_count > 1u ? 1u : 0u)
        + (has_non_primitive_request ? 1u : 0u);

    if (scratch_slice_count != 0u
        && src_size > std::numeric_limits<std::uint32_t>::max()
                          / scratch_slice_count) {
        return false;
    }

    InlineScratchBuffer16K scratch_buffer;
    std::array<std::uint8_t*, 3> scratch_slices{};
    if (scratch_slice_count != 0u) {
        try {
            scratch_buffer.ResizePreservingPrefix(
                src_size * static_cast<std::uint32_t>(scratch_slice_count));
        } catch (const std::bad_alloc&) {
            return false;
        }
        for (std::size_t i = 0; i < scratch_slice_count; ++i) {
            scratch_slices[i] = scratch_buffer.data() + (i * src_size);
        }
    }

    for (std::uint32_t request_index = 0; request_index < stage_count;
         ++request_index) {
        const auto plan = BuildSCompCompressionPlan(stage_specs[request_index].type);
        if (plan.count == 0u) {
            return false;
        }

        std::array<bool, 3> unavailable{};
        if (best_scratch_index < scratch_slice_count) {
            unavailable[best_scratch_index] = true;
        }

        const bool can_write_directly_to_dest =
            !overlap && best_data != dest_bytes;

        std::size_t final_scratch_index = scratch_slice_count;
        std::uint8_t* candidate_output = dest_bytes;
        if (!can_write_directly_to_dest) {
            final_scratch_index =
                AcquireScratchSlice(scratch_slice_count, unavailable);
            if (final_scratch_index == scratch_slice_count) {
                return false;
            }
            candidate_output = scratch_slices[final_scratch_index];
            unavailable[final_scratch_index] = true;
        }

        std::uint8_t* intermediate_output = nullptr;
        if (plan.count == 2u) {
            const auto intermediate_scratch_index =
                AcquireScratchSlice(scratch_slice_count, unavailable);
            if (intermediate_scratch_index == scratch_slice_count) {
                return false;
            }
            intermediate_output = scratch_slices[intermediate_scratch_index];
        }

        const auto combined_header = ComposeSCompHeader(plan.stages, plan.count);
        std::uint32_t current_input_size = src_size;
        const std::uint8_t* current_input = src_bytes;
        std::uint8_t* current_output = nullptr;
        bool candidate_succeeded = true;

        for (std::size_t stage_index = 0; stage_index < plan.count;
             ++stage_index) {
            std::uint32_t stage_output_size = current_input_size - 1u;
            std::uint32_t stage_status = 0;

            if (plan.count == 1u) {
                current_output = candidate_output + 1;
            } else if (stage_index == 0u) {
                current_output = intermediate_output;
            } else {
                current_output = candidate_output + 1;
            }

            plan.stages[stage_index].function(current_output, &stage_output_size,
                                              current_input, current_input_size,
                                              &stage_status,
                                              stage_specs[request_index].quality);
            (void)stage_status;
            if (stage_output_size >= current_input_size - 1u) {
                candidate_succeeded = false;
                break;
            }

            current_input = current_output;
            current_input_size = stage_output_size;
        }

        if (candidate_succeeded && combined_header != 0u) {
            candidate_output[0] = combined_header;
            const std::uint32_t candidate_size = current_input_size + 1u;
            if (candidate_size < best_size) {
                best_size = candidate_size;
                best_data = candidate_output;
                best_header = combined_header;
                best_scratch_index = final_scratch_index;
            }
        }
    }

    if (best_data != dest_bytes) {
        std::memcpy(dest_bytes, best_data, best_size);
    }

    *dest_size = best_size;
    *out_needs_checksum =
        static_cast<std::uint8_t>(SCompNeedsChecksumFlag(best_header));
    return true;
}

bool SCompDecompressTable(void* dest, uint32_t* dest_size,
                          const void* src, uint32_t src_size) {
    if (dest == nullptr || dest_size == nullptr || src == nullptr) {
        return false;
    }

    const auto output_capacity = *dest_size;
    if (output_capacity < src_size || src_size == 0u) {
        return false;
    }

    auto* dest_bytes = static_cast<std::uint8_t*>(dest);
    const auto* src_bytes = static_cast<const std::uint8_t*>(src);
    if (output_capacity == src_size) {
        if (dest_bytes != src_bytes) {
            std::memcpy(dest_bytes, src_bytes, src_size);
        }

        return true;
    }

    std::array<DecompressStageFunction, 2> decompress_chain{};
    switch (src_bytes[0]) {
    case 0x02u:
        decompress_chain[0] = &SCompDecompressZlib;
        break;
    case 0x08u:
        decompress_chain[0] = &SCompDecompressPklib;
        break;
    case 0x10u:
        decompress_chain[0] = &SCompDecompressBzip2;
        break;
    case 0x12u:
        decompress_chain[0] = &SCompDecompressLzma;
        break;
    case 0x20u:
        decompress_chain[0] = &SCompDecompressSparse;
        break;
    case 0x22u:
        decompress_chain[0] = &SCompDecompressZlib;
        decompress_chain[1] = &SCompDecompressSparse;
        break;
    case 0x30u:
        decompress_chain[0] = &SCompDecompressBzip2;
        decompress_chain[1] = &SCompDecompressSparse;
        break;
    default:
        openwow::data::PushStreamingStatusMessage(
            kSCompUnknownCompressionTypeMessage,
            kStormStatusContextSCompDecompress, src_bytes[0]);
        return false;
    }

    return DecompressTable(
        dest, dest_size, src_bytes + 1u, src_size - 1u,
        decompress_chain.data());
}

bool SCompDecompressZlib(void* dest, uint32_t* dest_size,
                         const void* src, uint32_t src_size) {

    if (dest == nullptr || dest_size == nullptr || src == nullptr) {
        return false;
    }

    const auto output_capacity = *dest_size;

    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(
        static_cast<const Bytef*>(src));
    stream.avail_in = src_size;
    stream.next_out = static_cast<Bytef*>(dest);
    stream.avail_out = output_capacity;

    if (inflateInit(&stream) != Z_OK) {
        return false;
    }

    const int inflate_result = inflate(&stream, Z_FINISH);
    const int end_result = inflateEnd(&stream);
    if (inflate_result != Z_STREAM_END || end_result != Z_OK) {
        return false;
    }

    *dest_size = static_cast<uint32_t>(stream.total_out);
    return true;
}

InlineScratchBuffer16K::InlineScratchBuffer16K() noexcept
    : data_(inline_storage_.data()) {}

void InlineScratchBuffer16K::ResizePreservingPrefix(
    std::uint32_t requested_size) {
    if (requested_size <= kInlineCapacity) {
        if (!uses_inline_storage()) {
            const auto copy_size = std::min(size_, requested_size);
            if (copy_size != 0) {
                std::memcpy(inline_storage_.data(), data_, copy_size);
            }
            heap_storage_.reset();
            data_ = inline_storage_.data();
        }

        size_ = requested_size;
        return;
    }

    if (requested_size != size_ || uses_inline_storage()) {
        auto new_storage = std::unique_ptr<std::uint8_t[]>(
            new std::uint8_t[requested_size]);
        const auto copy_size = std::min(size_, requested_size);
        if (copy_size != 0) {
            std::memcpy(new_storage.get(), data_, copy_size);
        }

        heap_storage_ = std::move(new_storage);
        data_ = heap_storage_.get();
    }

    size_ = requested_size;
}

bool DecompressTable(void* dest, uint32_t* dest_size,
                     const void* src, uint32_t src_size,
                     const DecompressStageFunction* decompress_chain) {

    if (dest == nullptr || dest_size == nullptr || src == nullptr
        || decompress_chain == nullptr || decompress_chain[0] == nullptr) {
        return false;
    }

    const auto output_capacity = *dest_size;
    auto* dest_bytes = static_cast<std::uint8_t*>(dest);
    const auto* src_bytes = static_cast<const std::uint8_t*>(src);

    std::array<DecompressStageFunction, 2> stages{};
    std::size_t stage_count = 0;
    for (; stage_count < stages.size() && decompress_chain[stage_count] != nullptr;
         ++stage_count) {
        stages[stage_count] = decompress_chain[stage_count];
    }

    InlineScratchBuffer16K scratch_buffer;
    const bool use_scratch_buffer =
        stage_count == 2
        || RangesOverlap(dest_bytes, output_capacity, src_bytes, src_size);
    if (use_scratch_buffer) {
        try {
            scratch_buffer.ResizePreservingPrefix(output_capacity);
        } catch (const std::bad_alloc&) {
            return false;
        }
    }

    const std::uint8_t* current_input = src_bytes;
    std::uint32_t current_size = src_size;
    for (std::size_t stage_index = 0; stage_index < stage_count;
         ++stage_index) {
        auto* stage_output = dest_bytes;
        if ((stage_count == 2 && stage_index == 0)
            || (stage_count == 1 && use_scratch_buffer)) {
            stage_output = scratch_buffer.data();
        }

        std::uint32_t stage_output_size = output_capacity;
        if (!stages[stage_index](stage_output, &stage_output_size,
                                 current_input, current_size)) {
            return false;
        }

        current_input = stage_output;
        current_size = stage_output_size;
    }

    if (current_input != dest_bytes) {
        std::memcpy(dest_bytes, current_input, current_size);
    }
    *dest_size = current_size;
    return true;
}

}
