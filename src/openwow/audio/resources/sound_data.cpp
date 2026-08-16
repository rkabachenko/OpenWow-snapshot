
#define DR_WAV_IMPLEMENTATION
#include "dr_libs/dr_wav.h"

#include "openwow/audio/resources/sound_data.h"
#include "openwow/audio/codecs/audio_decoder.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace openwow::audio {

std::uint32_t SoundData::TotalFrames() const {
  if (channels == 0) return 0;
  return static_cast<std::uint32_t>(samples.size()) / channels;
}

float SoundData::DurationSeconds() const {
  if (sample_rate == 0 || channels == 0) return 0.0f;
  return static_cast<float>(TotalFrames()) / static_cast<float>(sample_rate);
}

std::optional<SoundData> DecodeWav(const std::uint8_t* data, std::size_t size) {
  if (data == nullptr || size < 12) return std::nullopt;

  drwav wav;
  if (!drwav_init_memory(&wav, data, size, nullptr)) {
    return std::nullopt;
  }

  if (wav.channels == 0 || wav.channels > 8 || wav.sampleRate == 0) {
    drwav_uninit(&wav);
    return std::nullopt;
  }

  const drwav_uint64 total_frames = wav.totalPCMFrameCount;
  if (total_frames == 0 || total_frames > 0x7FFFFFFF) {
    drwav_uninit(&wav);
    return std::nullopt;
  }

  SoundData result;
  result.sample_rate = wav.sampleRate;
  result.channels = static_cast<std::uint8_t>(wav.channels);
  result.samples.resize(static_cast<std::size_t>(total_frames) * wav.channels);

  const drwav_uint64 frames_read = drwav_read_pcm_frames_s16(
      &wav, total_frames, result.samples.data());

  drwav_uninit(&wav);

  if (frames_read == 0) {
    return std::nullopt;
  }

  if (frames_read < total_frames) {
    result.samples.resize(static_cast<std::size_t>(frames_read) * result.channels);
  }

  return result;
}

std::optional<SoundData> DecodeWav(const std::vector<std::uint8_t>& bytes) {
  return DecodeWav(bytes.data(), bytes.size());
}

std::optional<SoundData> DecodeAudio(const std::vector<std::uint8_t>& bytes) {
  if (bytes.empty()) return std::nullopt;

  const AudioFormat fmt = DetectAudioFormat(bytes);

  if (fmt == AudioFormat::WAV) {
    return DecodeWav(bytes);
  }

  if (fmt == AudioFormat::Unknown) {
    return std::nullopt;
  }

  auto decoder = CreateAudioDecoder(fmt, bytes);
  if (!decoder || !decoder->IsOpen()) {
    return std::nullopt;
  }

  const std::uint32_t sample_rate = decoder->GetSampleRate();
  const std::uint32_t channels = decoder->GetChannels();
  const std::uint64_t total_frames = decoder->GetTotalFrames();

  if (sample_rate == 0 || channels == 0) return std::nullopt;

  SoundData result;
  result.sample_rate = sample_rate;
  result.channels = static_cast<std::uint8_t>(channels);

  if (total_frames > 0 && total_frames < 0x7FFFFFFF) {
    result.samples.reserve(static_cast<std::size_t>(total_frames) * channels);
  }

  constexpr std::size_t kChunkFrames = 4096;
  std::vector<float> float_buf(kChunkFrames * channels);

  while (true) {
    const std::size_t decoded = decoder->Decode(float_buf.data(), kChunkFrames);
    if (decoded == 0) break;

    const std::size_t sample_count = decoded * channels;
    for (std::size_t i = 0; i < sample_count; ++i) {
      const float clamped = std::clamp(float_buf[i], -1.0f, 1.0f);
      result.samples.push_back(
          static_cast<std::int16_t>(clamped * 32767.0f));
    }
  }

  if (result.samples.empty()) return std::nullopt;
  return result;
}

SoundData Resample(const SoundData& input, std::uint32_t target_rate) {
  if (input.Empty() || target_rate == 0) return input;
  if (input.sample_rate == target_rate) return input;

  const std::uint32_t in_frames = input.TotalFrames();
  const double ratio = static_cast<double>(target_rate) / static_cast<double>(input.sample_rate);
  const auto out_frames = static_cast<std::uint32_t>(std::ceil(in_frames * ratio));

  SoundData out;
  out.sample_rate = target_rate;
  out.channels = input.channels;
  out.samples.resize(static_cast<std::size_t>(out_frames) * input.channels);

  for (std::uint32_t i = 0; i < out_frames; ++i) {
    const double src_pos = static_cast<double>(i) / ratio;
    const auto idx0 = static_cast<std::uint32_t>(src_pos);
    const auto idx1 = std::min(idx0 + 1, in_frames - 1);
    const float frac = static_cast<float>(src_pos - idx0);

    for (std::uint8_t ch = 0; ch < input.channels; ++ch) {
      const float s0 = static_cast<float>(input.samples[static_cast<std::size_t>(idx0) * input.channels + ch]);
      const float s1 = static_cast<float>(input.samples[static_cast<std::size_t>(idx1) * input.channels + ch]);
      const float interp = s0 + (s1 - s0) * frac;
      out.samples[static_cast<std::size_t>(i) * input.channels + ch] =
          static_cast<std::int16_t>(std::clamp(interp, -32768.0f, 32767.0f));
    }
  }

  return out;
}

SoundData MonoToStereo(const SoundData& input) {
  if (input.channels != 1 || input.Empty()) return input;

  SoundData out;
  out.sample_rate = input.sample_rate;
  out.channels = 2;
  out.samples.resize(input.samples.size() * 2);

  for (std::size_t i = 0; i < input.samples.size(); ++i) {
    out.samples[i * 2 + 0] = input.samples[i];
    out.samples[i * 2 + 1] = input.samples[i];
  }

  return out;
}

SoundData ConvertChannelCount(const SoundData& input,
                              const std::uint8_t target_channels) {
  if (input.Empty() || input.channels == 0u || target_channels == 0u ||
      input.channels == target_channels) {
    return input;
  }
  if (input.channels == 1u && target_channels == 2u) {
    return MonoToStereo(input);
  }

  SoundData output;
  output.sample_rate = input.sample_rate;
  output.channels = target_channels;
  const std::size_t frames = input.TotalFrames();
  output.samples.resize(frames * target_channels);

  for (std::size_t frame = 0; frame < frames; ++frame) {
    const auto *source = input.samples.data() + frame * input.channels;
    auto *destination = output.samples.data() + frame * target_channels;
    if (target_channels == 1u) {
      std::int32_t sum = 0;
      for (std::uint8_t channel = 0; channel < input.channels; ++channel) {
        sum += source[channel];
      }
      destination[0] = static_cast<std::int16_t>(
          sum / static_cast<std::int32_t>(input.channels));
      continue;
    }

    if (target_channels == 2u) {
      float left = static_cast<float>(source[0]);
      float right = static_cast<float>(source[std::min<std::uint8_t>(1u, input.channels - 1u)]);
      float left_weight = 1.0f;
      float right_weight = 1.0f;
      for (std::uint8_t channel = 2u; channel < input.channels; ++channel) {
        if ((channel & 1u) == 0u) {
          left += static_cast<float>(source[channel]) * 0.5f;
          left_weight += 0.5f;
        } else {
          right += static_cast<float>(source[channel]) * 0.5f;
          right_weight += 0.5f;
        }
      }
      destination[0] = static_cast<std::int16_t>(std::clamp(
          left / left_weight, -32768.0f, 32767.0f));
      destination[1] = static_cast<std::int16_t>(std::clamp(
          right / right_weight, -32768.0f, 32767.0f));
      continue;
    }

    for (std::uint8_t channel = 0; channel < target_channels; ++channel) {
      destination[channel] = source[std::min<std::uint8_t>(
          channel, static_cast<std::uint8_t>(input.channels - 1u))];
    }
  }
  return output;
}

SoundData Normalize(const SoundData& input, std::uint32_t target_rate, std::uint8_t target_channels) {
  SoundData result = ConvertChannelCount(input, target_channels);
  if (result.sample_rate != target_rate) {
    result = Resample(result, target_rate);
  }
  return result;
}

}
