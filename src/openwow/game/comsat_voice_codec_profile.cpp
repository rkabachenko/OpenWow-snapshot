
#include "openwow/game/comsat_voice_codec_profile.h"

#include <array>
#include <cstdint>

namespace openwow::game {
namespace {

constexpr std::uint32_t kSampleRateQuantumHz = 8000u;
constexpr std::uint16_t kFrameSampleBase = 160u;
constexpr std::uint16_t kWideBlockSamples = 160u;
constexpr std::uint16_t kNarrowBlockSamples = 80u;
constexpr std::uint16_t kReferenceRateOffsetDivisor = 400u;

constexpr std::array<std::uint8_t, 8> kEightKhzReferenceRates{
    0u, 0u, 0u, 0u, 20u, 40u, 66u, 100u};
constexpr std::array<std::uint8_t, 10> kSixteenKhzReferenceRates{
    0u, 0u, 0u, 0u, 20u, 40u, 66u, 100u, 130u, 156u};
constexpr std::array<std::uint16_t, 16> kThirtyTwoKhzReferenceRates{
    0u, 0u, 0u, 0u, 20u, 40u, 66u, 100u, 130u, 156u, 180u, 204u, 228u, 256u, 276u, 300u};
constexpr std::array<std::uint32_t, 4> kReferenceRateOffsets{6000u, 8350u, 10600u, 13900u};
constexpr std::uint32_t kDefaultReferenceRateOffset = 13900u;

std::int32_t MaxBitrateIndexForSampleRate(const std::uint32_t sample_rate_hz) {
  switch (sample_rate_hz) {
  case 8000u:
    return 7;
  case 16000u:
    return 9;
  case 32000u:
    return 15;
  default:
    return -1;
  }
}

std::uint16_t ComputeReferenceRateBaseUnits(const ComSatVoiceCodecState &state) {
  if (state.sample_divisor == 0u) {
    return 0u;
  }

  std::uint32_t numerator = 0u;
  switch (state.sample_rate_hz) {
  case 8000u:
    numerator = 100u;
    break;
  case 16000u:
    numerator = 156u;
    break;
  case 32000u:
    numerator = 300u;
    break;
  default:
    break;
  }

  numerator /= state.sample_divisor;
  numerator += kDefaultReferenceRateOffset /
               (kReferenceRateOffsetDivisor * static_cast<std::uint32_t>(state.sample_divisor));
  return static_cast<std::uint16_t>(numerator);
}

std::uint16_t ComputeReferenceRateUnits(const ComSatVoiceCodecState &state,
                                        const std::int32_t bitrate_index) {
  if (bitrate_index < 0 || state.sample_divisor == 0u) {
    return 0u;
  }

  std::uint32_t units = 0u;
  switch (state.sample_rate_hz) {
  case 8000u:
    if (static_cast<std::size_t>(bitrate_index) < kEightKhzReferenceRates.size()) {
      units = kEightKhzReferenceRates[static_cast<std::size_t>(bitrate_index)];
    }
    break;
  case 16000u:
    if (static_cast<std::size_t>(bitrate_index) < kSixteenKhzReferenceRates.size()) {
      units = kSixteenKhzReferenceRates[static_cast<std::size_t>(bitrate_index)];
    }
    break;
  case 32000u:
    if (static_cast<std::size_t>(bitrate_index) < kThirtyTwoKhzReferenceRates.size()) {
      units = kThirtyTwoKhzReferenceRates[static_cast<std::size_t>(bitrate_index)];
    }
    break;
  default:
    break;
  }

  units /= state.sample_divisor;
  if (state.profile_variant != 0u || state.frame_mode == 3u) {
    const std::uint32_t offset =
        bitrate_index <= 3 ? kReferenceRateOffsets[static_cast<std::size_t>(bitrate_index)]
                           : kDefaultReferenceRateOffset;
    units += offset /
             (kReferenceRateOffsetDivisor * static_cast<std::uint32_t>(state.sample_divisor));
  }

  return static_cast<std::uint16_t>(units);
}

std::uint16_t ComputeBitWidth(std::uint16_t value) {
  std::uint16_t bit_width = 0u;
  while (value != 0u) {
    ++bit_width;
    value >>= 1u;
  }
  return bit_width;
}

}

bool ComSatVoiceCodec_InitializeState(ComSatVoiceCodecState &state,
                                      const std::uint32_t codec_mode,
                                      const std::uint32_t sample_rate_hz, const bool input_flag_a,
                                      const bool input_flag_b) {
  state.base_path_enabled = true;
  state.active_path_enabled = true;

  switch (codec_mode) {
  case 0u:
    break;
  case 1u:
    state.base_path_enabled = false;
    state.active_path_enabled = false;
    break;
  case 2u:
  case 3u:
    break;
  default:
    return false;
  }

  if (sample_rate_hz == 8000u) {
    state.base_path_enabled = false;
  }

  const std::uint16_t sample_rate_ratio =
      static_cast<std::uint16_t>(sample_rate_hz / kSampleRateQuantumHz);
  if (sample_rate_ratio == 0u) {
    return false;
  }

  const std::uint16_t frame_sample_count =
      static_cast<std::uint16_t>(kFrameSampleBase * sample_rate_ratio);
  const std::uint16_t block_samples =
      static_cast<std::uint16_t>((codec_mode == 0u || codec_mode == 2u) ? kNarrowBlockSamples
                                                                         : kWideBlockSamples);
  const std::uint16_t sample_divisor =
      static_cast<std::uint16_t>(frame_sample_count / (block_samples * sample_rate_ratio));

  state.sample_rate_ratio = sample_rate_ratio;
  state.frame_sample_count = frame_sample_count;
  state.sample_divisor = sample_divisor;
  state.samples_per_block = static_cast<std::uint16_t>(frame_sample_count / sample_divisor);
  state.frame_mode = 0u;
  state.selector_stage = 0u;
  state.selector_uses_extended_table = true;
  state.reference_rate_units = 0u;
  state.reference_rate_base_units = 0u;
  state.reference_rate_base_bit_width = 0u;
  state.bitrate_index = -1;

  if (input_flag_b) {
    state.frame_mode = 2u;
  }

  if (input_flag_b && !input_flag_a) {
    return false;
  }

  state.sample_rate_hz = sample_rate_hz;
  state.codec_mode = codec_mode;
  return true;
}

bool ComSatVoiceCodec_ApplyBitrateIndex(ComSatVoiceCodecState &state,
                                        const std::int32_t bitrate_index) {
  if (bitrate_index < 0) {
    return false;
  }

  if (const std::int32_t max_bitrate_index = MaxBitrateIndexForSampleRate(state.sample_rate_hz);
      max_bitrate_index >= 0 && bitrate_index > max_bitrate_index) {
    return false;
  }

  switch (state.profile_variant) {
  case 0u:
    state.active_path_enabled = true;
    if (bitrate_index <= 3) {
      state.selector_stage = static_cast<std::uint16_t>(bitrate_index + 1);
      state.selector_uses_extended_table = false;
    } else {
      state.selector_stage = 4u;
      state.selector_uses_extended_table = true;
    }
    break;
  case 1u:
    state.active_path_enabled = false;
    state.selector_uses_extended_table = true;
    state.selector_stage = 0u;
    break;
  case 2u:
  case 3u:
    state.active_path_enabled = true;
    state.selector_uses_extended_table = true;
    state.selector_stage =
        static_cast<std::uint16_t>(bitrate_index <= 3 ? bitrate_index + 1 : 4);
    break;
  default:
    return false;
  }

  state.reference_rate_base_units = ComputeReferenceRateBaseUnits(state);
  state.reference_rate_base_bit_width = ComputeBitWidth(state.reference_rate_base_units);
  state.reference_rate_units = ComputeReferenceRateUnits(state, bitrate_index);
  state.bitrate_index = bitrate_index;
  return true;
}

std::uint32_t ComSatVoiceCodec_QualityIndexToSampleRate(
    const std::uint32_t quality_index) noexcept {
  switch (quality_index) {
  case 0u:
    return 8000u;
  case 1u:
    return 16000u;
  case 2u:
    return 32000u;
  default:
    return 0u;
  }
}

std::uint32_t ComSatVoiceCodec_SampleRateToQualityIndex(
    const std::uint32_t sample_rate_hz) noexcept {
  switch (sample_rate_hz) {
  case 8000u:
    return 0u;
  case 16000u:
    return 1u;
  case 32000u:
    return 2u;
  default:
    return 0u;
  }
}

}
