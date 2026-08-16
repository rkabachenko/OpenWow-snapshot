
#include "openwow/audio/codecs/adpcm/vag_adpcm_decoder.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace openwow::audio {

const float kVagCoefficients[kVagCoeffCount][2] = {
    {0.0f,       0.0f},
    {0.9375f,    0.0f},
    {1.796875f, -0.8125f},
    {1.53125f,  -0.859375f},
    {1.90625f,  -0.9375f},
};

int VagAdpcm_DecodeBlocks(const std::uint8_t* src,
                          std::uint8_t* dst,
                          int block_count,
                          VagAdpcmState& state) {
  if (block_count <= 0) return 0;

  int total_bytes = 0;

  for (int blk = 0; blk < block_count; ++blk) {
    const std::uint8_t header = src[0];
    const int shift = header & 0x0F;
    const int coeff_idx = (header >> 4) & 0x0F;

    const int safe_idx = (coeff_idx < kVagCoeffCount) ? coeff_idx : 0;
    const float coeff1 = kVagCoefficients[safe_idx][0];
    const float coeff2 = kVagCoefficients[safe_idx][1];

    float samples[kVagSamplesPerBlock];

    const std::uint8_t* data = src + 2;
    int si = 0;

    for (int i = 0; i < kVagDataBytesPerBlock; ++i) {
      const std::uint8_t byte = data[i];

      std::int32_t lo = (byte & 0x0F) << 12;
      if (lo & 0x8000) lo |= static_cast<std::int32_t>(0xFFFF0000);
      lo >>= shift;
      samples[si++] = static_cast<float>(lo);

      std::int32_t hi = (byte & 0xF0) << 8;
      if (hi & 0x8000) hi |= static_cast<std::int32_t>(0xFFFF0000);
      hi >>= shift;
      samples[si++] = static_cast<float>(hi);
    }

    for (int i = 0; i < kVagSamplesPerBlock; ++i) {

      float filtered = coeff1 * state.history1 + coeff2 * state.history2 + samples[i];
      samples[i] = filtered;

      state.history2 = state.history1;
      state.history1 = filtered;

      std::int32_t pcm = static_cast<std::int32_t>(filtered + 0.5f);
      dst[0] = static_cast<std::uint8_t>(pcm & 0xFF);
      dst[1] = static_cast<std::uint8_t>((pcm >> 8) & 0xFF);
      dst += 2;
    }

    src += kVagBlockSize;
    total_bytes += kVagOutputBytesPerBlock;
  }

  return total_bytes;
}

}
