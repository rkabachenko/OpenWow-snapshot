
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace openwow::audio {

inline std::int16_t RoundVorbisSampleToSignedPcm16(const float sample) {
  const long rounded = std::lrintf(sample * 32768.0f);
  if (rounded > 32767) {
    return 32767;
  }
  if (rounded < -32768) {
    return -32768;
  }
  return static_cast<std::int16_t>(rounded);
}

inline std::size_t ConvertVorbisFrameToSignedPcm16Le(float *const *channel_pcm,
                                                     const std::uint32_t channels,
                                                     const std::size_t frame_count,
                                                     const std::span<std::uint8_t> output) {
  if (!channel_pcm || channels == 0 || frame_count == 0) {
    return 0;
  }

  constexpr std::size_t kBytesPerSample = sizeof(std::int16_t);
  const std::size_t bytes_per_frame = static_cast<std::size_t>(channels) * kBytesPerSample;
  if (bytes_per_frame == 0) {
    return 0;
  }

  for (std::uint32_t channel = 0; channel < channels; ++channel) {
    if (!channel_pcm[channel]) {
      return 0;
    }
  }

  const std::size_t writable_frames = std::min(frame_count, output.size() / bytes_per_frame);
  for (std::size_t frame = 0; frame < writable_frames; ++frame) {
    for (std::uint32_t channel = 0; channel < channels; ++channel) {
      const auto encoded =
          static_cast<std::uint16_t>(RoundVorbisSampleToSignedPcm16(channel_pcm[channel][frame]));
      const std::size_t byte_offset =
          (frame * static_cast<std::size_t>(channels) + channel) * kBytesPerSample;
      output[byte_offset] = static_cast<std::uint8_t>(encoded & 0xFF);
      output[byte_offset + 1] = static_cast<std::uint8_t>(encoded >> 8);
    }
  }

  return writable_frames;
}

}
