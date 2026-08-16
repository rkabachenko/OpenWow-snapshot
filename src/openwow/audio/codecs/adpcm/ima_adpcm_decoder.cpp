
#include "openwow/audio/codecs/adpcm/ima_adpcm_decoder.h"

#include <algorithm>
#include <cstdint>

namespace openwow::audio {

const std::int16_t kImaStepTable[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,
    16,    17,    19,    21,    23,    25,    28,    31,
    34,    37,    41,    45,    50,    55,    60,    66,
    73,    80,    88,    97,    107,   118,   130,   143,
    157,   173,   190,   209,   230,   253,   279,   307,
    337,   371,   408,   449,   494,   544,   598,   658,
    724,   796,   876,   963,   1060,  1166,  1282,  1411,
    1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,
    3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,
    7132,  7845,  8630,  9493,  10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767
};

const std::int32_t kImaIndexTable[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};

static inline std::int32_t DecodeNibble(std::uint32_t nibble,
                                        std::int32_t predictor,
                                        int& step_index) {
    const std::int32_t step = kImaStepTable[step_index];

    std::int32_t diff = step >> 3;
    if (nibble & 1) diff += step >> 2;
    if (nibble & 2) diff += step >> 1;
    if (nibble & 4) diff += step;
    if (nibble & 8) diff = -diff;

    std::int32_t result = predictor + diff;

    result = std::clamp(result, -32768, 32767);

    step_index += kImaIndexTable[nibble & 0xF];

    step_index = std::clamp(step_index, 0, 88);

    return result;
}

int ImaAdpcm_DecodeStereoBlock(const std::uint32_t* src,
                               std::int16_t* dst,
                               int block_count,
                               int block_stride,
                               int samples_per_block) {
    if (block_count <= 0) return 0;

    auto src_bytes = reinterpret_cast<const std::uint8_t*>(src);

    for (int block = 0; block < block_count; ++block) {
        auto block_ptr = reinterpret_cast<const std::uint32_t*>(src_bytes);

        const std::uint32_t hdr_left = block_ptr[0];
        std::int32_t pred_left = static_cast<std::int16_t>(hdr_left & 0xFFFF);
        int idx_left = static_cast<int>((hdr_left >> 16) & 0xFF);
        if (static_cast<unsigned>(idx_left) > 0x58) return 19;

        const std::uint32_t hdr_right = block_ptr[1];
        std::int32_t pred_right = static_cast<std::int16_t>(hdr_right & 0xFFFF);
        int idx_right = static_cast<int>((hdr_right >> 16) & 0xFF);
        if (static_cast<unsigned>(idx_right) > 0x58) return 19;

        *dst++ = static_cast<std::int16_t>(pred_left);
        *dst++ = static_cast<std::int16_t>(pred_right);

        int remaining = samples_per_block - 1;
        const std::uint32_t* data_ptr = block_ptr + 2;

        while (remaining > 0) {

            const int chunk = std::min(remaining, 8);

            std::uint32_t nibbles_left  = *data_ptr++;
            std::uint32_t nibbles_right = *data_ptr++;

            for (int i = 0; i < chunk; ++i) {

                pred_left = DecodeNibble(nibbles_left & 0xF, pred_left, idx_left);

                pred_right = DecodeNibble(nibbles_right & 0xF, pred_right, idx_right);

                nibbles_left  >>= 4;
                nibbles_right >>= 4;

                *dst++ = static_cast<std::int16_t>(pred_left);
                *dst++ = static_cast<std::int16_t>(pred_right);
            }

            remaining -= chunk;
        }

        src_bytes += block_stride;
    }

    return 0;
}

int ImaAdpcm_DecodeMonoBlock(const std::uint32_t* src,
                             std::int16_t* dst,
                             int block_count,
                             int block_stride,
                             int samples_per_block,
                             int output_stride) {
    if (block_count <= 0) return 0;

    auto src_bytes = reinterpret_cast<const std::uint8_t*>(src);

    for (int block = 0; block < block_count; ++block) {
        auto block_ptr = reinterpret_cast<const std::uint32_t*>(src_bytes);

        const std::uint32_t hdr = block_ptr[0];
        std::int32_t predictor = static_cast<std::int16_t>(hdr & 0xFFFF);
        int step_index = static_cast<int>((hdr >> 16) & 0xFF);
        if (static_cast<unsigned>(step_index) > 0x58) return 19;

        *dst = static_cast<std::int16_t>(predictor);
        dst += output_stride;

        int remaining = samples_per_block - 1;
        auto byte_ptr = reinterpret_cast<const std::uint8_t*>(block_ptr + 1);

        while (remaining > 0) {
            const std::uint8_t packed = *byte_ptr++;

            predictor = DecodeNibble(packed & 0xF, predictor, step_index);
            *dst = static_cast<std::int16_t>(predictor);
            dst += output_stride;
            --remaining;

            if (remaining <= 0) break;

            predictor = DecodeNibble((packed >> 4) & 0xF, predictor, step_index);
            *dst = static_cast<std::int16_t>(predictor);
            dst += output_stride;
            --remaining;
        }

        src_bytes += block_stride;
    }

    return 0;
}

}
