#include "openwow/audio/adapters/sdl/audio_engine_internal.h"

namespace openwow::audio {

bool AudioEngine::QueueVoicePcm(
    const std::uint32_t stream_index, const std::int16_t *samples,
    const std::size_t sample_count, const int sample_rate, const int channels,
    const float volume, const double playback_rate) {
  if (!initialized_ || device_ == 0 || samples == nullptr || sample_count == 0u ||
      sample_rate <= 0 || channels <= 0 ||
      stream_index >= external_voice_streams_.size() ||
      !std::isfinite(playback_rate)) {
    return false;
  }

  const std::size_t input_frames = sample_count / static_cast<std::size_t>(channels);
  if (input_frames == 0u) {
    return false;
  }
  const double clamped_rate = std::clamp(playback_rate, 0.9, 1.1);
  const int playback_rate_hz =
      voice_output_device_ != 0 ? voice_output_rate_ : output_rate_;
  const int playback_channels =
      voice_output_device_ != 0 ? voice_output_channels_ : output_channels_;
  const double source_frames_per_output_frame =
      static_cast<double>(sample_rate) * clamped_rate /
      static_cast<double>(playback_rate_hz);
  const std::size_t output_frames = static_cast<std::size_t>(std::ceil(
      static_cast<double>(input_frames) / source_frames_per_output_frame));
  const std::size_t output_sample_count =
      output_frames * static_cast<std::size_t>(playback_channels);
  if (output_sample_count == 0u ||
      output_sample_count > ExternalVoiceStream::kRingSampleCapacity) {
    return false;
  }

  std::vector<std::int16_t> converted(output_sample_count);
  for (std::size_t output_frame = 0; output_frame < output_frames;
       ++output_frame) {
    const double source_position =
        static_cast<double>(output_frame) * source_frames_per_output_frame;
    const std::size_t source_frame = std::min(
        static_cast<std::size_t>(source_position), input_frames - 1u);
    const std::size_t next_source_frame =
        std::min(source_frame + 1u, input_frames - 1u);
    const float fraction =
        static_cast<float>(source_position - static_cast<double>(source_frame));
    for (int output_channel = 0; output_channel < playback_channels;
         ++output_channel) {
      const int source_channel = channels == 1
                                     ? 0
                                     : std::min(output_channel, channels - 1);
      const auto current = samples[source_frame * static_cast<std::size_t>(channels) +
                                   static_cast<std::size_t>(source_channel)];
      const auto next = samples[next_source_frame * static_cast<std::size_t>(channels) +
                                static_cast<std::size_t>(source_channel)];
      converted[output_frame * static_cast<std::size_t>(playback_channels) +
                static_cast<std::size_t>(output_channel)] =
          static_cast<std::int16_t>(std::clamp(
              static_cast<float>(current) +
                  (static_cast<float>(next) - static_cast<float>(current)) * fraction,
              -32768.0f, 32767.0f));
    }
  }

  const auto playback_device =
      voice_output_device_ != 0 ? voice_output_device_ : device_;
  SDL_LockAudioDevice(playback_device);
  auto &voice = external_voice_streams_[stream_index];
  if (voice.samples.empty()) {
    voice.samples.resize(ExternalVoiceStream::kRingSampleCapacity);
  }
  if (output_sample_count >
      ExternalVoiceStream::kRingSampleCapacity - voice.sample_count) {
    SDL_UnlockAudioDevice(playback_device);
    return false;
  }
  voice.volume = std::clamp(volume, 0.0f, 1.0f);
  for (const auto sample : converted) {
    voice.samples[voice.write_cursor] = sample;
    voice.write_cursor =
        (voice.write_cursor + 1u) % ExternalVoiceStream::kRingSampleCapacity;
  }
  voice.sample_count += output_sample_count;
  SDL_UnlockAudioDevice(playback_device);
  return true;
}

void AudioEngine::SetVoicePcmStreamVolume(
    const std::uint32_t stream_index, const float volume) {
  if (!initialized_ || device_ == 0 ||
      stream_index >= external_voice_streams_.size()) {
    return;
  }
  const auto playback_device =
      voice_output_device_ != 0 ? voice_output_device_ : device_;
  SDL_LockAudioDevice(playback_device);
  external_voice_streams_[stream_index].volume =
      std::clamp(volume, 0.0f, 1.0f);
  SDL_UnlockAudioDevice(playback_device);
}

void AudioEngine::ResetVoicePcmStream(const std::uint32_t stream_index) {
  if (!initialized_ || device_ == 0 ||
      stream_index >= external_voice_streams_.size()) {
    return;
  }
  const auto playback_device =
      voice_output_device_ != 0 ? voice_output_device_ : device_;
  SDL_LockAudioDevice(playback_device);
  auto &voice = external_voice_streams_[stream_index];
  voice.read_cursor = 0u;
  voice.write_cursor = 0u;
  voice.sample_count = 0u;
  SDL_UnlockAudioDevice(playback_device);
}

void AudioEngine::ResetAllVoicePcmStreams() {
  if (!initialized_ || device_ == 0) {
    return;
  }
  const auto playback_device =
      voice_output_device_ != 0 ? voice_output_device_ : device_;
  SDL_LockAudioDevice(playback_device);
  for (auto &voice : external_voice_streams_) {
    voice.read_cursor = 0u;
    voice.write_cursor = 0u;
    voice.sample_count = 0u;
  }
  SDL_UnlockAudioDevice(playback_device);
}

std::size_t AudioEngine::GetQueuedVoicePcmSampleCount(
    const std::uint32_t stream_index) const {
  if (!initialized_ || device_ == 0 ||
      stream_index >= external_voice_streams_.size()) {
    return 0u;
  }
  const auto playback_device =
      voice_output_device_ != 0 ? voice_output_device_ : device_;
  SDL_LockAudioDevice(playback_device);
  const std::size_t count = external_voice_streams_[stream_index].sample_count;
  SDL_UnlockAudioDevice(playback_device);
  return count;
}

void AudioEngine::StartDecodedMusic(std::shared_ptr<const SoundData> sound,
                                    const bool loop, const float fade_in_ms,
                                    const float volume) {
  std::unique_ptr<MusicPcmStream> replaced_stream;
  SDL_LockAudioDevice(device_);
  replaced_stream = std::move(music_stream_);
  music_.data = std::move(sound);
  music_.position = 0;
  music_.volume = std::clamp(volume, 0.0f, 1.0f);
  music_.loop = loop;
  music_.active = true;
  music_.is_3d = false;
  BeginMusicFadeIn(music_fade_, fade_in_ms);
  SDL_UnlockAudioDevice(device_);

  replaced_stream.reset();
}

bool AudioEngine::StartStreamingMusic(std::unique_ptr<IAudioDecoder> decoder,
                                      const bool loop, const float fade_in_ms,
                                      const float volume) {
  const std::uint32_t source_rate = decoder ? decoder->GetSampleRate() : 0;
  const std::uint32_t source_channels = decoder ? decoder->GetChannels() : 0;
  auto stream = MusicPcmStream::Create(std::move(decoder), output_rate_, output_channels_,
                                       output_buffer_size_, loop);
  if (!stream) {
    return false;
  }

  const std::size_t capacity_frames = stream->CapacityFrames();
  std::unique_ptr<MusicPcmStream> replaced_stream;
  SDL_LockAudioDevice(device_);
  replaced_stream = std::move(music_stream_);
  music_ = {};
  music_.volume = std::clamp(volume, 0.0f, 1.0f);
  music_stream_ = std::move(stream);
  BeginMusicFadeIn(music_fade_, fade_in_ms);
  SDL_UnlockAudioDevice(device_);
  replaced_stream.reset();

  openwow::diagnostics::Log(
      openwow::diagnostics::LogLevel::kDebug,
      "Music stream scheduled: source_rate=" + std::to_string(source_rate) +
          " source_channels=" + std::to_string(source_channels) +
          " output_rate=" + std::to_string(output_rate_) +
          " output_channels=" + std::to_string(output_channels_) +
          " pcm_capacity_frames=" + std::to_string(capacity_frames));
  return true;
}

bool AudioEngine::StartStreamingCredits(std::unique_ptr<IAudioDecoder> decoder,
                                        const bool loop,
                                        const float fade_in_ms,
                                        const float volume) {
  auto stream = MusicPcmStream::Create(std::move(decoder), output_rate_,
                                       output_channels_, output_buffer_size_, loop);
  if (!stream) {
    return false;
  }

  std::unique_ptr<MusicPcmStream> replaced_stream;
  SDL_LockAudioDevice(device_);
  replaced_stream = std::move(credits_stream_);
  credits_ = {};
  credits_.volume = std::clamp(volume, 0.0f, 1.0f);
  credits_stream_ = std::move(stream);
  BeginMusicFadeIn(credits_fade_, fade_in_ms);
  SDL_UnlockAudioDevice(device_);
  replaced_stream.reset();
  return true;
}

bool AudioEngine::PlayMusicFromBytes(const std::vector<std::uint8_t>& raw_wav,
                                     const bool loop, const float fade_in_ms,
                                     const float volume) {
  if (!initialized_) return false;

  if (DetectAudioFormat(raw_wav) == AudioFormat::MP3) {
    auto decoder = std::make_unique<Mp3Decoder>();
    if (!decoder->Open(raw_wav)) {
      return false;
    }
    return StartStreamingMusic(std::move(decoder), loop, fade_in_ms, volume);
  }

  auto decoded = DecodeAudio(raw_wav);
  if (!decoded.has_value()) {
    decoded = DecodeWav(raw_wav);
  }
  if (!decoded.has_value()) return false;

  SoundData sound = Normalize(decoded.value(),
                              static_cast<std::uint32_t>(output_rate_),
                              static_cast<std::uint8_t>(output_channels_));
  StartDecodedMusic(std::make_shared<const SoundData>(std::move(sound)), loop,
                    fade_in_ms, volume);
  return true;
}

bool AudioEngine::PlayMusic(const std::string& path, const bool loop,
                            const float fade_in_ms, const float volume) {
  if (!initialized_) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
        "PlayMusic: engine not initialized, path=" + path);
    return false;
  }

  if (HasMp3Extension(path)) {
    if (!vfs_loader_) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                         "PlayMusic: no VFS loader set, path=" + path);
      return false;
    }

    const auto start = std::chrono::steady_clock::now();
    auto bytes = vfs_loader_(path);
    if (!bytes.has_value() || bytes->empty()) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                         "PlayMusic: VFS returned no data for path=" + path);
      return false;
    }
    if (DetectAudioFormat(*bytes) != AudioFormat::MP3) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                         "PlayMusic: invalid MP3 data for path=" + path);
      return false;
    }

    const std::size_t compressed_bytes = bytes->size();
    auto decoder = std::make_unique<Mp3Decoder>();
    if (!decoder->OpenOwned(std::move(*bytes)) ||
        !StartStreamingMusic(std::move(decoder), loop, fade_in_ms, volume)) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                         "PlayMusic: MP3 stream creation failed for path=" + path);
      return false;
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start);
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                       "PlayMusic: streaming path=" + path +
                           " compressed_bytes=" + std::to_string(compressed_bytes) +
                           " caller_ms=" +
                           std::to_string(static_cast<double>(elapsed.count()) / 1000.0) +
                           " loop=" + std::to_string(loop) +
                           " fade_in_ms=" + std::to_string(fade_in_ms));
    return true;
  }

  auto data = LoadAndDecode(path);
  if (data == nullptr) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
        "PlayMusic: LoadAndDecode failed for path=" + path);
    return false;
  }

  const auto total_frames = data->TotalFrames();
  StartDecodedMusic(std::move(data), loop, fade_in_ms, volume);
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "PlayMusic: decoded short asset path=" + path +
                         " frames=" + std::to_string(total_frames));
  return true;
}

void AudioEngine::StopMusic(float fade_out_ms) {
  if (!initialized_) return;

  std::unique_ptr<MusicPcmStream> stopped_stream;
  if (fade_out_ms > 0.0f) {
    SDL_LockAudioDevice(device_);
    if (music_.active || (music_stream_ != nullptr && !music_stream_->IsFinished())) {
      music_fade_.active = true;
      music_fade_.fading_in = false;
      music_fade_.duration_ms = fade_out_ms;
      music_fade_.elapsed_ms = 0.0f;
      music_fade_.start_volume = 1.0f;
      music_fade_.end_volume = 0.0f;
    }
    SDL_UnlockAudioDevice(device_);
  } else {
    SDL_LockAudioDevice(device_);
    stopped_stream = std::move(music_stream_);
    music_.active = false;
    music_.data = {};
    music_.position = 0;
    music_fade_ = {};
    SDL_UnlockAudioDevice(device_);
    stopped_stream.reset();
  }
}

void AudioEngine::SetMusicVolume(float volume) {
  music_volume_.store(std::clamp(volume, 0.0f, 1.0f), std::memory_order_relaxed);
}

bool AudioEngine::IsMusicPlaying() const {
  if (!initialized_ || device_ == 0) {
    return false;
  }
  SDL_LockAudioDevice(device_);
  const bool playing = music_.active ||
                       (music_stream_ != nullptr && !music_stream_->IsFinished());
  SDL_UnlockAudioDevice(device_);
  return playing;
}

std::optional<MusicStreamStats> AudioEngine::GetMusicStreamStats() const {
  if (!initialized_ || device_ == 0) {
    return std::nullopt;
  }
  SDL_LockAudioDevice(device_);
  std::optional<MusicStreamStats> stats;
  if (music_stream_ != nullptr) {
    stats = MusicStreamStats{
        .buffered_frames = music_stream_->BufferedFrames(),
        .capacity_frames = music_stream_->CapacityFrames(),
        .underflow_count = music_stream_->UnderflowCount(),
    };
  }
  SDL_UnlockAudioDevice(device_);
  return stats;
}

bool AudioEngine::PlayAmbienceFromBytes(const std::vector<std::uint8_t>& raw_wav,
                                        float volume, float fade_in_ms) {
  if (!initialized_) return false;

  auto decoded = DecodeAudio(raw_wav);
  if (!decoded.has_value()) {
    decoded = DecodeWav(raw_wav);
  }
  if (!decoded.has_value()) return false;

  SoundData sound = Normalize(decoded.value(),
                              static_cast<std::uint32_t>(output_rate_),
                              static_cast<std::uint8_t>(output_channels_));

  SDL_LockAudioDevice(device_);
  ambience_.data = std::make_shared<const SoundData>(std::move(sound));
  ambience_.position = 0;
  ambience_.volume = std::clamp(volume, 0.0f, 1.0f);
  ambience_.loop = true;
  ambience_.active = true;
  ambience_.is_3d = false;

  if (fade_in_ms > 0.0f) {
    ambience_fade_.active = true;
    ambience_fade_.fading_in = true;
    ambience_fade_.duration_ms = fade_in_ms;
    ambience_fade_.elapsed_ms = 0.0f;
    ambience_fade_.start_volume = 0.0f;
    ambience_fade_.end_volume = 1.0f;
  } else {
    ambience_fade_ = {};
  }
  SDL_UnlockAudioDevice(device_);

  return true;
}

bool AudioEngine::PlayAmbience(const std::string& path, float volume, float fade_in_ms) {
  if (!initialized_) return false;

  auto data = LoadAndDecode(path);
  if (data == nullptr) return false;

  SDL_LockAudioDevice(device_);
  ambience_.data = std::move(data);
  ambience_.position = 0;
  ambience_.volume = std::clamp(volume, 0.0f, 1.0f);
  ambience_.loop = true;
  ambience_.active = true;
  ambience_.is_3d = false;

  if (fade_in_ms > 0.0f) {
    ambience_fade_.active = true;
    ambience_fade_.fading_in = true;
    ambience_fade_.duration_ms = fade_in_ms;
    ambience_fade_.elapsed_ms = 0.0f;
    ambience_fade_.start_volume = 0.0f;
    ambience_fade_.end_volume = 1.0f;
  } else {
    ambience_fade_ = {};
  }
  SDL_UnlockAudioDevice(device_);

  return true;
}

void AudioEngine::StopAmbience(float fade_out_ms) {
  if (!initialized_) return;

  if (fade_out_ms > 0.0f) {
    SDL_LockAudioDevice(device_);
    if (ambience_.active) {
      ambience_fade_.active = true;
      ambience_fade_.fading_in = false;
      ambience_fade_.duration_ms = fade_out_ms;
      ambience_fade_.elapsed_ms = 0.0f;
      ambience_fade_.start_volume = 1.0f;
      ambience_fade_.end_volume = 0.0f;
    }
    SDL_UnlockAudioDevice(device_);
  } else {
    SDL_LockAudioDevice(device_);
    ambience_.active = false;
    ambience_.data = {};
    ambience_.position = 0;
    ambience_fade_ = {};
    SDL_UnlockAudioDevice(device_);
  }
}

void AudioEngine::SetAmbienceVolume(const float volume) {
  ambience_volume_.store(std::clamp(volume, 0.0f, 1.0f), std::memory_order_relaxed);
}

bool AudioEngine::IsAmbiencePlaying() const {
  return initialized_ && ambience_.active;
}

bool AudioEngine::PlayCreditsFromBytes(const std::vector<std::uint8_t>& raw_wav,
                                       const bool loop, const float fade_in_ms,
                                       const float volume) {
  if (!initialized_) return false;

  if (DetectAudioFormat(raw_wav) == AudioFormat::MP3) {
    auto decoder = std::make_unique<Mp3Decoder>();
    if (!decoder->Open(raw_wav)) {
      return false;
    }
    return StartStreamingCredits(std::move(decoder), loop, fade_in_ms, volume);
  }

  auto decoded = DecodeAudio(raw_wav);
  if (!decoded.has_value()) {
    decoded = DecodeWav(raw_wav);
  }
  if (!decoded.has_value()) return false;

  SoundData sound = Normalize(decoded.value(),
                              static_cast<std::uint32_t>(output_rate_),
                              static_cast<std::uint8_t>(output_channels_));

  std::unique_ptr<MusicPcmStream> replaced_stream;
  SDL_LockAudioDevice(device_);
  replaced_stream = std::move(credits_stream_);
  credits_.data = std::make_shared<const SoundData>(std::move(sound));
  credits_.position = 0;
  credits_.volume = std::clamp(volume, 0.0f, 1.0f);
  credits_.loop = loop;
  credits_.active = true;
  credits_.is_3d = false;

  if (fade_in_ms > 0.0f) {
    credits_fade_.active = true;
    credits_fade_.fading_in = true;
    credits_fade_.duration_ms = fade_in_ms;
    credits_fade_.elapsed_ms = 0.0f;
    credits_fade_.start_volume = 0.0f;
    credits_fade_.end_volume = 1.0f;
  } else {
    credits_fade_ = {};
  }
  SDL_UnlockAudioDevice(device_);
  replaced_stream.reset();

  return true;
}

bool AudioEngine::PlayCredits(const std::string& path, const bool loop,
                              const float fade_in_ms, const float volume) {
  if (!initialized_) return false;

  if (HasMp3Extension(path)) {
    if (!vfs_loader_) return false;
    auto bytes = vfs_loader_(path);
    if (!bytes.has_value() || bytes->empty() ||
        DetectAudioFormat(*bytes) != AudioFormat::MP3) {
      return false;
    }
    auto decoder = std::make_unique<Mp3Decoder>();
    if (!decoder->OpenOwned(std::move(*bytes))) {
      return false;
    }
    return StartStreamingCredits(std::move(decoder), loop, fade_in_ms, volume);
  }

  auto data = LoadAndDecode(path);
  if (data == nullptr) return false;

  std::unique_ptr<MusicPcmStream> replaced_stream;
  SDL_LockAudioDevice(device_);
  replaced_stream = std::move(credits_stream_);
  credits_.data = std::move(data);
  credits_.position = 0;
  credits_.volume = std::clamp(volume, 0.0f, 1.0f);
  credits_.loop = loop;
  credits_.active = true;
  credits_.is_3d = false;

  if (fade_in_ms > 0.0f) {
    credits_fade_.active = true;
    credits_fade_.fading_in = true;
    credits_fade_.duration_ms = fade_in_ms;
    credits_fade_.elapsed_ms = 0.0f;
    credits_fade_.start_volume = 0.0f;
    credits_fade_.end_volume = 1.0f;
  } else {
    credits_fade_ = {};
  }
  SDL_UnlockAudioDevice(device_);
  replaced_stream.reset();

  return true;
}

void AudioEngine::StopCredits(float fade_out_ms) {
  if (!initialized_) return;

  if (fade_out_ms > 0.0f) {
    SDL_LockAudioDevice(device_);
    if (credits_.active ||
        (credits_stream_ != nullptr && !credits_stream_->IsFinished())) {
      credits_fade_.active = true;
      credits_fade_.fading_in = false;
      credits_fade_.duration_ms = fade_out_ms;
      credits_fade_.elapsed_ms = 0.0f;
      credits_fade_.start_volume = 1.0f;
      credits_fade_.end_volume = 0.0f;
    }
    SDL_UnlockAudioDevice(device_);
  } else {
    std::unique_ptr<MusicPcmStream> stopped_stream;
    SDL_LockAudioDevice(device_);
    stopped_stream = std::move(credits_stream_);
    credits_.active = false;
    credits_.data = {};
    credits_.position = 0;
    credits_fade_ = {};
    SDL_UnlockAudioDevice(device_);
    stopped_stream.reset();
  }
}

bool AudioEngine::IsCreditsPlaying() const {
  return initialized_ &&
         (credits_.active ||
          (credits_stream_ != nullptr && !credits_stream_->IsFinished()));
}

void AudioEngine::PlayMovieAudioStream(std::shared_ptr<IMovieAudioSource> source,
                                       const float volume) {
  if (!initialized_ || !source || source->SampleRate() != output_rate_ ||
      source->Channels() != output_channels_) {
    return;
  }

  std::shared_ptr<IMovieAudioSource> replaced_stream;
  const bool can_lock = device_ != 0;
  if (can_lock) {
    SDL_LockAudioDevice(device_);
  }
  replaced_stream = std::move(movie_audio_stream_);
  movie_audio_stream_ = std::move(source);
  movie_sample_pos_ = 0;
  movie_sample_rate_ = movie_audio_stream_->SampleRate();
  movie_channels_ = movie_audio_stream_->Channels();
  movie_volume_ = std::clamp(volume, 0.0F, 1.0F);
  movie_audio_playing_ = !movie_audio_stream_->IsDrained();
  if (can_lock) {
    SDL_UnlockAudioDevice(device_);
  }
}

void AudioEngine::StopMovieAudio() {
  std::shared_ptr<IMovieAudioSource> stopped_stream;
  if (!initialized_) {
    movie_audio_stream_.reset();
    movie_sample_pos_ = 0;
    movie_audio_playing_ = false;
    return;
  }
  const bool can_lock = device_ != 0;
  if (can_lock) {
    SDL_LockAudioDevice(device_);
  }
  movie_audio_playing_ = false;
  stopped_stream = std::move(movie_audio_stream_);
  movie_sample_pos_ = 0;
  if (can_lock) {
    SDL_UnlockAudioDevice(device_);
  }
}

bool AudioEngine::IsMovieAudioPlaying() const {
  if (!initialized_) return false;
  const bool can_lock = device_ != 0;
  if (can_lock) {
    SDL_LockAudioDevice(device_);
  }
  const bool playing = movie_audio_playing_;
  if (can_lock) {
    SDL_UnlockAudioDevice(device_);
  }
  return playing;
}

double AudioEngine::MovieAudioTimeSeconds() const {
  if (!initialized_) return 0.0;
  const bool can_lock = device_ != 0;
  if (can_lock) {
    SDL_LockAudioDevice(device_);
  }
  const std::size_t pos = movie_sample_pos_;
  const int rate = movie_sample_rate_;
  const int ch = movie_channels_;
  const bool has_movie = movie_audio_stream_ != nullptr && rate > 0 && ch > 0;
  if (can_lock) {
    SDL_UnlockAudioDevice(device_);
  }

  if (!has_movie) return 0.0;
  const double frames = static_cast<double>(pos) / static_cast<double>(ch);
  return frames / static_cast<double>(rate);
}

std::optional<AudioEngine::MovieAudioStateSnapshot> AudioEngine::CaptureMovieAudioState() const {
  const bool can_lock = initialized_ && device_ != 0;
  if (can_lock) {
    SDL_LockAudioDevice(device_);
  }

  const MovieAudioStateSnapshot snapshot{
      .stream = movie_audio_stream_,
      .sample_pos = movie_sample_pos_,
      .sample_rate = movie_sample_rate_,
      .channels = movie_channels_,
      .volume = movie_volume_,
      .playing = movie_audio_playing_,
  };

  if (can_lock) {
    SDL_UnlockAudioDevice(device_);
  }

  if (!snapshot.playing || snapshot.stream == nullptr ||
      snapshot.sample_rate <= 0 || snapshot.channels <= 0) {
    return std::nullopt;
  }

  return snapshot;
}

void AudioEngine::RestoreMovieAudioState(const MovieAudioStateSnapshot& state) {
  if (!initialized_ || state.stream == nullptr ||
      state.sample_rate != output_rate_ || state.channels != output_channels_) {
    return;
  }

  std::shared_ptr<IMovieAudioSource> replaced_stream;
  const bool can_lock = device_ != 0;
  if (can_lock) {
    SDL_LockAudioDevice(device_);
  }

  replaced_stream = std::move(movie_audio_stream_);
  movie_audio_stream_ = state.stream;
  movie_sample_pos_ = state.sample_pos;
  movie_sample_rate_ = state.sample_rate > 0 ? state.sample_rate : 44100;
  movie_channels_ = state.channels > 0 ? state.channels : 2;
  movie_volume_ = std::clamp(state.volume, 0.0f, 1.0f);
  movie_audio_playing_ = state.playing && !state.stream->IsDrained();

  if (can_lock) {
    SDL_UnlockAudioDevice(device_);
  }
}

}
