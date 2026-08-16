
#pragma once

#include <cstdint>

namespace openwow::audio {

inline constexpr int kVagCoeffCount = 5;

inline constexpr int kVagBlockSize = 16;

inline constexpr int kVagSamplesPerBlock = 28;

inline constexpr int kVagOutputBytesPerBlock = 56;

inline constexpr int kVagDataBytesPerBlock = 14;

extern const float kVagCoefficients[kVagCoeffCount][2];

struct VagAdpcmState {
  float history1 = 0.0f;
  float history2 = 0.0f;
};

int VagAdpcm_DecodeBlocks(const std::uint8_t* src,
                          std::uint8_t* dst,
                          int block_count,
                          VagAdpcmState& state);

inline int VagAdpcm_DecodeBlock(const std::uint8_t* src,
                                std::uint8_t* dst,
                                VagAdpcmState& state) {
  return VagAdpcm_DecodeBlocks(src, dst, 1, state);
}

}
