#include "openwow/audio/adapters/sdl/audio_engine_internal.h"

namespace openwow::audio {

void AudioEngine::SetVfsLoader(VfsLoader loader) {
  vfs_loader_ = std::move(loader);
}

std::shared_ptr<const SoundData> AudioEngine::LoadAndDecode(const std::string& path) {

  {
    std::lock_guard lock(cache_mutex_);
    auto it = cache_.find(path);
    if (it != cache_.end()) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kDebug,
          "LoadAndDecode: cache hit for path=" + path);
      return it->second;
    }
  }

  if (!vfs_loader_) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
        "LoadAndDecode: no VFS loader set, path=" + path);
    return nullptr;
  }
  auto bytes = vfs_loader_(path);
  if (!bytes.has_value() || bytes->empty()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
        "LoadAndDecode: VFS returned no data for path=" + path);
    return nullptr;
  }

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kDebug,
      "LoadAndDecode: VFS loaded " + std::to_string(bytes->size()) +
      " bytes for path=" + path);

  auto decoded = DecodeAudio(bytes.value());
  if (!decoded.has_value()) {

    decoded = DecodeWav(bytes.value());
  }
  if (!decoded.has_value()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
        "LoadAndDecode: all decoders failed for path=" + path +
        " (file_size=" + std::to_string(bytes->size()) + ")");
    return nullptr;
  }

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kDebug,
      "LoadAndDecode: decoded path=" + path +
      " frames=" + std::to_string(decoded->TotalFrames()) +
      " channels=" + std::to_string(decoded->channels) +
      " rate=" + std::to_string(decoded->sample_rate));

  auto normalized = std::make_shared<const SoundData>(
      Normalize(decoded.value(), static_cast<std::uint32_t>(output_rate_),
                static_cast<std::uint8_t>(output_channels_)));

  {
    std::lock_guard lock(cache_mutex_);
    cache_[path] = normalized;
    cached_output_rate_ = output_rate_;
    cached_output_channels_ = output_channels_;
  }

  return normalized;
}

void AudioEngine::EnsureDecodeWorkers() {
  if (decode_workers_ != nullptr) {
    return;
  }
  auto workers = std::make_unique<openwow::core::ThreadPoolSystem>();

  workers->Initialize(2u);
  decode_workers_ = std::move(workers);
}

void AudioEngine::LoadAndDecodeAsync(
    std::string path,
    std::function<void(std::shared_ptr<const SoundData>)> on_ready) {
  std::shared_ptr<const SoundData> cache_hit;
  {
    std::lock_guard lock(cache_mutex_);
    auto it = cache_.find(path);
    if (it != cache_.end()) {
      cache_hit = it->second;
    }
  }
  if (cache_hit != nullptr) {
    on_ready(std::move(cache_hit));
    return;
  }

  EnsureDecodeWorkers();
  if (decode_workers_ == nullptr) {

    on_ready(LoadAndDecode(path));
    return;
  }

  decode_workers_->Submit(
      "AudioEngine::LoadAndDecode:" + path, openwow::core::TaskPriority::Normal,
      [this, path, on_ready = std::move(on_ready)]() mutable {
        on_ready(LoadAndDecode(path));
      });
}

void AudioEngine::ClearCacheForOutputFormat() {
  std::lock_guard lock(cache_mutex_);
  if (cache_.empty()) {
    return;
  }
  if (cached_output_rate_ == output_rate_ &&
      cached_output_channels_ == output_channels_) {
    return;
  }

  openwow::diagnostics::Log(
      openwow::diagnostics::LogLevel::kInfo,
      "AudioEngine: output format changed (" +
          std::to_string(cached_output_rate_) + "Hz/" +
          std::to_string(cached_output_channels_) + "ch -> " +
          std::to_string(output_rate_) + "Hz/" +
          std::to_string(output_channels_) + "ch); dropping " +
          std::to_string(cache_.size()) + " normalized clip(s)");
  cache_.clear();
}

void AudioEngine::Precache(const std::string& path) {

  if (HasMp3Extension(path)) {
    return;
  }
  (void)LoadAndDecode(path);
}

void AudioEngine::ClearCache() {
  std::lock_guard lock(cache_mutex_);
  cache_.clear();
}

void AudioEngine::RestartHandleFadeInOnChannelReadyLocked(
    const int channel, const std::uint64_t generation) {

  if (channel < 0 || channel >= static_cast<int>(handleIdByChannel_.size())) {
    return;
  }
  const std::uint32_t owner_id =
      handleIdByChannel_[static_cast<std::size_t>(channel)];
  if (owner_id == kNoOwningHandleId) {
    return;
  }
  const auto it = handles_.find(owner_id);
  if (it == handles_.end()) {
    return;
  }
  HandleEntry& entry = it->second;
  if (entry.sdlChannel != channel || entry.sdlGeneration != generation ||
      entry.state != AudioPlaybackState::FadingIn) {
    return;
  }
  entry.fadeElapsed = 0.0f;
  entry.currentVolume = entry.fadeStart;
}

void AudioEngine::ReleaseChannelOwnershipIndex(const HandleEntry& entry) noexcept {
  if (entry.sdlChannel < 0 ||
      entry.sdlChannel >= static_cast<int>(handleIdByChannel_.size())) {
    return;
  }
  auto& slot = handleIdByChannel_[static_cast<std::size_t>(entry.sdlChannel)];

  if (slot == entry.handleId) {
    slot = kNoOwningHandleId;
  }
}

bool AudioEngine::HandleOwnsChannelLocked(const HandleEntry& entry) const {
  return entry.sdlChannel >= 0 &&
         entry.sdlChannel < static_cast<int>(sfx_.size()) &&
         entry.sdlGeneration != 0 &&
         directPlaybackGenerations_[static_cast<std::size_t>(entry.sdlChannel)] ==
             entry.sdlGeneration;
}

void AudioEngine::ApplyHandleRuntimeVolumeLocked(const HandleEntry& entry) {
  if (!HandleOwnsChannelLocked(entry)) return;

  auto& channel = sfx_[static_cast<std::size_t>(entry.sdlChannel)];
  if (!channel.active) {
    return;
  }

  channel.volume = std::clamp(entry.currentVolume, 0.0f, 1.0f);
  if (channel.is_3d) {
    UpdateSpatializationLocked(channel);
  }
}

AudioPlaybackHandle AudioEngine::Play(const AudioClipInfo& clip) {
    if (!initialized_ || device_ == 0 || clip.path.empty() || !vfs_loader_) {
        return {};
    }

    const float initial_volume = clip.fadeInTime > 0.0f ? 0.0f : clip.volume;
    const int channel = PlaySound(clip.path, initial_volume, clip.loop,
                                  clip.priority, clip.channel);
    if (channel < 0) {
        return {};
    }

    HandleEntry entry;
    entry.handleId     = nextHandleId_++;
    entry.clip         = clip;
    entry.state        = AudioPlaybackState::Playing;
    entry.currentVolume = clip.volume;
    entry.position     = 0.0f;

    if (clip.fadeInTime > 0.0f) {
        entry.state        = AudioPlaybackState::FadingIn;
        entry.currentVolume = 0.0f;
        entry.fadeStart     = 0.0f;
        entry.fadeTarget    = clip.volume;
        entry.fadeDuration  = clip.fadeInTime;
        entry.fadeElapsed   = 0.0f;
    }

    entry.sdlChannel = channel;
    entry.priority = clip.priority;

    SDL_LockAudioDevice(device_);
    entry.sdlGeneration =
        directPlaybackGenerations_[static_cast<std::size_t>(channel)];
    SDL_UnlockAudioDevice(device_);

    AudioPlaybackHandle handle{entry.handleId};
    const std::uint32_t owning_handle_id = entry.handleId;
    auto [it, inserted] = handles_.emplace(entry.handleId, std::move(entry));
    (void)inserted;

    handleIdByChannel_[static_cast<std::size_t>(channel)] = owning_handle_id;
    if (initialized_ && device_ != 0) {
        SDL_LockAudioDevice(device_);
    }
    ApplyHandleRuntimeVolumeLocked(it->second);
    if (initialized_ && device_ != 0) {
        SDL_UnlockAudioDevice(device_);
    }
    return handle;
}

void AudioEngine::Stop(AudioPlaybackHandle handle) {
    auto it = handles_.find(handle.handleId);
    if (it == handles_.end()) return;
    if (it->second.sdlChannel >= 0 && it->second.sdlGeneration != 0) {
        StopSound(AudioDirectPlaybackHandle{it->second.sdlChannel,
                                            it->second.sdlGeneration});
    }
    it->second.state = AudioPlaybackState::Stopped;
}

void AudioEngine::Pause(AudioPlaybackHandle handle) {
    auto it = handles_.find(handle.handleId);
    if (it == handles_.end()) return;
    if (it->second.state == AudioPlaybackState::Playing ||
        it->second.state == AudioPlaybackState::FadingIn) {
      it->second.state = AudioPlaybackState::Paused;
      const int channel = it->second.sdlChannel;
      if (initialized_ && device_ != 0 && channel >= 0 &&
          channel < max_sfx_channels_) {
        SDL_LockAudioDevice(device_);
        if (HandleOwnsChannelLocked(it->second)) {
          sfx_[static_cast<std::size_t>(channel)].paused = true;
        }
        SDL_UnlockAudioDevice(device_);
      }
    }
}

void AudioEngine::Resume(AudioPlaybackHandle handle) {
    auto it = handles_.find(handle.handleId);
    if (it == handles_.end()) return;
    if (it->second.state == AudioPlaybackState::Paused) {
      it->second.state = AudioPlaybackState::Playing;
      const int channel = it->second.sdlChannel;
      if (initialized_ && device_ != 0 && channel >= 0 &&
          channel < max_sfx_channels_) {
        SDL_LockAudioDevice(device_);
        if (HandleOwnsChannelLocked(it->second)) {
          sfx_[static_cast<std::size_t>(channel)].paused = false;
        }
        SDL_UnlockAudioDevice(device_);
      }
    }
}

void AudioEngine::FadeIn(AudioPlaybackHandle handle, float seconds) {
    auto it = handles_.find(handle.handleId);
    if (it == handles_.end()) return;
    it->second.state        = AudioPlaybackState::FadingIn;
    it->second.fadeStart    = 0.0f;
    it->second.fadeTarget   = it->second.clip.volume;
    it->second.fadeDuration = seconds;
    it->second.fadeElapsed  = 0.0f;
    it->second.currentVolume = 0.0f;
}

void AudioEngine::FadeOut(AudioPlaybackHandle handle, float seconds) {
    auto it = handles_.find(handle.handleId);
    if (it == handles_.end()) return;
    it->second.state        = AudioPlaybackState::FadingOut;
    it->second.fadeStart    = it->second.currentVolume;
    it->second.fadeTarget   = 0.0f;
    it->second.fadeDuration = seconds;
    it->second.fadeElapsed  = 0.0f;
}

void AudioEngine::SetHandleVolume(AudioPlaybackHandle handle, float volume) {
    auto it = handles_.find(handle.handleId);
    if (it == handles_.end()) return;
    it->second.currentVolume = std::clamp(volume, 0.0f, 1.0f);
    it->second.clip.volume   = it->second.currentVolume;
    if (initialized_ && device_ != 0) {
        SDL_LockAudioDevice(device_);
    }
    ApplyHandleRuntimeVolumeLocked(it->second);
    if (initialized_ && device_ != 0) {
        SDL_UnlockAudioDevice(device_);
    }
}

void AudioEngine::SetSound3DPosition(AudioPlaybackHandle handle, float x, float y, float z) {
  if (!initialized_) return;

  auto it = handles_.find(handle.handleId);
  if (it == handles_.end()) return;

  const int ch_idx = it->second.sdlChannel;
  if (ch_idx < 0 || ch_idx >= max_sfx_channels_) return;

  SDL_LockAudioDevice(device_);
  auto& ch = sfx_[static_cast<std::size_t>(ch_idx)];
  if (HandleOwnsChannelLocked(it->second) && ch.active) {
    ch.is_3d = true;
    ch.x = x;
    ch.y = y;
    ch.z = z;
    UpdateSpatializationLocked(ch);
  }
  SDL_UnlockAudioDevice(device_);
}

void AudioEngine::SetHandleDistanceRange(AudioPlaybackHandle handle, float min_dist, float max_dist) {
  if (!initialized_) return;

  auto it = handles_.find(handle.handleId);
  if (it == handles_.end()) return;

  const int ch_idx = it->second.sdlChannel;
  if (ch_idx < 0 || ch_idx >= max_sfx_channels_) return;

  SDL_LockAudioDevice(device_);
  auto& ch = sfx_[static_cast<std::size_t>(ch_idx)];
  if (HandleOwnsChannelLocked(it->second) && ch.active) {
    ch.min_distance = std::max(0.0f, min_dist);
    ch.max_distance = std::max(std::max(0.0f, max_dist), ch.min_distance);
    UpdateSpatializationLocked(ch);
  }
  SDL_UnlockAudioDevice(device_);
}

float AudioEngine::GetHandleVolume(AudioPlaybackHandle handle) const {
    auto it = handles_.find(handle.handleId);
    if (it == handles_.end()) return 0.0f;
    return it->second.currentVolume;
}

AudioPlaybackState AudioEngine::GetState(AudioPlaybackHandle handle) const {
    auto it = handles_.find(handle.handleId);
    if (it == handles_.end()) return AudioPlaybackState::Stopped;
    return it->second.state;
}

bool AudioEngine::IsHandlePlaying(AudioPlaybackHandle handle) const {
    auto it = handles_.find(handle.handleId);
    if (it == handles_.end()) return false;
    return it->second.state == AudioPlaybackState::Playing ||
           it->second.state == AudioPlaybackState::FadingIn ||
           it->second.state == AudioPlaybackState::FadingOut;
}

bool AudioEngine::IsHandleMaterialized(const AudioPlaybackHandle handle) const {
    const auto it = handles_.find(handle.handleId);
    if (it == handles_.end() || !initialized_ || device_ == 0) return false;
    SDL_LockAudioDevice(device_);
    const bool materialized = HandleOwnsChannelLocked(it->second);
    SDL_UnlockAudioDevice(device_);
    return materialized;
}

bool AudioEngine::IsHandleChannelActive(AudioPlaybackHandle handle) const {
    auto it = handles_.find(handle.handleId);
    if (it == handles_.end()) return false;
    const int ch_idx = it->second.sdlChannel;
    if (ch_idx < 0 || ch_idx >= static_cast<int>(sfx_.size())) {
      return false;
    }
    if (!initialized_ || device_ == 0) return false;
    SDL_LockAudioDevice(device_);
    const bool active = HandleOwnsChannelLocked(it->second) &&
                        sfx_[static_cast<std::size_t>(ch_idx)].active;
    SDL_UnlockAudioDevice(device_);
    return active;
}

void AudioEngine::QueryHandleChannelsActive(
    const std::vector<AudioPlaybackHandle>& handles,
    std::vector<bool>& active_out) const {

    active_out.assign(handles.size(), false);
    if (!initialized_ || device_ == 0) {
        return;
    }
    SDL_LockAudioDevice(device_);
    for (std::size_t index = 0; index < handles.size(); ++index) {
        const auto it = handles_.find(handles[index].handleId);
        if (it == handles_.end()) {
            continue;
        }
        const int ch_idx = it->second.sdlChannel;
        active_out[index] = ch_idx >= 0 && ch_idx < static_cast<int>(sfx_.size()) &&
                            HandleOwnsChannelLocked(it->second) &&
                            sfx_[static_cast<std::size_t>(ch_idx)].active;
    }
    SDL_UnlockAudioDevice(device_);
}

void AudioEngine::DestroyHandle(AudioPlaybackHandle handle) {
    auto it = handles_.find(handle.handleId);
    if (it == handles_.end()) return;
    if (it->second.sdlChannel >= 0 && it->second.sdlGeneration != 0) {
        StopSound(AudioDirectPlaybackHandle{it->second.sdlChannel,
                                            it->second.sdlGeneration});
    }
    ReleaseChannelOwnershipIndex(it->second);
    handles_.erase(it);
}

void AudioEngine::SetPlaybackChannelVolume(PlaybackChannel ch, float volume) {
    auto idx = static_cast<int>(ch);
    if (idx >= 0 && idx < kPlaybackChannelCount) {
        if (initialized_ && device_ != 0) SDL_LockAudioDevice(device_);
        if (voice_output_device_ != 0) SDL_LockAudioDevice(voice_output_device_);
        playbackChannelVolumes_[static_cast<std::size_t>(idx)] = std::clamp(volume, 0.0f, 1.0f);
        if (voice_output_device_ != 0) SDL_UnlockAudioDevice(voice_output_device_);
        if (initialized_ && device_ != 0) SDL_UnlockAudioDevice(device_);
    }
}

float AudioEngine::GetPlaybackChannelVolume(PlaybackChannel ch) const {
    auto idx = static_cast<int>(ch);
    if (idx >= 0 && idx < kPlaybackChannelCount) {
        if (initialized_ && device_ != 0) SDL_LockAudioDevice(device_);
        if (voice_output_device_ != 0) SDL_LockAudioDevice(voice_output_device_);
        const float volume = playbackChannelVolumes_[static_cast<std::size_t>(idx)];
        if (voice_output_device_ != 0) SDL_UnlockAudioDevice(voice_output_device_);
        if (initialized_ && device_ != 0) SDL_UnlockAudioDevice(device_);
        return volume;
    }
    return 0.0f;
}

void AudioEngine::SetPlaybackChannelMuted(const PlaybackChannel ch, const bool muted) {
    const auto idx = static_cast<int>(ch);
    if (idx >= 0 && idx < kPlaybackChannelCount) {
        if (initialized_ && device_ != 0) SDL_LockAudioDevice(device_);
        if (voice_output_device_ != 0) SDL_LockAudioDevice(voice_output_device_);
        playbackChannelMuted_[static_cast<std::size_t>(idx)] = muted;
        if (voice_output_device_ != 0) SDL_UnlockAudioDevice(voice_output_device_);
        if (initialized_ && device_ != 0) SDL_UnlockAudioDevice(device_);
    }
}

bool AudioEngine::IsPlaybackChannelMuted(const PlaybackChannel ch) const {
    const auto idx = static_cast<int>(ch);
    if (idx < 0 || idx >= kPlaybackChannelCount) {
        return false;
    }
    if (initialized_ && device_ != 0) SDL_LockAudioDevice(device_);
    if (voice_output_device_ != 0) SDL_LockAudioDevice(voice_output_device_);
    const bool muted = playbackChannelMuted_[static_cast<std::size_t>(idx)];
    if (voice_output_device_ != 0) SDL_UnlockAudioDevice(voice_output_device_);
    if (initialized_ && device_ != 0) SDL_UnlockAudioDevice(device_);
    return muted;
}

float AudioEngine::GetMasterVolume() const {
    return master_volume_.load();
}

std::size_t AudioEngine::GetActivePlaybackCount() const {
    std::size_t count = 0;
    for (const auto& [id, entry] : handles_) {
        if (entry.state != AudioPlaybackState::Stopped) ++count;
    }
    return count;
}

std::size_t AudioEngine::GetActiveNonVoicePlaybackCount() const {
    std::size_t count = 0;
    for (const auto& [id, entry] : handles_) {
        if (entry.state != AudioPlaybackState::Stopped &&
            entry.clip.channel != PlaybackChannel::Voice) {
            ++count;
        }
    }
    return count;
}

std::vector<ActiveHandleInfo> AudioEngine::GetActiveHandlesInfo() const {
    std::vector<ActiveHandleInfo> result;
    for (const auto& [id, entry] : handles_) {
        if (entry.state == AudioPlaybackState::Stopped) continue;

        ActiveHandleInfo info;
        info.handleId       = entry.handleId;
        info.state          = entry.state;
        info.volume         = entry.currentVolume;
        info.fading_in      = (entry.state == AudioPlaybackState::FadingIn);
        info.fading_out     = (entry.state == AudioPlaybackState::FadingOut);
        info.is_virtual     = (entry.sdlChannel < 0);
        info.frequency      = static_cast<float>(output_rate_);
        info.creation_flags = entry.clip.loop ? 0x02 : 0;

        if (entry.sdlChannel >= 0 &&
            entry.sdlChannel < static_cast<int>(sfx_.size())) {
            const bool can_lock = initialized_ && device_ != 0;
            if (can_lock) SDL_LockAudioDevice(device_);
            if (HandleOwnsChannelLocked(entry)) {
              const auto& ch = sfx_[static_cast<std::size_t>(entry.sdlChannel)];
              info.is_3d    = ch.is_3d;
              info.pos_x    = ch.x;
              info.pos_y    = ch.y;
              info.pos_z    = ch.z;
              info.min_dist = ch.min_distance;
              info.max_dist = ch.max_distance;
              if (ch.is_3d) {
                const float dx = ch.x - listener_x_;
                const float dy = ch.y - listener_y_;
                const float dz = ch.z - listener_z_;
                info.distance_sq = dx * dx + dy * dy + dz * dz;
              }
            } else {
              info.is_virtual = true;
            }
            if (can_lock) SDL_UnlockAudioDevice(device_);
        }

        result.push_back(info);
    }
    return result;
}

void AudioEngine::StopAllHandles() {
    for (auto& [id, entry] : handles_) {
        if (entry.state != AudioPlaybackState::Stopped) {
            if (entry.sdlChannel >= 0 && entry.sdlGeneration != 0) {
              StopSound(AudioDirectPlaybackHandle{entry.sdlChannel,
                                                  entry.sdlGeneration});
            }
            entry.state = AudioPlaybackState::Stopped;
        }
    }
}

void AudioEngine::StopHandleChannel(const PlaybackChannel ch, const float fade_seconds) {
    for (auto& [id, entry] : handles_) {
        if (entry.clip.channel == ch && entry.state != AudioPlaybackState::Stopped) {
            if (fade_seconds > 0.0f) {
                entry.state = AudioPlaybackState::FadingOut;
                entry.fadeStart = entry.currentVolume;
                entry.fadeTarget = 0.0f;
                entry.fadeDuration = fade_seconds;
                entry.fadeElapsed = 0.0f;
                continue;
            }
            if (entry.sdlChannel >= 0 && entry.sdlGeneration != 0) {
              StopSound(AudioDirectPlaybackHandle{entry.sdlChannel,
                                                  entry.sdlGeneration});
            }
            entry.state = AudioPlaybackState::Stopped;
        }
    }
}

float AudioEngine::GetPlaybackPosition(AudioPlaybackHandle handle) const {
    auto it = handles_.find(handle.handleId);
    if (it == handles_.end()) return 0.0f;
    return it->second.position;
}

}
