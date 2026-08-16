#include "openwow/audio/playback/sound_runtime_internal.h"

namespace openwow::audio {

int SoundRuntime::PlaySoundKitByName(const std::string_view name, const float *position,
                                       std::uint32_t *handle_out,
                                       const SoundKitPlaybackOptions &options) {
  if (!sound_engine_initialized_) {
    return 17;
  }
  const std::uint32_t kit_id = LookupSoundKitIdByName(name);
  if (kit_id == 0) {
    return 7;
  }
  return PlaySoundKit(kit_id, position, handle_out, options);
}

int SoundRuntime::PlaySoundKit(const std::uint32_t sound_kit_id, const float *position,
                                 std::uint32_t *handle_out,
                                 const SoundKitPlaybackOptions &options) {
  if (sound_kit_id == 0)
    return 5;

  const auto *kit = GetSoundKitData(sound_kit_id);
  if (!kit)
    return 5;
  if (kit->file_count == 0)
    return 6;

  if (!sound_engine_initialized_)
    return 17;

  const std::uint32_t sound_type = ResolvePlaybackSoundType(options);

  if (cvar_get_bool_cb_ && !cvar_get_bool_cb_("Sound_EnableAllSound")) {
    return 17;
  }

  if (sound_type >= kSoundTypeLabels.size()) {
    return 8;
  }

  if (active_sound_type_counts_[sound_type] >= kSoundTypeMaxActiveCounts[sound_type]) {
    return 14;
  }

  if (SoundTypeUsesSfxCVar(sound_type) && cvar_get_bool_cb_ != nullptr &&
      !cvar_get_bool_cb_("Sound_EnableSFX")) {
    return 9;
  }

  if (SoundTypeUsesMusicCVar(sound_type) && cvar_get_bool_cb_ != nullptr &&
      !cvar_get_bool_cb_("Sound_EnableMusic")) {
    return 10;
  }

  if (SoundTypeUsesAmbienceCVar(sound_type) && cvar_get_bool_cb_ != nullptr &&
      !cvar_get_bool_cb_("Sound_EnableAmbience")) {
    return 11;
  }

  if (position == nullptr && non_positional_playback_block_depth_ != 0) {
    return 9;
  }

  ApplyExclusiveRepeatModeToSoundKit(sound_kit_id, options.exclusivity_mode);
  const bool tracks_exclusive_kit = ResolveExclusiveRepeatTracking(*kit, options);
  if (tracks_exclusive_kit &&
      active_exclusive_sound_kits_.find(sound_kit_id) != active_exclusive_sound_kits_.end()) {
    return 15;
  }

  if (options.allow_advanced_kit_properties && kit->advanced.has_value()) {
    return QueueAdvancedSoundKitPlayback(*kit, options, position, handle_out);
  }

  const std::int32_t preload_queue_hint = ResolvePlaybackPreloadQueueHint(sound_type, options);
  const auto selected_file = SelectSoundKitFileForPlayback(
      *kit, options.variation_mode, options.forced_file_index, preload_queue_hint);
  if (!selected_file.has_value()) {
    return 16;
  }

  std::uint32_t handle = AllocateSoundHandle();
  auto &sh = active_handles_[handle];
  ++active_sound_type_counts_[sound_type];
  if (tracks_exclusive_kit) {
    active_exclusive_sound_kits_.insert(sound_kit_id);
  }
  sh.sound_kit_id = sound_kit_id;
  sh.sound_type = sound_type;
  sh.playback_priority = options.playback_priority;
  sh.max_audible_behavior = ResolveMaxAudibleBehavior(options);
  sh.max_audible_mute_fade_speed = options.max_audible_mute_fade_speed;
  sh.selected_file_index = selected_file->index;
  sh.sound_model_override = options.sound_model_override;
  sh.has_active_sound = true;
  sh.is_playing = true;
  sh.loops = ResolveLoopingPlayback(*kit, options);
  sh.bypass_virtual_play_window = options.force_ambient_loop;
  sh.min_distance = kit->min_distance;
  sh.max_distance = options.max_distance_override > -1.0f
                        ? options.max_distance_override
                        : kit->max_distance;
  sh.tracks_instance_limit = true;
  sh.tracks_exclusive_kit = tracks_exclusive_kit;
  sh.source_label = ResolvePlaybackSourceLabel(*kit);
  sh.selected_file_path = std::string(selected_file->path);
  sh.relative_volume = {};
  ApplySoundKitFadeOptionsToHandle(sh, options);

  const SoundKitPlaybackVolume resolved_volume =
      ResolveSoundKitPlaybackVolume(*kit, options);
  sh.relative_volume.SetDirectVolume(resolved_volume.direct_volume);

  if (position) {
    sh.has_position = true;
    sh.position[0] = position[0];
    sh.position[1] = position[1];
    sh.position[2] = position[2];
  }

  sh.relative_volume.SetPlaybackScale(resolved_volume.playback_scale);

  if (!MaterializeSoundHandle(sh)) {
    CleanupHandlePlaybackState(sh);
    active_handles_.erase(handle);
    if (handle_out != nullptr) {
      *handle_out = 0;
    }
    return 16;
  }

  if (handle_out)
    *handle_out = handle;

  return 0;
}

int SoundRuntime::PlayVoiceChatToggle(std::uint32_t sound_kit_id) {
  if ((voice_chat_toggle_lookup_flags_ & kVoiceChatToggleOnLookupResolvedBit) == 0) {
    voice_chat_toggle_lookup_flags_ |= kVoiceChatToggleOnLookupResolvedBit;
    voice_chat_on_kit_ = LookupSoundKitIdByName("VoiceChatOn");
  }
  if ((voice_chat_toggle_lookup_flags_ & kVoiceChatToggleOffLookupResolvedBit) == 0) {
    voice_chat_toggle_lookup_flags_ |= kVoiceChatToggleOffLookupResolvedBit;
    voice_chat_off_kit_ = LookupSoundKitIdByName("VoiceChatOff");
  }

  SoundKitPlaybackOptions options;
  options.playback_priority = kVoiceChatTogglePlaybackPriority;
  if (sound_kit_id == voice_chat_on_kit_ || sound_kit_id == voice_chat_off_kit_) {
    options.sound_type = 3;
  }
  options.exclusivity_mode = SoundKitExclusivityMode::kEnableExclusiveRepeat;
  return PlaySoundKit(sound_kit_id, nullptr, nullptr, options);
}

void SoundRuntime::PlayErrorSpeech(const std::uint32_t sound_kit_id) {
  if (error_speech_handle_id_ != 0 && IsSoundHandlePlaying(error_speech_handle_id_)) return;
  SoundKitPlaybackOptions options{};
  options.sound_type = kErrorSpeechPlaybackSoundType;
  (void)PlaySoundKit(sound_kit_id, nullptr, &error_speech_handle_id_, options);
}

int SoundRuntime::PlayScriptSound(const std::string &path, std::uint32_t sound_type) {

  if (cvar_get_bool_cb_ && !cvar_get_bool_cb_("Sound_EnableAllSound")) {
    return 17;
  }

  if (IsMovieAudioPlaying()) {
    return 17;
  }

  if (!sound_engine_->IsInitialized() || !audio_engine_->IsInitialized()) {
    return 17;
  }

  if (sound_type == 5) {
    if (cvar_get_bool_cb_ && !cvar_get_bool_cb_("Sound_EnableMusic")) {
      return 10;
    }
  }

  if (sound_type == 5 && script_music_handle_id_.has_value()) {
    FreeSoundHandle(*script_music_handle_id_);
  }

  std::uint32_t handle = AllocateSoundHandle();
  auto &sh = active_handles_[handle];
  sh.sound_kit_id = 0;
  sh.sound_type = sound_type;
  sh.has_active_sound = true;
  sh.is_playing = true;
  sh.loops = sound_type == 5;
  sh.source_label = sound_type == 4 ? "<SCRIPT SOUND>" : "<SCRIPT MUSIC>";
  sh.selected_file_path = path;
  sh.relative_volume = {};

  auto &engine = *audio_engine_;
  AudioClipInfo clip;
  clip.clipId = handle;
  clip.path = path;
  clip.channel = ResolveAudioPlaybackChannel(sound_type);
  clip.volume = sh.relative_volume.channel_volume;
  clip.loop = sh.loops;
  const auto engine_handle = engine.Play(clip);
  if (engine_handle.handleId == 0 || !engine.IsHandleMaterialized(engine_handle)) {
    if (engine_handle.handleId != 0) {
      engine.DestroyHandle(engine_handle);
    }
    active_handles_.erase(handle);
    return 16;
  }
  sh.audio_engine_handle_id = engine_handle.handleId;

  if (sound_type == 5) {
    script_music_handle_id_ = handle;
    script_music_playing_ = true;
  }

  return 0;
}

int SoundRuntime::StopScriptMusic() {
  const auto handle_id = script_music_handle_id_;
  if (!handle_id.has_value() && !script_music_playing_) {
    return 0;
  }

  if (handle_id.has_value()) {

    (void)RequestStopSoundHandle(*handle_id, 0.5F);
  }
  DetachScriptMusicHandle();

  return 0;
}

int SoundRuntime::StopScriptMusicImmediately() {
  const auto handle_id = script_music_handle_id_;
  if (!handle_id.has_value() && !script_music_playing_) {
    return 0;
  }

  if (handle_id.has_value()) {
    FreeSoundHandle(*handle_id);
  } else {
    DetachScriptMusicHandle();
  }

  return 0;
}

void SoundRuntime::DetachScriptMusicHandle() {
  script_music_handle_id_.reset();
  script_music_playing_ = false;
}

void SoundRuntime::ResetZoneAndScriptMusicRuntime() {
  StopScriptMusic();

  std::vector<std::uint32_t> handles_to_clear;
  handles_to_clear.reserve(active_handles_.size());
  for (const auto &[handle_id, handle] : active_handles_) {
    if (!SoundTypeUsesMusicCVar(handle.sound_type)) {
      continue;
    }
    handles_to_clear.push_back(handle_id);
  }

  for (const auto handle_id : handles_to_clear) {
    (void)StopActiveSoundHandle(handle_id, true, -1.0f, true);
  }

  zone_music_delay_deadline_ms_ = 0;
  ResetCurrentZoneMusicRuntime();
  play_music_runtime_.Reset();
}

bool SoundRuntime::IsMusicCVarEnabled() const {
  return cvar_get_bool_cb_ == nullptr || cvar_get_bool_cb_("Sound_EnableMusic");
}

bool SoundRuntime::GetActivePlayerPosition(float *position_out) const {
  if (!position_out) {
    return false;
  }
  return active_player_position_cb_ && active_player_position_cb_(position_out);
}

const LiquidTypeSoundData *
SoundRuntime::GetLiquidTypeSoundData(const std::uint32_t liquid_type_id) const {
  const auto it = liquid_type_sound_data_.find(liquid_type_id);
  if (it == liquid_type_sound_data_.end()) {
    return nullptr;
  }
  return &it->second;
}

void SoundRuntime::RequestLiquidAmbienceStop() {
  if (!liquid_ambience_.handle_id.has_value()) {
    return;
  }
  const std::uint32_t handle_id = *liquid_ambience_.handle_id;

  if (!RequestStopSoundHandle(handle_id, -1.0f)) {
    liquid_ambience_.handle_id.reset();
    liquid_ambience_.stop_pending = false;
    return;
  }
  liquid_ambience_.stop_pending = true;
}

void SoundRuntime::ClearLiquidAmbienceRuntime() {
  if (liquid_ambience_.handle_id.has_value()) {
    FreeSoundHandle(*liquid_ambience_.handle_id);
    liquid_ambience_.handle_id.reset();
  }

  liquid_query_result_buffer_.Reset();
  liquid_ambience_ = {};
}

void EmotesTextSoundTable::Load(std::uint32_t max_emote_text_id) {
  max_emote_text_id_ = max_emote_text_id;
  table_.clear();
  entry_count_ = 0;
}

void EmotesTextSoundTable::Insert(std::uint32_t emote_text_id, std::uint32_t race_id,
                                  std::uint32_t gender_id, std::uint32_t sound_kit_id) {
  if (race_id >= kMaxRaces || gender_id >= kMaxGenders)
    return;
  if (emote_text_id >= max_emote_text_id_)
    return;

  auto [it, inserted] = table_.try_emplace(emote_text_id);
  if (inserted) {
    ++entry_count_;
  }

  auto &arr = it->second;

  arr[gender_id + 2 * race_id] = sound_kit_id;
}

}
