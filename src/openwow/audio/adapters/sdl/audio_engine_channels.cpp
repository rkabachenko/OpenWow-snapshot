#include "openwow/audio/adapters/sdl/audio_engine_internal.h"

namespace openwow::audio {

void AudioEngine::SetMaxSfxChannels(const int max_channels) {
  const int clamped = std::clamp(max_channels, kMinSoftwareChannels,
                                 kSfxChannelCapacity);
  if (clamped == max_sfx_channels_) {
    return;
  }

  std::vector<std::shared_ptr<MusicPcmStream>> retired_streams;
  if (initialized_ && device_ != 0) {
    SDL_LockAudioDevice(device_);
  }
  if (clamped < max_sfx_channels_) {
    retired_streams.reserve(
        static_cast<std::size_t>(max_sfx_channels_ - clamped));
    for (int index = clamped; index < max_sfx_channels_; ++index) {
      if (auto stream = StopSfxChannel(
              sfx_[static_cast<std::size_t>(index)],
              sfx_fades_[static_cast<std::size_t>(index)], 0.0f)) {
        retired_streams.push_back(std::move(stream));
      }

      handleIdByChannel_[static_cast<std::size_t>(index)] = kNoOwningHandleId;
    }
  }
  max_sfx_channels_ = clamped;
  if (initialized_ && device_ != 0) {
    SDL_UnlockAudioDevice(device_);
  }
}

int AudioEngine::AllocateChannel(const int incoming_priority) {

  for (int i = 0; i < max_sfx_channels_; ++i) {
    if (!sfx_[static_cast<std::size_t>(i)].active) return i;
  }

  std::array<HandleEntry*, kSfxChannelCapacity> owner_by_channel{};
  for (int i = 0; i < max_sfx_channels_; ++i) {
    const std::uint32_t owner_id = handleIdByChannel_[static_cast<std::size_t>(i)];
    if (owner_id == kNoOwningHandleId) {
      continue;
    }
    const auto it = handles_.find(owner_id);
    if (it == handles_.end()) {
      continue;
    }
    HandleEntry& entry = it->second;
    if (entry.state == AudioPlaybackState::Stopped || entry.sdlChannel != i) {
      continue;
    }
    if (entry.sdlGeneration ==
        directPlaybackGenerations_[static_cast<std::size_t>(i)]) {
      owner_by_channel[static_cast<std::size_t>(i)] = &entry;
    }
  }

  int candidate_priority = std::numeric_limits<int>::max();
  int candidate_channel = -1;
  float candidate_completion_ratio = -1.0f;
  HandleEntry* candidate_owner = nullptr;

  for (int i = 0; i < max_sfx_channels_; ++i) {
    const auto& ch = sfx_[static_cast<std::size_t>(i)];
    const std::uint32_t total_frames = ch.data ? ch.data->TotalFrames() : 0u;
    const float ratio = (total_frames > 0)
                            ? static_cast<float>(ch.position) / static_cast<float>(total_frames)
                            : 1.0f;

    HandleEntry* owner = owner_by_channel[static_cast<std::size_t>(i)];

    const int priority = owner != nullptr ? owner->priority : -1;
    if (priority < candidate_priority ||
        (priority == candidate_priority && ratio > candidate_completion_ratio)) {
      candidate_priority = priority;
      candidate_channel = i;
      candidate_completion_ratio = ratio;
      candidate_owner = owner;
    }
  }

  if (candidate_channel < 0 || incoming_priority < candidate_priority) {
    return -1;
  }

  if (candidate_owner != nullptr) {
    ReleaseChannelOwnershipIndex(*candidate_owner);
    candidate_owner->state = AudioPlaybackState::Stopped;
    candidate_owner->sdlChannel = -1;
    candidate_owner->sdlGeneration = 0;
  }
  auto& reclaimed = sfx_[static_cast<std::size_t>(candidate_channel)];
  reclaimed.active = false;
  reclaimed.data = {};
  reclaimed.position = 0;
  return candidate_channel;
}

AudioDirectPlaybackHandle AudioEngine::ClaimChannelInstance(const int channel) {
  if (channel < 0 || channel >= max_sfx_channels_) {
    return {};
  }

  std::uint64_t generation = nextDirectPlaybackGeneration_++;
  if (generation == 0) {
    generation = nextDirectPlaybackGeneration_++;
  }
  directPlaybackGenerations_[static_cast<std::size_t>(channel)] = generation;
  return {.channel = channel, .generation = generation};
}

int AudioEngine::PlaySoundFromBytes(
    const std::vector<std::uint8_t>& raw_wav, const float volume,
    const bool loop,
    const std::optional<PlaybackChannel> playback_channel) {
  return PlaySoundFromBytesTracked(raw_wav, volume, loop, playback_channel).channel;
}

AudioDirectPlaybackHandle AudioEngine::PlaySoundFromBytesTracked(
    const std::vector<std::uint8_t>& raw_wav, const float volume,
    const bool loop,
    const std::optional<PlaybackChannel> playback_channel) {
  if (!initialized_) return {};

  if (DetectAudioFormat(raw_wav) == AudioFormat::MP3) {
    auto decoder = std::make_unique<Mp3Decoder>();
    if (!decoder->OpenOwned(raw_wav)) {
      return {};
    }
    return StartStreamingSound(std::move(decoder), volume, loop,
                               playback_channel, -1);
  }

  auto decoded = DecodeAudio(raw_wav);
  if (!decoded.has_value()) {
    decoded = DecodeWav(raw_wav);
  }
  if (!decoded.has_value()) return {};

  SoundData sound = Normalize(decoded.value(),
                              static_cast<std::uint32_t>(output_rate_),
                              static_cast<std::uint8_t>(output_channels_));

  SDL_LockAudioDevice(device_);
  const int ch_idx = AllocateChannel();
  if (ch_idx < 0) {
    SDL_UnlockAudioDevice(device_);
    return {};
  }
  const auto handle = ClaimChannelInstance(ch_idx);
  auto& ch = sfx_[static_cast<std::size_t>(ch_idx)];
  auto replaced_stream = std::move(ch.pcm_stream);
  ch = {};
  ch.data = std::make_shared<const SoundData>(std::move(sound));
  ch.position = 0;
  ch.volume = std::clamp(volume, 0.0f, 1.0f);
  ch.loop = loop;
  ch.active = true;
  ch.uses_playback_channel = playback_channel.has_value();
  ch.playback_channel = playback_channel.value_or(PlaybackChannel::SFX);
  ch.is_3d = false;
  ch.computed_volume = ch.volume;
  sfx_fades_[static_cast<std::size_t>(ch_idx)] = {};
  SDL_UnlockAudioDevice(device_);
  replaced_stream.reset();

  return handle;
}

int AudioEngine::PlaySound(const std::string& path, float volume, bool loop,
                           const int priority,
                           const std::optional<PlaybackChannel> playback_channel) {
  if (!initialized_) return -1;

  if (HasMp3Extension(path)) {
    if (!vfs_loader_) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                         "PlaySound: no VFS loader set, path=" + path);
      return -1;
    }
    auto encoded = vfs_loader_(path);
    if (!encoded.has_value() || encoded->empty()) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                         "PlaySound: VFS returned no data for path=" + path);
      return -1;
    }
    if (DetectAudioFormat(*encoded) != AudioFormat::MP3) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                         "PlaySound: invalid MP3 data for path=" + path);
      return -1;
    }

    const std::size_t encoded_size = encoded->size();
    auto decoder = std::make_unique<Mp3Decoder>();
    if (!decoder->OpenOwned(std::move(*encoded))) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                         "PlaySound: MP3 stream creation failed for path=" + path);
      return -1;
    }

    const int channel =
        StartStreamingSound(std::move(decoder), volume, loop,
                            playback_channel, priority).channel;
    if (channel >= 0) {
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kInfo,
          "AudioEngine: queued streaming voice path=" + path +
              " compressed_bytes=" + std::to_string(encoded_size) +
              " channel=" + std::to_string(channel));
    }
    return channel;
  }

  SDL_LockAudioDevice(device_);
  const int ch_idx = AllocateChannel(priority);
  if (ch_idx < 0) {
    SDL_UnlockAudioDevice(device_);
    return -1;
  }
  const auto handle = ClaimChannelInstance(ch_idx);
  auto& ch = sfx_[static_cast<std::size_t>(ch_idx)];
  auto replaced_stream = std::move(ch.pcm_stream);
  ch = {};
  ch.position = 0;
  ch.volume = std::clamp(volume, 0.0f, 1.0f);
  ch.loop = loop;
  ch.active = true;
  ch.paused = true;
  ch.uses_playback_channel = playback_channel.has_value();
  ch.playback_channel = playback_channel.value_or(PlaybackChannel::SFX);
  ch.is_3d = false;
  ch.computed_volume = ch.volume;
  sfx_fades_[static_cast<std::size_t>(ch_idx)] = {};
  SDL_UnlockAudioDevice(device_);
  replaced_stream.reset();

  const std::uint64_t generation = handle.generation;
  LoadAndDecodeAsync(
      path,
      [this, ch_idx, generation](std::shared_ptr<const SoundData> decoded) {
        SDL_LockAudioDevice(device_);

        if (directPlaybackGenerations_[static_cast<std::size_t>(ch_idx)] ==
            generation) {
          auto& target = sfx_[static_cast<std::size_t>(ch_idx)];
          if (decoded != nullptr) {

            target.data = std::move(decoded);
            target.paused = false;
            RestartHandleFadeInOnChannelReadyLocked(ch_idx, generation);
          } else {
            target.active = false;
          }
        }
        SDL_UnlockAudioDevice(device_);
      });

  return ch_idx;
}

AudioDirectPlaybackHandle AudioEngine::StartStreamingSound(
    std::unique_ptr<IAudioDecoder> decoder, const float volume,
    const bool loop,
    const std::optional<PlaybackChannel> playback_channel,
    const int priority) {
  auto stream = MusicPcmStream::Create(std::move(decoder), output_rate_, output_channels_,
                                       output_buffer_size_, loop);
  if (!stream) {
    return {};
  }

  std::shared_ptr<MusicPcmStream> shared_stream(std::move(stream));
  SDL_LockAudioDevice(device_);
  const int channel_index = AllocateChannel(priority);
  if (channel_index < 0) {
    SDL_UnlockAudioDevice(device_);
    shared_stream->RequestStop();
    return {};
  }
  const auto handle = ClaimChannelInstance(channel_index);
  auto& channel = sfx_[static_cast<std::size_t>(channel_index)];
  auto replaced_stream = std::move(channel.pcm_stream);
  channel = {};
  channel.pcm_stream = std::move(shared_stream);
  channel.volume = std::clamp(volume, 0.0f, 1.0f);
  channel.loop = loop;
  channel.active = true;
  channel.uses_playback_channel = playback_channel.has_value();
  channel.playback_channel = playback_channel.value_or(PlaybackChannel::SFX);
  channel.is_3d = false;
  channel.computed_volume = channel.volume;
  sfx_fades_[static_cast<std::size_t>(channel_index)] = {};
  SDL_UnlockAudioDevice(device_);
  replaced_stream.reset();
  return handle;
}

int AudioEngine::PlaySound3DFromBytes(const std::vector<std::uint8_t>& raw_wav,
                                      float x, float y, float z,
                                      float volume, float max_distance) {
  if (!initialized_) return -1;

  auto decoded = DecodeAudio(raw_wav);
  if (!decoded.has_value()) {
    decoded = DecodeWav(raw_wav);
  }
  if (!decoded.has_value()) return -1;

  SoundData sound = Normalize(decoded.value(),
                              static_cast<std::uint32_t>(output_rate_),
                              static_cast<std::uint8_t>(output_channels_));

  SDL_LockAudioDevice(device_);
  const int ch_idx = AllocateChannel();
  if (ch_idx < 0) {
    SDL_UnlockAudioDevice(device_);
    return -1;
  }
  (void)ClaimChannelInstance(ch_idx);
  auto& ch = sfx_[static_cast<std::size_t>(ch_idx)];
  auto replaced_stream = std::move(ch.pcm_stream);
  ch = {};
  ch.data = std::make_shared<const SoundData>(std::move(sound));
  ch.position = 0;
  ch.volume = std::clamp(volume, 0.0f, 1.0f);
  ch.loop = false;
  ch.active = true;
  ch.uses_playback_channel = false;
  ch.playback_channel = PlaybackChannel::SFX;
  ch.is_3d = true;
  ch.x = x;
  ch.y = y;
  ch.z = z;
  ch.max_distance = std::max(max_distance, 1.0f);

  UpdateSpatializationLocked(ch);
  sfx_fades_[static_cast<std::size_t>(ch_idx)] = {};
  SDL_UnlockAudioDevice(device_);
  replaced_stream.reset();

  return ch_idx;
}

int AudioEngine::PlaySound3D(const std::string& path,
                             float x, float y, float z,
                             float volume, float max_distance) {
  if (!initialized_) return -1;

  SDL_LockAudioDevice(device_);
  const int ch_idx = AllocateChannel();
  if (ch_idx < 0) {
    SDL_UnlockAudioDevice(device_);
    return -1;
  }
  const auto handle = ClaimChannelInstance(ch_idx);
  auto& ch = sfx_[static_cast<std::size_t>(ch_idx)];
  auto replaced_stream = std::move(ch.pcm_stream);
  ch = {};
  ch.position = 0;
  ch.volume = std::clamp(volume, 0.0f, 1.0f);
  ch.loop = false;
  ch.active = true;
  ch.paused = true;
  ch.uses_playback_channel = false;
  ch.playback_channel = PlaybackChannel::SFX;
  ch.is_3d = true;
  ch.x = x;
  ch.y = y;
  ch.z = z;
  ch.max_distance = std::max(max_distance, 1.0f);

  UpdateSpatializationLocked(ch);
  sfx_fades_[static_cast<std::size_t>(ch_idx)] = {};
  SDL_UnlockAudioDevice(device_);
  replaced_stream.reset();

  const std::uint64_t generation = handle.generation;
  LoadAndDecodeAsync(
      path,
      [this, ch_idx, generation](std::shared_ptr<const SoundData> decoded) {
        SDL_LockAudioDevice(device_);
        if (directPlaybackGenerations_[static_cast<std::size_t>(ch_idx)] ==
            generation) {
          auto& target = sfx_[static_cast<std::size_t>(ch_idx)];
          if (decoded != nullptr) {
            target.data = std::move(decoded);
            target.paused = false;
            RestartHandleFadeInOnChannelReadyLocked(ch_idx, generation);
          } else {
            target.active = false;
          }
        }
        SDL_UnlockAudioDevice(device_);
      });

  return ch_idx;
}

void AudioEngine::StopSound(const int channel, const float fade_out_ms) {
  if (!initialized_ || channel < 0 || channel >= max_sfx_channels_) return;
  SDL_LockAudioDevice(device_);
  const std::size_t index = static_cast<std::size_t>(channel);
  auto retired_stream = StopSfxChannel(sfx_[index], sfx_fades_[index], fade_out_ms);
  SDL_UnlockAudioDevice(device_);
  retired_stream.reset();
}

void AudioEngine::StopSound(const AudioDirectPlaybackHandle handle,
                            const float fade_out_ms) {
  if (!initialized_ || device_ == 0 || !handle.IsValid() ||
      handle.channel >= max_sfx_channels_) {
    return;
  }

  std::shared_ptr<MusicPcmStream> retired_stream;
  SDL_LockAudioDevice(device_);
  const auto index = static_cast<std::size_t>(handle.channel);
  if (directPlaybackGenerations_[index] == handle.generation) {
    retired_stream = StopSfxChannel(sfx_[index], sfx_fades_[index], fade_out_ms);
  }
  SDL_UnlockAudioDevice(device_);
  retired_stream.reset();
}

bool AudioEngine::IsSoundChannelActive(const int channel) const {
  if (!initialized_ || device_ == 0 || channel < 0 ||
      channel >= max_sfx_channels_) {
    return false;
  }
  SDL_LockAudioDevice(device_);
  const bool active = sfx_[static_cast<std::size_t>(channel)].active;
  SDL_UnlockAudioDevice(device_);
  return active;
}

bool AudioEngine::IsSoundChannelActive(
    const AudioDirectPlaybackHandle handle) const {
  if (!initialized_ || device_ == 0 || !handle.IsValid() ||
      handle.channel >= max_sfx_channels_) {
    return false;
  }

  SDL_LockAudioDevice(device_);
  const auto index = static_cast<std::size_t>(handle.channel);
  const bool active = directPlaybackGenerations_[index] == handle.generation &&
                      sfx_[index].active;
  SDL_UnlockAudioDevice(device_);
  return active;
}

void AudioEngine::StopAllSounds(const float fade_out_ms) {
  if (!initialized_) return;
  std::vector<std::shared_ptr<MusicPcmStream>> retired_streams;
  retired_streams.reserve(sfx_.size());
  SDL_LockAudioDevice(device_);
  for (std::size_t index = 0; index < sfx_.size(); ++index) {
    if (auto stream = StopSfxChannel(sfx_[index], sfx_fades_[index], fade_out_ms)) {
      retired_streams.push_back(std::move(stream));
    }
  }
  SDL_UnlockAudioDevice(device_);
  retired_streams.clear();
}

void AudioEngine::StopNonVoiceSounds(const float fade_out_ms) {
  if (!initialized_) {
    return;
  }

  std::vector<std::shared_ptr<MusicPcmStream>> retired_streams;
  retired_streams.reserve(sfx_.size());
  SDL_LockAudioDevice(device_);
  for (std::size_t index = 0; index < sfx_.size(); ++index) {
    auto& channel = sfx_[index];
    if (channel.uses_playback_channel &&
        channel.playback_channel == PlaybackChannel::Voice) {
      continue;
    }
    if (auto stream = StopSfxChannel(channel, sfx_fades_[index], fade_out_ms)) {
      retired_streams.push_back(std::move(stream));
    }
  }
  SDL_UnlockAudioDevice(device_);
  retired_streams.clear();

  StopMusic(fade_out_ms);
  StopAmbience(fade_out_ms);
  StopCredits(fade_out_ms);
  for (auto& [id, entry] : handles_) {
    if (entry.clip.channel == PlaybackChannel::Voice ||
        entry.state == AudioPlaybackState::Stopped) {
      continue;
    }
    if (entry.sdlChannel >= 0 && entry.sdlGeneration != 0) {
      StopSound(AudioDirectPlaybackHandle{entry.sdlChannel,
                                          entry.sdlGeneration}, fade_out_ms);
    }
    entry.state = AudioPlaybackState::Stopped;
  }
}

void AudioEngine::SetSoundVolume(float volume) {
  sfx_volume_.store(std::clamp(volume, 0.0f, 1.0f), std::memory_order_relaxed);
}

}
