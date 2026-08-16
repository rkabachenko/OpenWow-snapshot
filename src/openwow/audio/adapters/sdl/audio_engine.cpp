#include "openwow/audio/adapters/sdl/audio_engine_internal.h"

namespace openwow::audio {

AudioEngineState::~AudioEngineState() = default;

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine() {
  Shutdown();
}

bool AudioEngine::Initialize(const int sample_rate, const int channels,
                             const int buffer_size,
                             const std::string_view output_device_name) {
  if (sample_rate <= 0 || channels <= 0 || channels > 8 ||
      buffer_size <= 0 || buffer_size > std::numeric_limits<Uint16>::max()) {
    return false;
  }

  const std::string requested_device_name(output_device_name);
  if (initialized_ && output_rate_ == sample_rate &&
      output_channels_ == channels && output_buffer_size_ == buffer_size &&
      output_device_name_ == requested_device_name) {
    return true;
  }
  if (initialized_) {
    Shutdown();
  }

  if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO)) {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kError,
          std::string("SDL_InitSubSystem(SDL_INIT_AUDIO) failed: ") + SDL_GetError());
      return false;
    }
  }

  SDL_AudioSpec desired{};
  desired.freq = sample_rate;
  desired.format = AUDIO_S16SYS;
  desired.channels = static_cast<Uint8>(channels);
  desired.samples = static_cast<Uint16>(buffer_size);
  desired.callback = &AudioEngine::AudioCallback;
  desired.userdata = this;

  SDL_AudioSpec obtained{};
  device_ = SDL_OpenAudioDevice(
      requested_device_name.empty() ? nullptr : requested_device_name.c_str(),
      0, &desired, &obtained,
      SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
  if (device_ == 0) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kError,
        std::string("SDL_OpenAudioDevice failed: ") + SDL_GetError());
    return false;
  }

  output_rate_ = obtained.freq;
  output_channels_ = obtained.channels;
  output_buffer_size_ = obtained.samples;
  output_device_name_ = requested_device_name;

  ClearCacheForOutputFormat();
  const auto callback_sample_capacity = static_cast<std::size_t>(output_buffer_size_) *
                                        static_cast<std::size_t>(output_channels_);
  callback_mix_buffer_.assign(callback_sample_capacity, 0);
  world_reverb_input_.resize(callback_sample_capacity);
  world_reverb_output_.resize(callback_sample_capacity);
  world_reverb_initialized_ = false;

  {
    const char* driver = SDL_GetCurrentAudioDriver();
    std::string fmt_str = "0x" + std::to_string(static_cast<int>(obtained.format));
    if (obtained.format == AUDIO_S16SYS) fmt_str = "S16SYS";
    else if (obtained.format == AUDIO_F32SYS) fmt_str = "F32SYS";
    else if (obtained.format == AUDIO_U8) fmt_str = "U8";

    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
        "AudioEngine::Initialize OK — driver=" + std::string(driver ? driver : "(null)") +
        " freq=" + std::to_string(obtained.freq) +
        " channels=" + std::to_string(obtained.channels) +
        " format=" + fmt_str +
        " samples=" + std::to_string(obtained.samples) +
        " device_id=" + std::to_string(device_));
  }

  music_stream_.reset();
  credits_stream_.reset();
  music_ = {};
  ambience_ = {};
  credits_ = {};
  for (auto& ch : sfx_) ch = {};
  music_fade_ = {};
  ambience_fade_ = {};
  credits_fade_ = {};
  sfx_fades_.fill({});
  movie_audio_stream_.reset();
  movie_sample_pos_ = 0;
  movie_sample_rate_ = output_rate_;
  movie_channels_ = output_channels_;
  movie_volume_ = 1.0f;
  movie_audio_playing_ = false;
  for (auto &stream : external_voice_streams_) {
    stream.read_cursor = 0;
    stream.write_cursor = 0;
    stream.sample_count = 0;
    stream.volume = 1.0f;
  }
  handles_.clear();
  directPlaybackGenerations_.fill(0);
  handleIdByChannel_.fill(kNoOwningHandleId);
  nextHandleId_ = 1;
  nextDirectPlaybackGeneration_ = 1;

  SDL_PauseAudioDevice(device_, 0);

  initialized_ = true;
  return true;
}

bool AudioEngine::ReopenOutputDevicePreservingMovieAudio(
    const std::string_view output_device_name) {
  const auto movie = CaptureMovieAudioState();
  const std::string requested_device_name(output_device_name);

  if (!initialized_) {
    return Initialize(output_rate_, output_channels_, output_buffer_size_,
                      requested_device_name);
  }

  if (device_ != 0) {
    SDL_PauseAudioDevice(device_, 1);
    SDL_CloseAudioDevice(device_);
    device_ = 0;
  }

  music_stream_.reset();
  credits_stream_.reset();
  ResetPlaybackStateAfterDeviceClose(false);

  SDL_AudioSpec desired{};
  desired.freq = output_rate_;
  desired.format = AUDIO_S16SYS;
  desired.channels = static_cast<Uint8>(output_channels_);
  desired.samples = static_cast<Uint16>(output_buffer_size_);
  desired.callback = &AudioEngine::AudioCallback;
  desired.userdata = this;

  SDL_AudioSpec obtained{};
  device_ = SDL_OpenAudioDevice(
      requested_device_name.empty() ? nullptr : requested_device_name.c_str(),
      0, &desired, &obtained,
      SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
  if (device_ == 0) {

    CloseVoiceOutputDevice();
    movie_audio_stream_.reset();
    movie_audio_playing_ = false;
    initialized_ = false;
    output_device_name_.clear();
    return false;
  }

  output_rate_ = obtained.freq;
  output_channels_ = obtained.channels;
  output_buffer_size_ = obtained.samples;
  output_device_name_ = requested_device_name;

  ClearCacheForOutputFormat();
  const auto callback_sample_capacity = static_cast<std::size_t>(output_buffer_size_) *
                                        static_cast<std::size_t>(output_channels_);
  callback_mix_buffer_.assign(callback_sample_capacity, 0);
  world_reverb_input_.resize(callback_sample_capacity);
  world_reverb_output_.resize(callback_sample_capacity);
  world_reverb_initialized_ = false;

  if (voice_output_device_ == 0) {
    for (auto &stream : external_voice_streams_) {
      stream.read_cursor = 0;
      stream.write_cursor = 0;
      stream.sample_count = 0;
    }
  }
  movie_audio_stream_.reset();
  movie_sample_pos_ = 0;
  movie_sample_rate_ = 44100;
  movie_channels_ = 2;
  movie_volume_ = 1.0f;
  movie_audio_playing_ = false;

  if (movie.has_value()) {
    RestoreMovieAudioState(*movie);
  }

  SDL_PauseAudioDevice(device_, 0);
  initialized_ = true;
  return true;
}

bool AudioEngine::OpenVoiceOutputDevice(
    const std::string_view output_device_name) {
  if (!initialized_ || device_ == 0) {
    return false;
  }
  CloseVoiceOutputDevice();

  const std::string requested_name(output_device_name);
  SDL_AudioSpec desired{};
  desired.freq = output_rate_;
  desired.format = AUDIO_S16SYS;
  desired.channels = static_cast<Uint8>(output_channels_);
  desired.samples = static_cast<Uint16>(output_buffer_size_);
  desired.callback = &AudioEngine::VoiceAudioCallback;
  desired.userdata = this;

  SDL_AudioSpec obtained{};
  const SDL_AudioDeviceID opened_device = SDL_OpenAudioDevice(
      requested_name.empty() || requested_name == "System Default"
          ? nullptr
          : requested_name.c_str(),
      0, &desired, &obtained,
      SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
  if (opened_device == 0) {
    return false;
  }

  SDL_LockAudioDevice(device_);
  for (auto &stream : external_voice_streams_) {
    stream.read_cursor = 0u;
    stream.write_cursor = 0u;
    stream.sample_count = 0u;
  }
  voice_output_rate_ = obtained.freq;
  voice_output_channels_ = obtained.channels;
  voice_callback_mix_buffer_.assign(
      static_cast<std::size_t>(obtained.samples) * obtained.channels, 0);
  voice_output_device_ = opened_device;
  SDL_UnlockAudioDevice(device_);
  SDL_PauseAudioDevice(opened_device, 0);
  return true;
}

void AudioEngine::CloseVoiceOutputDevice() {
  if (voice_output_device_ != 0) {
    const SDL_AudioDeviceID closing_device = voice_output_device_;
    SDL_PauseAudioDevice(closing_device, 1);
    SDL_CloseAudioDevice(closing_device);
    if (device_ != 0) {
      SDL_LockAudioDevice(device_);
    }
    voice_output_device_ = 0;
    voice_output_rate_ = output_rate_;
    voice_output_channels_ = output_channels_;
    voice_callback_mix_buffer_.clear();
    for (auto &stream : external_voice_streams_) {
      stream.read_cursor = 0u;
      stream.write_cursor = 0u;
      stream.sample_count = 0u;
    }
    if (device_ != 0) {
      SDL_UnlockAudioDevice(device_);
    }
    return;
  }
  voice_output_rate_ = output_rate_;
  voice_output_channels_ = output_channels_;
  voice_callback_mix_buffer_.clear();
  if (device_ != 0) {
    SDL_LockAudioDevice(device_);
  }
  for (auto &stream : external_voice_streams_) {
    stream.read_cursor = 0u;
    stream.write_cursor = 0u;
    stream.sample_count = 0u;
  }
  if (device_ != 0) {
    SDL_UnlockAudioDevice(device_);
  }
}

void AudioEngine::Shutdown() {

  if (decode_workers_ != nullptr) {
    decode_workers_->Shutdown();
    decode_workers_.reset();
  }

  if (!initialized_) {
    music_stream_.reset();
    credits_stream_.reset();
    movie_audio_stream_.reset();
    ResetPlaybackStateAfterDeviceClose(true);
    output_device_name_.clear();
    return;
  }

  CloseVoiceOutputDevice();
  if (device_ != 0) {
    SDL_PauseAudioDevice(device_, 1);
    SDL_CloseAudioDevice(device_);
    device_ = 0;
  }

  music_stream_.reset();
  credits_stream_.reset();
  ResetPlaybackStateAfterDeviceClose(true);
  for (auto &stream : external_voice_streams_) {
    stream.samples.clear();
    stream.read_cursor = 0;
    stream.write_cursor = 0;
    stream.sample_count = 0;
    stream.volume = 1.0f;
  }

  movie_audio_playing_ = false;
  movie_audio_stream_.reset();
  movie_sample_pos_ = 0;
  movie_sample_rate_ = output_rate_;
  movie_channels_ = output_channels_;
  movie_volume_ = 1.0f;

  {
    std::lock_guard lock(cache_mutex_);
    cache_.clear();
  }

  initialized_ = false;
  world_reverb_initialized_ = false;
  output_device_name_.clear();
}

void AudioEngine::ResetPlaybackStateAfterDeviceClose(
    const bool destroy_handles) {
  music_ = {};
  ambience_ = {};
  credits_ = {};
  for (auto &channel : sfx_) {
    channel = {};
  }
  music_fade_ = {};
  ambience_fade_ = {};
  credits_fade_ = {};
  sfx_fades_.fill({});
  directPlaybackGenerations_.fill(0);

  handleIdByChannel_.fill(kNoOwningHandleId);

  if (destroy_handles) {
    handles_.clear();
    nextHandleId_ = 1;
    nextDirectPlaybackGeneration_ = 1;
    return;
  }

  for (auto &[id, handle] : handles_) {
    (void)id;
    handle.state = AudioPlaybackState::Stopped;
    handle.sdlChannel = -1;
    handle.sdlGeneration = 0;
  }
}

bool AudioEngine::IsInitialized() const {
  return initialized_;
}

void AudioEngine::AudioCallback(void* userdata, std::uint8_t* stream, int len) {
  if (userdata == nullptr || stream == nullptr || len <= 0) return;
  auto* engine = static_cast<AudioEngine*>(userdata);

  const int bytes_per_sample = 2;
  const int total_samples = len / bytes_per_sample;
  const int frames = total_samples / engine->output_channels_;

  std::memset(stream, 0, static_cast<std::size_t>(len));

  engine->MixOutput(reinterpret_cast<std::int16_t*>(stream), frames);
}

void AudioEngine::VoiceAudioCallback(void *userdata, std::uint8_t *stream,
                                     const int len) {
  if (userdata == nullptr || stream == nullptr || len <= 0) {
    return;
  }
  auto *const engine = static_cast<AudioEngine *>(userdata);
  const int total_samples = len / static_cast<int>(sizeof(std::int16_t));
  const int frames = total_samples / engine->voice_output_channels_;
  std::memset(stream, 0, static_cast<std::size_t>(len));
  engine->MixVoiceOutput(reinterpret_cast<std::int16_t *>(stream), frames);
}

void AudioEngine::MixOutput(std::int16_t* output, int frames) {
  const bool muted = muted_.load(std::memory_order_relaxed);
  const float configured_master = master_volume_.load(std::memory_order_relaxed);
  const bool silent = muted || configured_master <= 0.0F;
  const float master = silent ? 0.0F : configured_master;

  const int total_samples = frames * output_channels_;

  constexpr int kStackLimit = 8192;
  std::int32_t stack_buf[kStackLimit];
  std::int32_t* mix = stack_buf;
  if (total_samples > kStackLimit) {
    if (static_cast<std::size_t>(total_samples) > callback_mix_buffer_.size()) {
      std::fill_n(output, total_samples, static_cast<std::int16_t>(0));
      return;
    }
    mix = callback_mix_buffer_.data();
    std::fill_n(mix, total_samples, 0);
  } else {
    std::memset(mix, 0, static_cast<std::size_t>(total_samples) * sizeof(std::int32_t));
  }

  if (music_stream_ != nullptr && !music_stream_->IsFinished()) {
    float mvol = playbackChannelMuted_[static_cast<std::size_t>(PlaybackChannel::Music)]
                     ? 0.0f
                     : music_volume_.load(std::memory_order_relaxed);
    if (music_fade_.active) {
      const float t = (music_fade_.duration_ms > 0.0f)
                          ? std::clamp(music_fade_.elapsed_ms / music_fade_.duration_ms, 0.0f, 1.0f)
                          : 1.0f;
      mvol *= music_fade_.start_volume + (music_fade_.end_volume - music_fade_.start_volume) * t;
    }
    music_stream_->MixInto(mix, frames, mvol * music_.volume * master);
  } else if (music_.active) {
    float mvol = playbackChannelMuted_[static_cast<std::size_t>(PlaybackChannel::Music)]
                     ? 0.0f
                     : music_volume_.load(std::memory_order_relaxed);

    if (music_fade_.active) {
      const float t = (music_fade_.duration_ms > 0.0f)
                          ? std::clamp(music_fade_.elapsed_ms / music_fade_.duration_ms, 0.0f, 1.0f)
                          : 1.0f;
      mvol *= music_fade_.start_volume + (music_fade_.end_volume - music_fade_.start_volume) * t;
    }
    MixChannel(music_, mix, frames, mvol * master);
  }

  if (ambience_.active) {
    float avol = playbackChannelMuted_[static_cast<std::size_t>(PlaybackChannel::Ambience)]
                     ? 0.0f
                     : ambience_volume_.load(std::memory_order_relaxed);

    if (ambience_fade_.active) {
      const float t = (ambience_fade_.duration_ms > 0.0f)
                          ? std::clamp(ambience_fade_.elapsed_ms / ambience_fade_.duration_ms, 0.0f, 1.0f)
                          : 1.0f;
      avol *= ambience_fade_.start_volume + (ambience_fade_.end_volume - ambience_fade_.start_volume) * t;
    }
    MixChannel(ambience_, mix, frames, avol * master);
  }

  if (credits_stream_ != nullptr && !credits_stream_->IsFinished()) {
    float cvol = playbackChannelMuted_[static_cast<std::size_t>(PlaybackChannel::Music)]
                     ? 0.0f
                     : music_volume_.load(std::memory_order_relaxed);
    if (credits_fade_.active) {
      const float t = (credits_fade_.duration_ms > 0.0f)
                          ? std::clamp(credits_fade_.elapsed_ms /
                                           credits_fade_.duration_ms,
                                       0.0f, 1.0f)
                          : 1.0f;
      cvol *= credits_fade_.start_volume +
              (credits_fade_.end_volume - credits_fade_.start_volume) * t;
    }
    credits_stream_->MixInto(mix, frames, cvol * credits_.volume * master);
  } else if (credits_.active) {
    float cvol = playbackChannelMuted_[static_cast<std::size_t>(PlaybackChannel::Music)]
                     ? 0.0f
                     : music_volume_.load(std::memory_order_relaxed);

    if (credits_fade_.active) {
      const float t = (credits_fade_.duration_ms > 0.0f)
                          ? std::clamp(credits_fade_.elapsed_ms / credits_fade_.duration_ms, 0.0f, 1.0f)
                          : 1.0f;
      cvol *= credits_fade_.start_volume + (credits_fade_.end_volume - credits_fade_.start_volume) * t;
    }
    MixChannel(credits_, mix, frames, cvol * master);
  }

  const float direct_sfx_volume =
      playbackChannelMuted_[static_cast<std::size_t>(PlaybackChannel::SFX)]
          ? 0.0f
          : sfx_volume_.load(std::memory_order_relaxed);
  for (std::size_t index = 0; index < sfx_.size(); ++index) {
    auto& ch = sfx_[index];
    if (!ch.active) continue;
    const float fade_scale =
        sfx_fades_[index].active ? std::clamp(EvaluateFadeScale(sfx_fades_[index]), 0.0f, 1.0f)
                                 : 1.0f;
    const float channel_vol = (ch.is_3d ? ch.computed_volume : ch.volume) * fade_scale;
    float category_gain = direct_sfx_volume;
    if (ch.uses_playback_channel) {
      const auto category = static_cast<std::size_t>(ch.playback_channel);
      category_gain = category < playbackChannelVolumes_.size() &&
                              !playbackChannelMuted_[category]
                          ? playbackChannelVolumes_[category]
                          : 0.0f;
    }
    MixChannel(ch, mix, frames, channel_vol * category_gain * master);
  }

  if (movie_audio_playing_ && movie_audio_stream_ != nullptr) {
    const auto mixed = movie_audio_stream_->MixInto(
        mix, static_cast<std::size_t>(total_samples), movie_volume_ * master);
    movie_sample_pos_ += mixed;
    if (movie_audio_stream_->IsDrained()) {
      movie_audio_playing_ = false;
    }
  }

  ApplyWorldReverb(mix, frames);

  if (voice_output_device_ == 0) {
    const auto voice_category = static_cast<std::size_t>(PlaybackChannel::Voice);
    const float voice_gain = playbackChannelMuted_[voice_category]
                                 ? 0.0f
                                 : playbackChannelVolumes_[voice_category] * master;
    for (auto &voice : external_voice_streams_) {
      const std::size_t to_mix = std::min(
          voice.sample_count, static_cast<std::size_t>(total_samples));
      for (std::size_t index = 0; index < to_mix; ++index) {
        if (voice_gain != 0.0f) {
          mix[index] += static_cast<std::int32_t>(
              static_cast<float>(voice.samples[voice.read_cursor]) *
              voice.volume * voice_gain);
        }
        voice.read_cursor =
            (voice.read_cursor + 1u) % ExternalVoiceStream::kRingSampleCapacity;
      }
      voice.sample_count -= to_mix;
    }
  }

  for (int i = 0; i < total_samples; ++i) {
    output[i] = silent
        ? static_cast<std::int16_t>(0)
        : static_cast<std::int16_t>(std::clamp(
              mix[i], static_cast<std::int32_t>(-32768),
              static_cast<std::int32_t>(32767)));
  }
}

void AudioEngine::MixVoiceOutput(std::int16_t *output, const int frames) {
  const int total_samples = frames * voice_output_channels_;
  if (output == nullptr || total_samples <= 0 ||
      static_cast<std::size_t>(total_samples) > voice_callback_mix_buffer_.size()) {
    return;
  }
  auto *const mix = voice_callback_mix_buffer_.data();
  std::fill_n(mix, total_samples, 0);

  const bool silent = muted_.load(std::memory_order_relaxed) ||
                      master_volume_.load(std::memory_order_relaxed) <= 0.0f;
  const auto voice_category = static_cast<std::size_t>(PlaybackChannel::Voice);
  const float gain = silent || playbackChannelMuted_[voice_category]
                         ? 0.0f
                         : master_volume_.load(std::memory_order_relaxed) *
                               playbackChannelVolumes_[voice_category];
  for (auto &voice : external_voice_streams_) {
    const std::size_t to_mix = std::min(
        voice.sample_count, static_cast<std::size_t>(total_samples));
    for (std::size_t index = 0; index < to_mix; ++index) {
      if (gain != 0.0f) {
        mix[index] += static_cast<std::int32_t>(
            static_cast<float>(voice.samples[voice.read_cursor]) *
            voice.volume * gain);
      }
      voice.read_cursor =
          (voice.read_cursor + 1u) % ExternalVoiceStream::kRingSampleCapacity;
    }
    voice.sample_count -= to_mix;
  }
  for (int index = 0; index < total_samples; ++index) {
    output[index] = static_cast<std::int16_t>(std::clamp(
        mix[index], static_cast<std::int32_t>(-32768),
        static_cast<std::int32_t>(32767)));
  }
}

void AudioEngine::EnsureWorldReverbInitialized() {
  if (world_reverb_initialized_) {
    return;
  }

  world_reverb_engine_.Initialize(output_rate_);
  world_reverb_engine_.SetRoomSize(world_reverb_room_size_);
  world_reverb_engine_.SetDamping(world_reverb_damping_);
  world_reverb_engine_.SetWetLevel(world_reverb_wet_);
  world_reverb_engine_.SetDryLevel(world_reverb_dry_);
  world_reverb_engine_.SetStereoWidth(world_reverb_width_);
  world_reverb_engine_.SetFreezeMode(false);
  world_reverb_initialized_ = true;
}

void AudioEngine::ApplyWorldReverb(std::int32_t* mix_buf, const int frames) {
  if (!world_reverb_enabled_.load(std::memory_order_relaxed) || output_channels_ < 2 ||
      frames <= 0 || mix_buf == nullptr) {
    return;
  }

  EnsureWorldReverbInitialized();

  const int total_samples = frames * output_channels_;
  world_reverb_input_.resize(static_cast<std::size_t>(total_samples));
  world_reverb_output_.resize(static_cast<std::size_t>(total_samples));

  for (int i = 0; i < total_samples; ++i) {
    world_reverb_input_[static_cast<std::size_t>(i)] = static_cast<float>(mix_buf[i]);
  }

  world_reverb_engine_.ProcessInterleaved(
      world_reverb_input_.data(), world_reverb_output_.data(), frames,
      output_channels_);

  for (int i = 0; i < total_samples; ++i) {
    const float sample =
        std::clamp(world_reverb_output_[static_cast<std::size_t>(i)], -32768.0f, 32767.0f);
    mix_buf[i] = static_cast<std::int32_t>(std::lround(sample));
  }
}

void AudioEngine::MixChannel(AudioChannel& ch, std::int32_t* mix_buf, int frames, float vol_scale) {
  if (ch.paused) {
    return;
  }
  if (ch.pcm_stream != nullptr) {
    ch.pcm_stream->MixInto(mix_buf, frames, vol_scale);
    if (ch.pcm_stream->IsFinished()) {

      ch.active = false;
    }
    return;
  }

  if (ch.data == nullptr || ch.data->Empty()) return;

  const SoundData& data = *ch.data;
  const int src_channels = data.channels;
  const int dst_channels = output_channels_;
  const std::size_t total_src_frames = data.TotalFrames();

  for (int f = 0; f < frames; ++f) {
    if (ch.position >= total_src_frames) {
      if (ch.loop) {
        ch.position = 0;
      } else {
        ch.active = false;
        return;
      }
    }

    const std::size_t source_offset = ch.position *
        static_cast<std::size_t>(src_channels);
    float left = static_cast<float>(data.samples[source_offset]);
    float right = src_channels == 1
                      ? left
                      : static_cast<float>(data.samples[source_offset + 1u]);

    if (vol_scale != 0.0f) {
      left *= vol_scale;
      right *= vol_scale;

      if (ch.is_3d && dst_channels >= 2 &&
          software_hrtf_enabled_.load(std::memory_order_relaxed)) {
        const float pan = std::clamp(ch.computed_pan, -1.0f, 1.0f);
        const float absolute_pan = std::abs(pan);
        const std::size_t delay_frames = std::min<std::size_t>(
            static_cast<std::size_t>(std::lround(
                absolute_pan * 0.00065f * static_cast<float>(output_rate_))),
            AudioChannel::kHrtfDelayCapacity - 1u);
        ch.hrtf_left_delay[ch.hrtf_delay_cursor] = left;
        ch.hrtf_right_delay[ch.hrtf_delay_cursor] = right;
        const std::size_t delayed_cursor =
            (ch.hrtf_delay_cursor + AudioChannel::kHrtfDelayCapacity -
             delay_frames) % AudioChannel::kHrtfDelayCapacity;
        constexpr float kFarEarLowPassBlend = 0.32f;
        constexpr float kFarEarMinimumGain = 0.55f;
        if (pan > 0.0f) {
          ch.hrtf_left_filter += kFarEarLowPassBlend *
              (ch.hrtf_left_delay[delayed_cursor] - ch.hrtf_left_filter);
          left = ch.hrtf_left_filter *
              (1.0f - (1.0f - kFarEarMinimumGain) * absolute_pan);
        } else if (pan < 0.0f) {
          ch.hrtf_right_filter += kFarEarLowPassBlend *
              (ch.hrtf_right_delay[delayed_cursor] - ch.hrtf_right_filter);
          right = ch.hrtf_right_filter *
              (1.0f - (1.0f - kFarEarMinimumGain) * absolute_pan);
        }
        ch.hrtf_delay_cursor =
            (ch.hrtf_delay_cursor + 1u) % AudioChannel::kHrtfDelayCapacity;
      } else if (ch.is_3d && dst_channels >= 2) {

        left *= ch.computed_pan > 0.0f ? 1.0f - ch.computed_pan : 1.0f;
        right *= ch.computed_pan < 0.0f ? 1.0f + ch.computed_pan : 1.0f;
      }

      if (!ch.is_3d && src_channels == dst_channels && dst_channels > 2) {
        for (int channel = 0; channel < dst_channels; ++channel) {
          mix_buf[f * dst_channels + channel] += static_cast<std::int32_t>(
              static_cast<float>(data.samples[
                  source_offset + static_cast<std::size_t>(channel)]) *
              vol_scale);
        }
      } else if (dst_channels >= 2) {
        mix_buf[f * dst_channels + 0] += static_cast<std::int32_t>(left);
        mix_buf[f * dst_channels + 1] += static_cast<std::int32_t>(right);
      } else {
        mix_buf[f] += static_cast<std::int32_t>((left + right) * 0.5f);
      }
    }

    ++ch.position;
  }
}

void AudioEngine::SetMasterVolume(float volume) {

  if (volume >= 0.0f && volume <= 1.0f) {
    master_volume_.store(volume, std::memory_order_relaxed);
  }
}

void AudioEngine::SetListenerPosition(float x, float y, float z) {
  if (!initialized_) return;
  SDL_LockAudioDevice(device_);
  listener_x_ = x;
  listener_y_ = y;
  listener_z_ = z;
  SDL_UnlockAudioDevice(device_);
}

void AudioEngine::SetListenerOrientation(float fx, float fy, float fz) {
  if (!initialized_) return;
  SDL_LockAudioDevice(device_);
  listener_fx_ = fx;
  listener_fy_ = fy;
  listener_fz_ = fz;
  SDL_UnlockAudioDevice(device_);
}

void AudioEngine::SetListener3DAttributes(const float* position,
                                           const float* velocity,
                                           const float* forward,
                                           const float* up) {
  if (!initialized_) return;
  SDL_LockAudioDevice(device_);
  if (position) {
    listener_x_ = position[0];
    listener_y_ = position[1];
    listener_z_ = position[2];
  }
  if (velocity) {
    listener_vx_ = velocity[0];
    listener_vy_ = velocity[1];
    listener_vz_ = velocity[2];
  } else {
    listener_vx_ = listener_vy_ = listener_vz_ = 0.0f;
  }
  if (forward) {
    listener_fx_ = forward[0];
    listener_fy_ = forward[1];
    listener_fz_ = forward[2];
  }
  if (up) {
    listener_ux_ = up[0];
    listener_uy_ = up[1];
    listener_uz_ = up[2];
  } else {
    listener_ux_ = 0.0f;
    listener_uy_ = 0.0f;
    listener_uz_ = 1.0f;
  }
  SDL_UnlockAudioDevice(device_);
}

void AudioEngine::Update(float delta_ms) {
  const float dt_sec = delta_ms / 1000.0f;

  {
    for (auto& [id, entry] : handles_) {
      if (entry.state == AudioPlaybackState::Playing) {
          entry.position += dt_sec;
      } else if (entry.state == AudioPlaybackState::FadingIn) {
          entry.fadeElapsed += dt_sec;
          float t = (entry.fadeDuration > 0.0f)
                      ? std::clamp(entry.fadeElapsed / entry.fadeDuration, 0.0f, 1.0f)
                      : 1.0f;
          entry.currentVolume = entry.fadeStart +
              (entry.fadeTarget - entry.fadeStart) * t;
          if (t >= 1.0f) {
              entry.state = AudioPlaybackState::Playing;
              entry.currentVolume = entry.fadeTarget;
          }
          entry.position += dt_sec;
      } else if (entry.state == AudioPlaybackState::FadingOut) {
          entry.fadeElapsed += dt_sec;
          float t = (entry.fadeDuration > 0.0f)
                      ? std::clamp(entry.fadeElapsed / entry.fadeDuration, 0.0f, 1.0f)
                      : 1.0f;
          entry.currentVolume = entry.fadeStart +
              (entry.fadeTarget - entry.fadeStart) * t;
          if (t >= 1.0f) {
              entry.state = AudioPlaybackState::Stopped;
              entry.currentVolume = 0.0f;
              if (entry.sdlChannel >= 0 && entry.sdlGeneration != 0 && initialized_) {
                StopSound(AudioDirectPlaybackHandle{entry.sdlChannel,
                                                    entry.sdlGeneration});
              }
          }
          entry.position += dt_sec;
      }
    }
  }

  if (!initialized_) return;

  std::unique_ptr<MusicPcmStream> stopped_music_stream;
  std::unique_ptr<MusicPcmStream> stopped_credits_stream;
  std::vector<std::shared_ptr<MusicPcmStream>> stopped_sfx_streams;
  stopped_sfx_streams.reserve(sfx_.size());
  SDL_LockAudioDevice(device_);

  if (music_fade_.active &&
      (music_.active || (music_stream_ != nullptr && !music_stream_->IsFinished()))) {
    music_fade_.elapsed_ms += delta_ms;
    if (music_fade_.elapsed_ms >= music_fade_.duration_ms) {
      music_fade_.elapsed_ms = music_fade_.duration_ms;
      music_fade_.active = false;

      if (!music_fade_.fading_in) {

        music_.active = false;
        music_.data = {};
        music_.position = 0;
        stopped_music_stream = std::move(music_stream_);
      }
    }
  }

  if (ambience_fade_.active && ambience_.active) {
    ambience_fade_.elapsed_ms += delta_ms;
    if (ambience_fade_.elapsed_ms >= ambience_fade_.duration_ms) {
      ambience_fade_.elapsed_ms = ambience_fade_.duration_ms;
      ambience_fade_.active = false;

      if (!ambience_fade_.fading_in) {
        ambience_.active = false;
        ambience_.data = {};
        ambience_.position = 0;
      }
    }
  }

  if (credits_fade_.active &&
      (credits_.active ||
       (credits_stream_ != nullptr && !credits_stream_->IsFinished()))) {
    credits_fade_.elapsed_ms += delta_ms;
    if (credits_fade_.elapsed_ms >= credits_fade_.duration_ms) {
      credits_fade_.elapsed_ms = credits_fade_.duration_ms;
      credits_fade_.active = false;

      if (!credits_fade_.fading_in) {
        credits_.active = false;
        credits_.data = {};
        credits_.position = 0;
        stopped_credits_stream = std::move(credits_stream_);
      }
    }
  }

  for (std::size_t index = 0; index < sfx_.size(); ++index) {
    auto& fade = sfx_fades_[index];
    auto& channel = sfx_[index];
    if (!channel.active && channel.pcm_stream != nullptr) {
      channel.pcm_stream->RequestStop();
      stopped_sfx_streams.push_back(std::move(channel.pcm_stream));
    }
    if (!fade.active || !channel.active) {
      continue;
    }

    fade.elapsed_ms += delta_ms;
    if (fade.elapsed_ms < fade.duration_ms) {
      continue;
    }

    fade.elapsed_ms = fade.duration_ms;
    fade.active = false;
    if (!fade.fading_in) {
      if (channel.pcm_stream != nullptr) {
        channel.pcm_stream->RequestStop();
        stopped_sfx_streams.push_back(std::move(channel.pcm_stream));
      }
      channel.active = false;
      channel.data = {};
      channel.position = 0;
    }
  }

  for (const auto& [id, entry] : handles_) {
    (void)id;
    ApplyHandleRuntimeVolumeLocked(entry);
  }

  for (auto& ch : sfx_) {
    if (!ch.active || !ch.is_3d) continue;
    UpdateSpatializationLocked(ch);
  }

  SDL_UnlockAudioDevice(device_);
  stopped_music_stream.reset();
  stopped_credits_stream.reset();
  stopped_sfx_streams.clear();
}

void AudioEngine::UpdateSpatializationLocked(AudioChannel& channel) {
  const float dx = channel.x - listener_x_;
  const float dy = channel.y - listener_y_;
  const float dz = channel.z - listener_z_;
  const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
  channel.computed_volume = channel.volume * static_cast<float>(
      SoundEngine_Custom3DRolloff(channel.min_distance,
                                  channel.max_distance, distance));
  channel.computed_pan = 0.0f;
  if (distance <= 1.0e-6f) {
    return;
  }

  const float right_x = listener_fy_ * listener_uz_ - listener_fz_ * listener_uy_;
  const float right_y = listener_fz_ * listener_ux_ - listener_fx_ * listener_uz_;
  const float right_z = listener_fx_ * listener_uy_ - listener_fy_ * listener_ux_;
  const float right_length =
      std::sqrt(right_x * right_x + right_y * right_y + right_z * right_z);
  if (right_length <= 1.0e-6f) {
    return;
  }
  channel.computed_pan = std::clamp(
      (dx * right_x + dy * right_y + dz * right_z) /
          (distance * right_length),
      -1.0f, 1.0f);
}

void AudioEngine::SetMuted(bool muted) {
  muted_.store(muted, std::memory_order_relaxed);
}

bool AudioEngine::IsMuted() const {
  return muted_.load(std::memory_order_relaxed);
}

void AudioEngine::SetWorldReverbEnabled(const bool enabled) {
  const bool can_lock = initialized_ && device_ != 0;
  if (can_lock) {
    SDL_LockAudioDevice(device_);
  }

  world_reverb_enabled_.store(enabled, std::memory_order_relaxed);
  if (enabled) {
    EnsureWorldReverbInitialized();
  } else if (world_reverb_initialized_) {
    world_reverb_engine_.Clear();
  }

  if (can_lock) {
    SDL_UnlockAudioDevice(device_);
  }
}

void AudioEngine::ConfigureWorldReverb(const float room_size, const float damping, const float wet,
                                       const float dry, const float width) {
  const bool can_lock = initialized_ && device_ != 0;
  if (can_lock) {
    SDL_LockAudioDevice(device_);
  }

  world_reverb_room_size_ = std::clamp(room_size, 0.0f, 1.0f);
  world_reverb_damping_ = std::clamp(damping, 0.0f, 1.0f);
  world_reverb_wet_ = std::clamp(wet, 0.0f, 1.0f);
  world_reverb_dry_ = std::clamp(dry, 0.0f, 1.0f);
  world_reverb_width_ = std::clamp(width, 0.0f, 1.0f);
  EnsureWorldReverbInitialized();
  world_reverb_engine_.SetRoomSize(world_reverb_room_size_);
  world_reverb_engine_.SetDamping(world_reverb_damping_);
  world_reverb_engine_.SetWetLevel(world_reverb_wet_);
  world_reverb_engine_.SetDryLevel(world_reverb_dry_);
  world_reverb_engine_.SetStereoWidth(world_reverb_width_);
  world_reverb_engine_.SetFreezeMode(false);

  if (can_lock) {
    SDL_UnlockAudioDevice(device_);
  }
}

}
