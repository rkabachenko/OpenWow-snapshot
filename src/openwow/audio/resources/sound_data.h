
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace openwow::audio {

struct SoundData {
  std::vector<std::int16_t> samples;
  std::uint32_t sample_rate{44100};
  std::uint8_t channels{2};

  [[nodiscard]] std::uint32_t TotalFrames() const;

  [[nodiscard]] float DurationSeconds() const;

  [[nodiscard]] bool Empty() const { return samples.empty(); }
};

[[nodiscard]] std::optional<SoundData> DecodeWav(const std::uint8_t* data, std::size_t size);

[[nodiscard]] std::optional<SoundData> DecodeWav(const std::vector<std::uint8_t>& bytes);

[[nodiscard]] std::optional<SoundData> DecodeAudio(const std::vector<std::uint8_t>& bytes);

[[nodiscard]] SoundData Resample(const SoundData& input, std::uint32_t target_rate);

[[nodiscard]] SoundData MonoToStereo(const SoundData& input);

[[nodiscard]] SoundData ConvertChannelCount(const SoundData& input,
                                            std::uint8_t target_channels);

[[nodiscard]] SoundData Normalize(const SoundData& input, std::uint32_t target_rate, std::uint8_t target_channels);

}
