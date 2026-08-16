#include "openwow/audio/playback/sound_engine.h"
#include "openwow/audio/playback/audio_engine.h"

#include <algorithm>
#include <chrono>
#include <memory>

namespace {
std::uint32_t GetTickCountMs() {
  using namespace std::chrono;
  return static_cast<std::uint32_t>(duration_cast<milliseconds>(
      steady_clock::now().time_since_epoch()).count());
}
}

namespace openwow::audio {

std::uint32_t SoundEngine::HashCI(const char* str) {
  std::uint32_t h = 0;
  if (!str) return 0;
  for (; *str; ++str) {
    char c = *str;
    if (c >= 'A' && c <= 'Z') c += 32;
    h = h * 33 + static_cast<std::uint8_t>(c);
  }
  return h;
}

float SoundEngine::ComputeEffectiveVolume(SoundObj* obj) const {
  if (!initialized_ || obj == nullptr || channel_groups_.empty()) {
    return 0.0f;
  }

  float volume = obj->direct_volume;
  if (obj->fade_in_active || obj->fade_out_active) {
    volume *= obj->fade_volume;
  }

  volume *= ComputeChannelGroupGain(*obj);

  if (obj->kind != kSoundKind_Capture && sound_audibility_callback_ != nullptr) {
    volume *= sound_audibility_callback_(obj->audibility_token);
  }

  return volume;
}

float SoundEngine::ComputeChannelGroupGain(const SoundObj& obj) const {
  if (channel_groups_.empty()) {
    return 0.0f;
  }

  float gain = 1.0f;
  int group_index = obj.channel_group_index;

  while (group_index > 0 &&
         group_index < static_cast<int>(channel_groups_.size())) {
    const auto& group = channel_groups_[static_cast<std::size_t>(group_index)];
    gain *= group.volume * group.effective_volume;
    group_index = group.parent_index;
  }

  return gain;
}

void SoundEngine::ApplyResolvedSoundVolume(SoundObj* obj) const {
  if (obj == nullptr || obj->playback_handle_id == 0) {
    return;
  }

  audio_engine_.SetHandleVolume(
      AudioPlaybackHandle{obj->playback_handle_id},
      ComputeEffectiveVolume(obj));
}

bool SoundEngine::CanDestroyExpiredSound(const SoundObj& sound) const {
  if (sound.playback_handle_id == 0) {
    return true;
  }

  return !audio_engine_.IsHandlePlaying(
      AudioPlaybackHandle{sound.playback_handle_id});
}

void SoundEngine::ReleaseExpiredSoundHandle(SoundObj* sound) {
  if (sound == nullptr || sound->playback_handle_id == 0) {
    return;
  }

  UnregisterPlaybackDspHead(sound->playback_handle_id);

  auto& audio = audio_engine_;
  const AudioPlaybackHandle handle{sound->playback_handle_id};
  audio.Stop(handle);
  audio.DestroyHandle(handle);
  sound->playback_handle_id = 0;
}
bool SoundEngine::SetSoundDistances(SoundObj* obj,
                                     float min_dist,
                                     float max_dist) {
  if (!initialized_ || obj == nullptr) {
    return true;
  }
  obj->min_distance = std::max(0.0f, min_dist);
  obj->max_distance = std::max(std::max(0.0f, max_dist), obj->min_distance);
  if (obj->playback_handle_id != 0) {
    audio_engine_.SetHandleDistanceRange(
        AudioPlaybackHandle{obj->playback_handle_id},
        obj->min_distance, obj->max_distance);
  }
  return true;
}

void SoundEngine::SetSound3DPosition(SoundObj* obj, const float* pos3) {
  if (!initialized_ || obj == nullptr || pos3 == nullptr) {
    return;
  }
  obj->pos_x = pos3[0];
  obj->pos_y = pos3[1];
  obj->pos_z = pos3[2];
  if (obj->playback_handle_id != 0) {
    audio_engine_.SetSound3DPosition(
        AudioPlaybackHandle{obj->playback_handle_id},
        pos3[0], pos3[1], pos3[2]);
  }
}

void SoundEngine::SetMasterVolume(float volume) {
  if (!initialized_) return;
  if (volume < 0.0f || volume > 1.0f) return;

  if (!channel_groups_.empty()) {
    channel_groups_[0].volume = volume;
    channel_groups_[0].dirty = true;
  }
  audio_engine_.SetMasterVolume(volume);
}

void SoundEngine::SetMasterMuted(const bool muted) {
  if (!initialized_) return;

  if (!channel_groups_.empty()) {
    channel_groups_[0].effective_volume = muted ? 0.0f : 1.0f;
    channel_groups_[0].dirty = true;
  }
  audio_engine_.SetMuted(muted);
}

void SoundEngine::SetSoundAudibilityCallback(const SoundAudibilityCallback callback) {
  sound_audibility_callback_ = callback;
}

void SoundEngine::ApplyChannelGroupVolume(SoundObj* obj) {
  if (!initialized_ || obj == nullptr || obj->playback_handle_id == 0) {
    return;
  }

  ApplyResolvedSoundVolume(obj);
}

void SoundEngine::PropagatePitch(std::size_t group_index) {
  if (group_index >= channel_groups_.size()) {
    return;
  }

  auto& group = channel_groups_[group_index];

  float parent_effective = 1.0f;
  if (group.parent_index >= 0 &&
      static_cast<std::size_t>(group.parent_index) < channel_groups_.size()) {
    parent_effective = channel_groups_[group.parent_index].effective_pitch;
  }
  group.effective_pitch = parent_effective * group.pitch;

  for (std::size_t i = 0; i < channel_groups_.size(); ++i) {
    if (i != group_index &&
        channel_groups_[i].parent_index ==
            static_cast<std::int32_t>(group_index)) {
      PropagatePitch(i);
    }
  }

}

void SoundEngine::PropagatePitchByName(const char* group_name) {
  if (!initialized_ || group_name == nullptr) {
    return;
  }
  const std::uint32_t h = HashCI(group_name);
  for (std::size_t i = 0; i < channel_groups_.size(); ++i) {
    if (channel_groups_[i].name_hash == h) {
      PropagatePitch(i);
      return;
    }
  }
}

bool SoundEngine::IsGroupPaused(std::size_t group_index) const {
  while (group_index < channel_groups_.size()) {
    if (channel_groups_[group_index].pause_state) {
      return true;
    }
    const int parent = channel_groups_[group_index].parent_index;
    if (parent < 0) {
      break;
    }
    group_index = static_cast<std::size_t>(parent);
  }
  return false;
}

void SoundEngine::UpdatePauseMuteState(SoundObj* obj, bool paused_by_group) {
  if (obj == nullptr || obj->playback_handle_id == 0) {
    return;
  }

  const bool effectively_muted =
      paused_by_group ||
      (obj->channel_group_index >= 0 &&
       IsGroupPaused(
           static_cast<std::size_t>(obj->channel_group_index)));

  if (effectively_muted) {

    audio_engine_.SetHandleVolume(
        AudioPlaybackHandle{obj->playback_handle_id}, 0.0f);
  } else {

    ApplyResolvedSoundVolume(obj);
  }
}

void SoundEngine::PropagatePauseState(std::size_t group_index,
                                       bool paused) {
  if (group_index >= channel_groups_.size()) {
    return;
  }

  channel_groups_[group_index].pause_state = paused;

  for (std::size_t i = 0; i < channel_groups_.size(); ++i) {
    if (i != group_index &&
        channel_groups_[i].parent_index ==
            static_cast<std::int32_t>(group_index)) {
      PropagatePauseState(i, paused);
    }
  }

  for (auto& sound : expired_sounds_) {
    if (sound && sound->channel_group_index ==
                     static_cast<std::int32_t>(group_index)) {
      const bool obj_paused =
          IsGroupPaused(static_cast<std::size_t>(
              sound->channel_group_index));
      UpdatePauseMuteState(sound.get(), obj_paused);
    }
  }
}

bool SoundEngine::IsGroupMuted(std::size_t group_index) const {
  while (group_index < channel_groups_.size()) {
    if (channel_groups_[group_index].mute_state) {
      return true;
    }
    const int parent = channel_groups_[group_index].parent_index;
    if (parent < 0) {
      break;
    }
    group_index = static_cast<std::size_t>(parent);
  }
  return false;
}

void SoundEngine::UpdateMuteVolume(SoundObj* obj, bool muted_by_group) {
  if (obj == nullptr || obj->playback_handle_id == 0) {
    return;
  }

  const bool effectively_muted =
      muted_by_group ||
      (obj->channel_group_index >= 0 &&
       IsGroupMuted(
           static_cast<std::size_t>(obj->channel_group_index)));

  if (effectively_muted) {

    audio_engine_.SetHandleVolume(
        AudioPlaybackHandle{obj->playback_handle_id}, 0.0f);
  } else {

    ApplyResolvedSoundVolume(obj);
  }
}

void SoundEngine::PropagateMuteState(std::size_t group_index,
                                      bool muted) {
  if (group_index >= channel_groups_.size()) {
    return;
  }

  channel_groups_[group_index].mute_state = muted;

  for (std::size_t i = 0; i < channel_groups_.size(); ++i) {
    if (i != group_index &&
        channel_groups_[i].parent_index ==
            static_cast<std::int32_t>(group_index)) {
      PropagateMuteState(i, muted);
    }
  }

  for (auto& sound : expired_sounds_) {
    if (sound && sound->channel_group_index ==
                     static_cast<std::int32_t>(group_index)) {
      const bool obj_muted =
          IsGroupMuted(static_cast<std::size_t>(
              sound->channel_group_index));
      UpdateMuteVolume(sound.get(), obj_muted);
    }
  }
}

void SoundEngine::SetSoundVolume(SoundObj* obj, float volume) {
  if (obj == nullptr) {
    return;
  }

  obj->direct_volume = std::clamp(volume, 0.0f, 1.0f);
  if (!initialized_ || obj->playback_handle_id == 0) {
    return;
  }

  ApplyResolvedSoundVolume(obj);
}

bool SoundEngine::HasSoundGroupReachedInstanceLimit(const char* group_name,
                                                      std::uint32_t limit) const {
    if (limit == 0 || !group_name || !*group_name) return false;
    const std::uint32_t hash = HashCI(group_name);
    const auto it = sound_group_instance_counts_.find(hash);
    if (it == sound_group_instance_counts_.end()) return false;
    return it->second >= limit;
}

void SoundEngine::IncrementSoundGroupInstanceCount(const char* group_name) {
    if (!group_name || !*group_name) return;
    const std::uint32_t hash = HashCI(group_name);
    ++sound_group_instance_counts_[hash];
}

void SoundEngine::DecrementSoundGroupInstanceCount(const char* group_name) {
    if (!group_name || !*group_name) return;
    const std::uint32_t hash = HashCI(group_name);
    auto it = sound_group_instance_counts_.find(hash);
    if (it != sound_group_instance_counts_.end() && it->second > 0) {
        --it->second;
    }
}

void SoundEngine::PlaySound(SoundObj* obj) {

  if (obj != nullptr && obj->playback_handle_id != 0) {
    audio_engine_.Resume(
        AudioPlaybackHandle{obj->playback_handle_id});
  }
}

void SoundEngine::StopSound(SoundObj* obj, bool ,
                             float ) {
  if (obj == nullptr || obj->playback_handle_id == 0) {
    return;
  }
  audio_engine_.Stop(
      AudioPlaybackHandle{obj->playback_handle_id});
}

void SoundEngine::StopAllSounds() {
  std::lock_guard<std::mutex> lock(sound_mutex_);
  audio_engine_.StopNonVoiceSounds();
  LogLine(0, ".\\SoundEngine.cpp", 0, "StopAllSounds");
}

void SoundEngine::StartPlayback(SoundObj* obj) {
  if (!obj) return;
  PlaySound(obj);
}

bool SoundEngine::GetSoundIsPlaying(SoundObj* obj) const {
  if (obj == nullptr || obj->playback_handle_id == 0) {
    return false;
  }
  return audio_engine_.IsHandlePlaying(
      AudioPlaybackHandle{obj->playback_handle_id});
}

void SoundEngine::StartFadeIn(SoundObj* obj) {
  if (!obj) return;
  obj->fade_out_active = false;
  obj->fade_in_active = true;
}

void SoundEngine::StartFadeOut(SoundObj* obj) {
  if (!obj) return;
  obj->fade_in_active = false;
  obj->fade_out_active = true;
}

void SoundEngine::SetFadeInDuration(SoundObj* obj, const float duration) {
  if (!obj) return;
  obj->fade_in_duration = duration;
}

void SoundEngine::SetFadeOutDuration(SoundObj* obj, const float duration) {
  if (!obj) return;
  obj->fade_out_duration = duration;
}

bool SoundEngine::CreateSoundInstance(
    const char* filename,
    std::uint32_t flags,
    std::uint32_t handle_ptr,
    std::uint32_t fade_in_len,
    std::uint32_t fade_out_len,
    std::uint8_t  ,
    std::uint32_t ,
    std::uint32_t ,
    std::uint8_t  loop_sound,
    std::uint32_t ,
    std::uint32_t ,
    float , float , float ,
    const float*  ) {
  if (!initialized_ || !filename || !*filename) return false;

  LogLine(1633, ".\\SoundEngine.cpp", 0, "CreateSoundInstance: %s", filename);

  AudioClipInfo clip;
  clip.path = filename;
  clip.channel = PlaybackChannel::SFX;
  clip.volume = 1.0f;
  clip.loop = (flags & kSoundFlag_Loop) != 0 || loop_sound != 0;
  clip.fadeInTime = static_cast<float>(fade_in_len) / 1000.0f;
  clip.fadeOutTime = static_cast<float>(fade_out_len) / 1000.0f;

  AudioPlaybackHandle handle = audio_engine_.Play(clip);
  if (handle.handleId == 0) {
    LogLine(1633, ".\\SoundEngine.cpp", 0,
            "CreateSoundInstance: Play returned 0 for %s", filename);
    return false;
  }

  if (handle_ptr != 0) {
    *reinterpret_cast<std::uint32_t*>(
        static_cast<std::uintptr_t>(handle_ptr)) = handle.handleId;
  }

  RegisterPlaybackDspHead(handle.handleId);

  return true;
}
int SoundEngine::EnumActiveSounds(SESoundEnumEntry* out, int max_count,
                                   bool include_playing,
                                   bool include_paused,
                                   bool include_stopped,
                                   bool include_no_channel,
                                   bool include_virtual,
                                   int* playing_out, int* paused_out,
                                   int* stopped_out, int* no_channel_out,
                                   int* virtual_out) {
  if (!out || max_count <= 0) return 0;
  if (playing_out)    *playing_out    = 0;
  if (paused_out)     *paused_out     = 0;
  if (stopped_out)    *stopped_out    = 0;
  if (no_channel_out) *no_channel_out = 0;
  if (virtual_out)    *virtual_out    = 0;

  auto& engine = audio_engine_;
  if (!engine.IsInitialized()) return 0;

  auto active = engine.GetActiveHandlesInfo();
  int written = 0;

  for (const auto& info : active) {
    if (written >= max_count) break;

    int type = 0;
    bool is_no_channel = false;
    switch (info.state) {
      case AudioPlaybackState::Playing:
      case AudioPlaybackState::FadingIn:
      case AudioPlaybackState::FadingOut:
        if (info.is_virtual) {
          type = 1;
          is_no_channel = true;
        } else {
          type = 2;
        }
        break;
      case AudioPlaybackState::Paused:
        type = 3;
        break;
      case AudioPlaybackState::Stopped:
        type = 4;
        break;
    }

    if (type == 2 && !include_playing)         continue;
    if (type == 3 && !include_paused)          continue;
    if (type == 4 && !include_stopped)         continue;
    if (is_no_channel && !include_no_channel)  continue;
    if (info.is_virtual && !include_virtual)   continue;

    out[written].type               = type;
    out[written].pos_x              = info.pos_x;
    out[written].pos_y              = info.pos_y;
    out[written].pos_z              = info.pos_z;
    out[written].min_dist           = info.min_dist;
    out[written].max_dist           = info.max_dist;
    out[written].sound_handle       = info.handleId;
    out[written].volume             = info.volume;
    out[written].audibility         = info.volume;
    out[written].distance_sq        = info.distance_sq;
    out[written].frequency          = info.frequency;
    out[written].channel_group_handle = 0;
    out[written].is_3d              = info.is_3d;
    out[written].is_fading_in       = info.fading_in;
    out[written].is_fading_out      = info.fading_out;
    out[written].is_virtual         = info.is_virtual;
    out[written].flags              = static_cast<std::int32_t>(info.creation_flags);

    if (playing_out    && type == 2)          *playing_out    += 1;
    if (paused_out     && type == 3)          *paused_out     += 1;
    if (stopped_out    && type == 4)          *stopped_out    += 1;
    if (no_channel_out && is_no_channel)      *no_channel_out += 1;
    if (virtual_out    && info.is_virtual)    *virtual_out    += 1;

    ++written;
  }

  return written;
}
SEChannelGroup* SoundEngine::FindOrCreateChannelGroup(
    const char* name, const bool create_if_missing, const bool attach_to_master) {
  if (!initialized_) return nullptr;
  std::uint32_t h = HashCI(name);

  for (std::size_t i = 1; i < channel_groups_.size(); ++i) {
    if (channel_groups_[i].name_hash == h)
      return &channel_groups_[i];
  }

  if (!create_if_missing) return nullptr;

  SEChannelGroup g;
  g.name_hash = h;
  g.parent_index = attach_to_master && !channel_groups_.empty() ? 0 : -1;
  g.volume = 1.0f;
  g.effective_volume = 1.0f;
  g.pitch = 1.0f;
  g.effective_pitch = 1.0f;
  g.dirty = true;
  channel_groups_.push_back(g);
  return &channel_groups_.back();
}

float SoundEngine::GetChannelGroupCompositeVolume(const char* name) const {
  if (!initialized_) {
    return 0.0f;
  }

  if (name == nullptr) {
    return 1.0f;
  }

  const std::uint32_t hash = HashCI(name);
  for (std::size_t index = 1; index < channel_groups_.size(); ++index) {
    const auto& group = channel_groups_[index];
    if (group.name_hash == hash) {
      return group.volume * group.effective_volume;
    }
  }

  return 1.0f;
}

void SoundEngine::SetChannelGroupVolume(const char* name,
                                        const float volume,
                                        const bool attach_to_master) {
  if (!initialized_ || name == nullptr || volume < 0.0f || volume > 1.0f) {
    return;
  }

  if (auto* group = FindOrCreateChannelGroup(name, true, attach_to_master)) {
    group->volume = volume;
    group->dirty = true;
  }
}

void SoundEngine::SetChannelGroupMuted(const char* name, const bool muted) {
  if (!initialized_ || name == nullptr) {
    return;
  }

  if (auto* group = FindOrCreateChannelGroup(name, false, false)) {
    group->effective_volume = muted ? 0.0f : 1.0f;
    group->dirty = true;
  }
}

void SoundEngine::AssignChannelGroup(SoundObj* obj,
                                     const char* group_name,
                                     const bool attach_to_master) {
  if (!initialized_ || obj == nullptr || group_name == nullptr) {
    return;
  }

  auto* group = FindOrCreateChannelGroup(group_name, true, attach_to_master);
  if (group == nullptr || channel_groups_.empty()) {
    return;
  }

  obj->channel_group_index =
      static_cast<std::int32_t>(group - channel_groups_.data());
  if (obj->playback_handle_id != 0) {
    ApplyResolvedSoundVolume(obj);
  }
}

void SoundEngine::StopChannelGroupSounds(const char* group_name,
                                          const float fade_time) {
  if (!initialized_ || group_name == nullptr) return;
  auto* grp = FindOrCreateChannelGroup(group_name, false, false);
  if (!grp) return;

  const float fade_seconds = std::max(fade_time, 0.0f);
  if (grp->name_hash == HashCI("SFX")) {
    auto& audio = audio_engine_;
    audio.StopAllSounds(fade_seconds * 1000.0f);
    audio.StopHandleChannel(PlaybackChannel::SFX, fade_seconds);
  }

  LogLine(0, ".\\SoundEngine.cpp", 0,
          "StopChannelGroupSounds: group hash 0x%08X", grp->name_hash);
}

void SoundEngine::PurgeSoundCache(bool force_all) {
  if (force_all) {
    audio_engine_.ClearCache();
  }
}

void SoundEngine::DestroyCacheLock() {
  cache_lock_destroyed_ = true;
}

void SoundEngine::UpdatePositions() {
  if (!initialized_) {
    return;
  }

  std::lock_guard<std::mutex> lock(sound_mutex_);

  for (auto& sound : expired_sounds_) {
    if (!sound || sound->playback_handle_id == 0) {
      continue;
    }
    if (!(sound->creation_flags & kSoundFlag_3D)) {
      continue;
    }

    float pos[3] = {sound->pos_x, sound->pos_y, sound->pos_z};
    bool pos_obtained = false;

    if (position_callback_ != nullptr &&
        (sound->guid_lo != 0 || sound->guid_hi != 0)) {
      pos_obtained = position_callback_(sound->guid_lo, sound->guid_hi, pos);
    }

    if (!pos_obtained) {
      pos[0] = sound->pos_x;
      pos[1] = sound->pos_y;
      pos[2] = sound->pos_z;
    }

    audio_engine_.SetSound3DPosition(
        AudioPlaybackHandle{sound->playback_handle_id},
        pos[0], pos[1], pos[2]);
  }
}

void SoundEngine::CompleteNonBlockingLoad(SoundObj* obj,
                                           int result_code) {
  if (result_code != 0 && result_code != 41) {
    LogLine(5152, ".\\SoundEngine.cpp", 0,
            "##### ERROR In CompleteNonBlockingLoad! code=%d", result_code);

    if (obj && obj->playback_handle_id != 0) {
      audio_engine_.Stop(
          AudioPlaybackHandle{obj->playback_handle_id});
      audio_engine_.DestroyHandle(
          AudioPlaybackHandle{obj->playback_handle_id});
      obj->playback_handle_id = 0;
    }
  }
}

void SoundEngine::CleanupFinishedSounds() {
  std::lock_guard<std::mutex> lock(sound_mutex_);
  auto& audio = audio_engine_;

  for (auto& sound : expired_sounds_) {
    if (!sound || sound->playback_handle_id == 0) {
      continue;
    }

    const AudioPlaybackHandle handle{sound->playback_handle_id};

    if (audio.IsHandlePlaying(handle) &&
        !audio.IsHandleChannelActive(handle)) {
      audio.Stop(handle);
    }

    if (!audio.IsHandlePlaying(handle)) {
      sound->cleanup_remaining_ms = 0;
    }
  }
}

void SoundEngine::CleanupExpiredSounds(const bool force) {
  if (!expired_sound_cleanup_clock_initialized_) {
    expired_sound_cleanup_clock_initialized_ = true;
    expired_sound_cleanup_tick_ms_ = GetTickCountMs();
  }

  const std::uint32_t now = GetTickCountMs();
  const std::int32_t elapsed_ms =
      static_cast<std::int32_t>(now - expired_sound_cleanup_tick_ms_);

  std::lock_guard<std::mutex> lock(sound_mutex_);
  expired_sounds_.erase(
      std::remove_if(expired_sounds_.begin(), expired_sounds_.end(),
                     [&](std::unique_ptr<SoundObj>& sound) {
                       if (!sound) {
                         return true;
                       }

                       sound->cleanup_remaining_ms -= elapsed_ms;
                       if (!force &&
                           (sound->cleanup_remaining_ms > 0 ||
                            !CanDestroyExpiredSound(*sound))) {
                         return false;
                       }

                       ReleaseExpiredSoundHandle(sound.get());
                       return true;
                     }),
      expired_sounds_.end());
  expired_sound_cleanup_tick_ms_ = now;
}

void SoundEngine::ReleaseSoundObj(SoundObj* obj) {
  if (!obj) {
    return;
  }

  obj->cached_data_handle = 0;

  obj->cache_ref_count = 0;
  obj->cache_flags     = 0;

  obj->in_cache_list = false;
}

void SoundEngine::CleanupSoundObj(SoundObj* obj) {
  if (!obj) {
    return;
  }

  ReleaseExpiredSoundHandle(obj);

  ReleaseSoundObj(obj);
}

}
