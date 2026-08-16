
#include "openwow/game/comsat_voice_codec_post_filter.h"

#include <cmath>

namespace openwow::game {
namespace {

constexpr double kInputCoefficient0 = 0.9780304792065596;
constexpr double kInputCoefficient1 = -1.956060958413119;
constexpr double kInputCoefficient2 = 0.9780304792065596;
constexpr double kOutputCoefficient1 = 1.955578240315035;
constexpr double kOutputCoefficient2 = -0.9565436765112032;
constexpr double kTailHistoryResetThreshold = 1.0e-10;

}

void ComSatVoiceCodec_ApplyActivePathIirFilter(
    ComSatVoiceCodecPostFilterState &state,
    const std::span<const float, kComSatVoiceCodecPostFilterBlockSampleCount> input_samples,
    const std::span<float, kComSatVoiceCodecPostFilterBlockSampleCount> output_samples) {
  for (std::size_t sample_index = 0; sample_index < input_samples.size(); ++sample_index) {
    const float current_input = input_samples[sample_index];
    const float previous_input = state.previous_input;
    const float older_input = state.older_input;
    const float previous_output = state.previous_output;
    const float older_output = state.older_output;

    const float current_output = static_cast<float>(
        static_cast<double>(current_input) * kInputCoefficient0 +
        static_cast<double>(previous_input) * kInputCoefficient1 +
        static_cast<double>(older_input) * kInputCoefficient2 +
        static_cast<double>(previous_output) * kOutputCoefficient1 +
        static_cast<double>(older_output) * kOutputCoefficient2);

    output_samples[sample_index] = current_output;

    state.older_input = previous_input;
    state.previous_input = current_input;
    state.older_output = previous_output;
    state.previous_output = current_output;
  }

  if (std::fabs(static_cast<double>(state.previous_output)) +
          std::fabs(static_cast<double>(state.older_output)) <
      kTailHistoryResetThreshold) {
    state.previous_output = 0.0f;
    state.older_output = 0.0f;
  }
}

}
