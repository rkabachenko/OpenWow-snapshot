
#pragma once

#include <cstddef>
#include <span>

namespace openwow::game {

inline constexpr std::size_t kComSatVoiceCodecPostFilterBlockSampleCount = 160u;

struct ComSatVoiceCodecPostFilterState {
  float previous_input{0.0f};
  float older_input{0.0f};
  float previous_output{0.0f};
  float older_output{0.0f};
};

void ComSatVoiceCodec_ApplyActivePathIirFilter(
    ComSatVoiceCodecPostFilterState &state,
    std::span<const float, kComSatVoiceCodecPostFilterBlockSampleCount> input_samples,
    std::span<float, kComSatVoiceCodecPostFilterBlockSampleCount> output_samples);

}
