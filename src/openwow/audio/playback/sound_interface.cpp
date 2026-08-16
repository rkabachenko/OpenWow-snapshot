#include "openwow/audio/playback/sound_runtime_internal.h"
#include "openwow/audio/voice/voice_chat_loopback.h"

namespace openwow::audio {

SoundRuntime::SoundRuntime() {
  world_reverb_ = BuildGlueWorldReverbProperties();
  audio_engine_ = std::make_unique<AudioEngine>();
  sound_engine_ = std::make_unique<SoundEngine>(*audio_engine_);
  voice_loopback_ =
      std::make_unique<VoiceChatLoopback>(*sound_engine_, *audio_engine_);
  world_audio_callbacks_ = std::make_unique<WorldAudioCallbackRegistrationState>();
  sound_validation_callbacks_ =
      std::make_unique<SoundValidationCallbackRegistrationState>();
  callback_lifetime_ = std::make_shared<int>(0);
  sound_engine_->BindVoiceChatLoopback(*voice_loopback_);
  sound_provider_preference_ids_.fill(-1);
  world_reverb_room_lf_dsp_state_ = BuildWorldReverbRoomLfDspState(
      world_reverb_, static_cast<float>(audio_engine_->GetOutputRate()));
}

SoundRuntime::~SoundRuntime() {
  sound_validation_callbacks_->Reset();
  UnregisterEnterWorldAudioCallbacks();
  UnregisterSoundEngineUpdateCallback();
  callback_lifetime_.reset();
}

bool SoundRuntime::PrepareOutputDevice(const int sample_rate, const int channels,
                                       const int buffer_size) {
  return audio_engine_->Initialize(sample_rate, channels, buffer_size);
}

bool SoundRuntime::IsOutputDeviceReady() const { return audio_engine_->IsInitialized(); }

void SoundRuntime::SetVfsLoader(VfsLoader loader) {
  audio_engine_->SetVfsLoader(std::move(loader));
}

void SoundRuntime::ShutdownOutputDevice() { audio_engine_->Shutdown(); }

bool SoundRuntime::PlayMusicFile(const std::string& path, const bool loop,
                                 const float fade_in_ms, const float volume) {
  return audio_engine_->PlayMusic(path, loop, fade_in_ms, volume);
}

void SoundRuntime::StopMusicFile(const float fade_out_ms) {
  audio_engine_->StopMusic(fade_out_ms);
}

bool SoundRuntime::IsMusicFilePlaying() const { return audio_engine_->IsMusicPlaying(); }

bool SoundRuntime::PlayAmbienceFile(const std::string& path, const float volume,
                                    const float fade_in_ms) {
  return audio_engine_->PlayAmbience(path, volume, fade_in_ms);
}

void SoundRuntime::StopAmbienceFile(const float fade_out_ms) {
  audio_engine_->StopAmbience(fade_out_ms);
}

bool SoundRuntime::IsAmbienceFilePlaying() const { return audio_engine_->IsAmbiencePlaying(); }

bool SoundRuntime::PlayCreditsFile(const std::string& path, const bool loop,
                                   const float volume) {
  return audio_engine_->PlayCredits(path, loop, 0.0f, volume);
}

void SoundRuntime::StopCreditsFile(const float fade_out_ms) {
  audio_engine_->StopCredits(fade_out_ms);
}

bool SoundRuntime::IsCreditsFilePlaying() const { return audio_engine_->IsCreditsPlaying(); }

void SoundRuntime::PlayMovieAudio(std::shared_ptr<IMovieAudioSource> source, const float volume) {
  audio_engine_->PlayMovieAudioStream(std::move(source), volume);
}

void SoundRuntime::StopMovieAudio() { audio_engine_->StopMovieAudio(); }

bool SoundRuntime::IsMovieAudioPlaying() const { return audio_engine_->IsMovieAudioPlaying(); }

double SoundRuntime::MovieAudioTimeSeconds() const {
  return audio_engine_->MovieAudioTimeSeconds();
}

int SoundRuntime::GetOutputSampleRate() const {
  return audio_engine_->GetOutputRate();
}

int SoundRuntime::GetOutputChannelCount() const {
  return audio_engine_->GetOutputChannels();
}

float SoundRuntime::GetMasterVolume() const { return audio_engine_->GetMasterVolume(); }

float SoundRuntime::GetPlaybackChannelVolume(const PlaybackChannel channel) const {
  return audio_engine_->GetPlaybackChannelVolume(channel);
}

void SoundRuntime::SetMasterVolume(const float volume) {
  sound_engine_->SetMasterVolume(volume);
}

void SoundRuntime::SetPlaybackChannelVolume(const PlaybackChannel channel,
                                            const float volume) {
  audio_engine_->SetPlaybackChannelVolume(channel, volume);
}

void SoundRuntime::SetListener3DAttributes(const float* position,
                                           const float* velocity,
                                           const float* forward,
                                           const float* up) {
  audio_engine_->SetListener3DAttributes(position, velocity, forward, up);
}

void SoundRuntime::RegisterSoundEngineUpdateCallback() {
  auto &state = *world_audio_callbacks_;
  std::lock_guard lock(state.mutex);
  if (state.engine_update_handle != openwow::core::CallbackHandle::Invalid) {
    return;
  }

  const std::weak_ptr<void> lifetime = callback_lifetime_;
  state.engine_update_handle = openwow::core::FrameScheduler::Instance().Register(
      openwow::core::Phase::Update, 1000,
      [this, lifetime](double) {
        if (lifetime.expired()) return;
        (void)sound_engine_->ProcessUpdateTick();
      },
      "SoundEngine_ProcessUpdateTick");
}

void SoundRuntime::UnregisterSoundEngineUpdateCallback() {
  auto &state = *world_audio_callbacks_;
  openwow::core::CallbackHandle handle = openwow::core::CallbackHandle::Invalid;
  {
    std::lock_guard lock(state.mutex);
    handle = state.engine_update_handle;
    state.engine_update_handle = openwow::core::CallbackHandle::Invalid;
  }
  if (handle != openwow::core::CallbackHandle::Invalid) {
    openwow::core::FrameScheduler::Instance().Unregister(handle);
  }
}

void SoundRuntime::RegisterEnterWorldAudioCallbacks() {
  auto &state = *world_audio_callbacks_;
  std::lock_guard lock(state.mutex);
  const std::weak_ptr<void> lifetime = callback_lifetime_;

  if (state.zone_music_handle == openwow::core::CallbackHandle::Invalid) {
    state.zone_music_handle = openwow::core::FrameScheduler::Instance().Register(
        openwow::core::Phase::Update, 1000,
        [this, lifetime](double) {
          if (lifetime.expired()) return;
          (void)UpdateZoneMusic();
        },
        "SoundInterface_UpdateZoneMusic");
  }

  if (state.liquid_update_handle == openwow::core::CallbackHandle::Invalid) {
    state.liquid_update_handle = openwow::core::FrameScheduler::Instance().Register(
        openwow::core::Phase::Update, 1000,
        [this, lifetime](double delta_sec) {
          if (lifetime.expired()) return;
          (void)UpdateLoop();
          (void)UpdateLiquidAmbience(delta_sec);
        },
        "LiquidQueryResult__method");
  }
}

void SoundRuntime::UnregisterEnterWorldAudioCallbacks() {
  auto &state = *world_audio_callbacks_;
  openwow::core::CallbackHandle zone_music_handle = openwow::core::CallbackHandle::Invalid;
  openwow::core::CallbackHandle liquid_update_handle = openwow::core::CallbackHandle::Invalid;
  openwow::core::CallbackHandle chaos_mode_handle = openwow::core::CallbackHandle::Invalid;

  {
    std::lock_guard lock(state.mutex);
    zone_music_handle = state.zone_music_handle;
    liquid_update_handle = state.liquid_update_handle;
    chaos_mode_handle = state.chaos_mode_handle;
    state.zone_music_handle = openwow::core::CallbackHandle::Invalid;
    state.liquid_update_handle = openwow::core::CallbackHandle::Invalid;
    state.chaos_mode_handle = openwow::core::CallbackHandle::Invalid;
  }

  auto &scheduler = openwow::core::FrameScheduler::Instance();
  if (zone_music_handle != openwow::core::CallbackHandle::Invalid) {
    scheduler.Unregister(zone_music_handle);
  }
  if (liquid_update_handle != openwow::core::CallbackHandle::Invalid) {
    scheduler.Unregister(liquid_update_handle);
  }
  if (chaos_mode_handle != openwow::core::CallbackHandle::Invalid) {
    scheduler.Unregister(chaos_mode_handle);
  }

  StopLiquidAmbience();
}

}
