
#pragma once

#include <cstdint>

namespace openwow::audio {

extern const std::int16_t kImaStepTable[89];

extern const std::int32_t kImaIndexTable[16];

int ImaAdpcm_DecodeStereoBlock(const std::uint32_t* src,
                               std::int16_t* dst,
                               int block_count,
                               int block_stride,
                               int samples_per_block);

int ImaAdpcm_DecodeMonoBlock(const std::uint32_t* src,
                             std::int16_t* dst,
                             int block_count,
                             int block_stride,
                             int samples_per_block,
                             int output_stride);

}
