
#pragma once

#include <cstdint>

namespace openwow::game {

struct ComSatVoiceCodecState {
  std::uint32_t sample_rate_hz{0};
  std::uint32_t codec_mode{0};
  std::uint32_t profile_variant{0};
  std::uint32_t frame_mode{0};
  bool base_path_enabled{false};
  bool active_path_enabled{false};
  std::uint16_t sample_rate_ratio{0};
  std::uint16_t frame_sample_count{0};
  std::uint16_t sample_divisor{0};
  std::uint16_t samples_per_block{0};
  std::uint16_t selector_stage{0};
  bool selector_uses_extended_table{false};
  std::uint16_t reference_rate_units{0};
  std::uint16_t reference_rate_base_units{0};
  std::uint16_t reference_rate_base_bit_width{0};
  std::int32_t bitrate_index{-1};
};

[[nodiscard]] bool ComSatVoiceCodec_InitializeState(ComSatVoiceCodecState &state,
                                                    std::uint32_t codec_mode,
                                                    std::uint32_t sample_rate_hz,
                                                    bool input_flag_a,
                                                    bool input_flag_b);

[[nodiscard]] bool ComSatVoiceCodec_ApplyBitrateIndex(ComSatVoiceCodecState &state,
                                                      std::int32_t bitrate_index);

[[nodiscard]] std::uint32_t ComSatVoiceCodec_QualityIndexToSampleRate(
    std::uint32_t quality_index) noexcept;

[[nodiscard]] std::uint32_t ComSatVoiceCodec_SampleRateToQualityIndex(
    std::uint32_t sample_rate_hz) noexcept;

}
