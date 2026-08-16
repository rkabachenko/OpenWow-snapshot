#include "openwow/audio/playback/sound_runtime_internal.h"

namespace openwow::audio {

void SoundRuntime::ResetCurrentZoneAmbienceRuntime() {
  DetachHandleBinding(zone_ambience_player_.ActiveSlot().handle_binding);
  zone_ambience_player_.ActiveSlot() = {};
}

void SoundRuntime::SetCurrentZoneAmbienceRuntime(const std::int32_t sound_kit_id,
                                                   const int priority, const bool is_playing) {
  ResetCurrentZoneAmbienceRuntime();
  auto &slot = zone_ambience_player_.ActiveSlot();
  slot.sound_kit_id = static_cast<std::uint32_t>(sound_kit_id);
  slot.zone_sound_type = priority;
  slot.is_playing_override = is_playing;
}

bool SoundRuntime::BindCurrentZoneAmbienceHandle(const std::uint32_t handle_id) {
  auto &slot = zone_ambience_player_.ActiveSlot();
  if (!AttachHandleBinding(slot.handle_binding, handle_id)) {
    slot.follows_handle = false;
    return false;
  }

  slot.follows_handle = true;
  return true;
}

bool SoundRuntime::IsCurrentZoneAmbiencePlaying() const {
  const auto &slot = zone_ambience_player_.ActiveSlot();
  if (!slot.follows_handle) {
    return slot.is_playing_override;
  }

  const std::uint32_t handle_id = slot.handle_binding.active_handle_id;
  return handle_id != 0 && IsSoundHandlePlaying(handle_id);
}

std::int32_t SoundRuntime::GetCurrentZoneAmbienceSoundKitId() const {
  if (!IsCurrentZoneAmbiencePlaying()) {
    return 0;
  }
  return static_cast<std::int32_t>(zone_ambience_player_.ActiveSlot().sound_kit_id);
}

void SoundRuntime::ResetZoneAmbienceRuntime() {

  for (auto &slot : zone_ambience_player_.slots) {
    const std::uint32_t handle_id = slot.handle_binding.active_handle_id;
    if (handle_id != 0) {
      (void)StopActiveSoundHandle(handle_id, true, -1.0f, true);
    }
    DetachHandleBinding(slot.handle_binding);
  }
  zone_ambience_player_ = {};
}

void SoundRuntime::ResetCurrentZoneMusicRuntime() {
  DetachHandleBinding(zone_music_player_.ActiveSlot().handle_binding);
  zone_music_player_.ActiveSlot() = {};
  zone_music_stop_action_ = {};
}

void SoundRuntime::ResetZoneMusicRuntime() {

  for (auto &slot : zone_music_player_.slots) {
    const std::uint32_t handle_id = slot.handle_binding.active_handle_id;
    if (handle_id != 0) {
      (void)StopActiveSoundHandle(handle_id, true, -1.0f, true);
    }
    DetachHandleBinding(slot.handle_binding);
  }
  zone_music_player_ = {};
  zone_music_stop_action_ = {};
}

void SoundRuntime::SetCurrentZoneMusicRuntime(const std::int32_t sound_kit_id, const int priority,
                                                const bool is_playing) {
  ResetCurrentZoneMusicRuntime();
  auto &slot = zone_music_player_.ActiveSlot();
  slot.sound_kit_id = static_cast<std::uint32_t>(sound_kit_id);
  slot.zone_sound_type = priority;
  slot.is_playing_override = is_playing;
}

void SoundRuntime::InitializePlayMusicRuntime(const std::int32_t sound_kit_id) {
  play_music_runtime_.Arm(sound_kit_id);
}

void SoundRuntime::ArmZoneMusicSelectionDelayStopAction(const std::int32_t expected_sound_kit_id,
                                                          const std::uint32_t repeat_delay_min_ms,
                                                          const std::uint32_t repeat_delay_max_ms) {
  zone_music_stop_action_ = {
      .kind = ZoneMusicStopActionKind::kSelectionDelayGate,
      .expected_sound_kit_id = expected_sound_kit_id,
      .repeat_delay_min_ms = repeat_delay_min_ms,
      .repeat_delay_max_ms = repeat_delay_max_ms,
  };
}

void SoundRuntime::ArmZoneMusicRuntimeResetStopAction() {

  zone_music_stop_action_ = {
      .kind = ZoneMusicStopActionKind::kResetRuntimeOnStop,
  };
}

void SoundRuntime::ArmZoneMusicRepeatDelayDeadlineStopAction(
    const std::int32_t sound_kit_id, const std::uint32_t repeat_delay_minutes) {
  zone_music_stop_action_ = {
      .kind = ZoneMusicStopActionKind::kSpecialRepeatDeadline,
      .expected_sound_kit_id = sound_kit_id,
      .repeat_delay_min_ms = repeat_delay_minutes,
  };
}

void SoundRuntime::ClearZoneMusicStopAction() {
  zone_music_stop_action_ = {};
}

bool SoundRuntime::BindCurrentZoneMusicHandle(const std::uint32_t handle_id) {
  auto &slot = zone_music_player_.ActiveSlot();
  if (!AttachHandleBinding(slot.handle_binding, handle_id)) {
    slot.follows_handle = false;
    return false;
  }

  slot.follows_handle = true;
  return true;
}

bool SoundRuntime::IsCurrentZoneMusicPlaying() const {
  const auto &slot = zone_music_player_.ActiveSlot();
  if (!slot.follows_handle) {
    return slot.is_playing_override;
  }

  const std::uint32_t handle_id = slot.handle_binding.active_handle_id;
  return handle_id != 0 && IsSoundHandlePlaying(handle_id);
}

void SoundRuntime::ApplyZoneMusicSelectionDelayStopAction() {
  const auto stop_action = zone_music_stop_action_;
  ClearZoneMusicStopAction();
  if (stop_action.kind != ZoneMusicStopActionKind::kSelectionDelayGate ||
      SelectZoneMusic(nullptr, nullptr, nullptr) != stop_action.expected_sound_kit_id) {
    zone_music_delay_deadline_ms_ = 0;
    return;
  }

  const std::uint32_t now_ms = openwow::core::GameClock::GetTickCount32();
  if (stop_action.repeat_delay_max_ms > stop_action.repeat_delay_min_ms) {
    if (!zone_music_delay_rng_initialized_) {
      zone_music_delay_rng_initialized_ = true;
      zone_music_delay_rng_ = retail_rng::MakeAdlerSeedState(0u);
    }

    zone_music_delay_deadline_ms_ =
        now_ms + stop_action.repeat_delay_min_ms +
        retail_rng::AdlerSeedNextBoundedValue(
            stop_action.repeat_delay_max_ms - stop_action.repeat_delay_min_ms,
            zone_music_delay_rng_);
    return;
  }

  zone_music_delay_deadline_ms_ = now_ms + stop_action.repeat_delay_max_ms;
}

void SoundRuntime::ApplyZoneMusicRepeatDelayDeadlineStopAction(
    const std::int32_t sound_kit_id, const std::uint32_t repeat_delay_minutes) {
  if (sound_kit_id < 0 ||
      static_cast<std::size_t>(sound_kit_id) >= zone_music_repeat_delay_deadlines_ms_.size()) {
    return;
  }

  const std::uint32_t now_ms = openwow::core::GameClock::GetTickCount32();
  zone_music_repeat_delay_deadlines_ms_[static_cast<std::size_t>(sound_kit_id)] =
      now_ms + repeat_delay_minutes * 60000u;
}

void SoundRuntime::HandleStoppedSoundHandle(const SoundHandle &handle) {
  if (zone_music_player_.ActiveSlot().handle_binding.active_handle_id == handle.handle_id) {
    const auto stop_action = zone_music_stop_action_;
    switch (stop_action.kind) {
    case ZoneMusicStopActionKind::kSelectionDelayGate:
      ApplyZoneMusicSelectionDelayStopAction();
      break;
    case ZoneMusicStopActionKind::kResetRuntimeOnStop:
      ResetCurrentZoneMusicRuntime();
      break;
    case ZoneMusicStopActionKind::kSpecialRepeatDeadline:
      ClearZoneMusicStopAction();
      ApplyZoneMusicRepeatDelayDeadlineStopAction(stop_action.expected_sound_kit_id,
                                                  stop_action.repeat_delay_min_ms);
      break;
    case ZoneMusicStopActionKind::kNone:
    default:
      ClearZoneMusicStopAction();
      break;
    }
  }
}

void SoundRuntime::StopDualSlotSlot(DualSlotSoundPlayer::Slot &slot, const bool immediate,
                                      const float fade_sec, const bool release) {
  const std::uint32_t handle_id = slot.handle_binding.active_handle_id;
  if (handle_id != 0) {
    (void)StopActiveSoundHandle(handle_id, immediate, fade_sec, release);
    DetachHandleBinding(slot.handle_binding);
  }
  slot = {};
}

void SoundRuntime::ResetDualSlotPlayer(DualSlotSoundPlayer &player) {
  for (auto &slot : player.slots) {
    const std::uint32_t handle_id = slot.handle_binding.active_handle_id;
    if (handle_id != 0) {
      (void)StopActiveSoundHandle(handle_id, true, -1.0f, true);
    }
    DetachHandleBinding(slot.handle_binding);
  }
  player = {};
}

bool SoundRuntime::IsDualSlotActivePlaying(const DualSlotSoundPlayer &player) const {
  const auto &slot = player.ActiveSlot();
  if (!slot.follows_handle) {
    return slot.is_playing_override;
  }
  const std::uint32_t handle_id = slot.handle_binding.active_handle_id;
  return handle_id != 0 && IsSoundHandlePlaying(handle_id);
}

std::uint32_t
SoundRuntime::GetDualSlotActiveSoundKitId(const DualSlotSoundPlayer &player) const {
  if (!IsDualSlotActivePlaying(player)) {
    return 0;
  }
  return player.ActiveSlot().sound_kit_id;
}

void SoundRuntime::StartSoundHandleFadeIn(const std::uint32_t handle_id) {

  ReverseSoundHandleFade(handle_id);
}

void SoundRuntime::ReverseSoundHandleFade(const std::uint32_t handle_id) {
  if (handle_id == 0) {
    return;
  }
  const auto it = active_handles_.find(handle_id);
  if (it == active_handles_.end()) {
    return;
  }
  auto &handle = it->second;

  handle.stop_state = {};

  const float fade_seconds = handle.fade_in_seconds;
  if (fade_seconds <= 0.0f) {
    handle.fade_in_state = {};
    handle.relative_volume.SetStopScale(1.0f);
    return;
  }

  handle.fade_in_state.active = true;
  handle.fade_in_state.fade_total_ms =
      std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(fade_seconds * 1000.0f));
  handle.fade_in_state.fade_elapsed_ms = 0;
  handle.fade_in_state.start_scale = std::clamp(handle.relative_volume.stop_scale, 0.0f, 1.0f);
  handle.relative_volume.SetStopScale(handle.fade_in_state.start_scale);
}

void SoundRuntime::UpdateFadingInSoundHandle(SoundHandle &handle, const int delta_ms) {
  if (!handle.fade_in_state.active) {
    return;
  }

  if (handle.fade_in_state.fade_total_ms == 0) {
    handle.fade_in_state = {};
    handle.relative_volume.SetStopScale(1.0f);
    return;
  }

  if (delta_ms > 0) {
    const std::uint32_t advanced_elapsed =
        handle.fade_in_state.fade_elapsed_ms + static_cast<std::uint32_t>(delta_ms);
    handle.fade_in_state.fade_elapsed_ms =
        std::min(handle.fade_in_state.fade_total_ms, advanced_elapsed);
  }

  const float progress = static_cast<float>(handle.fade_in_state.fade_elapsed_ms) /
                         static_cast<float>(handle.fade_in_state.fade_total_ms);
  const float start_scale = handle.fade_in_state.start_scale;
  const float current_scale =
      std::clamp(start_scale + (1.0f - start_scale) * progress, 0.0f, 1.0f);
  handle.relative_volume.SetStopScale(current_scale);
  if (handle.fade_in_state.fade_elapsed_ms < handle.fade_in_state.fade_total_ms) {
    return;
  }

  handle.fade_in_state = {};
  handle.relative_volume.SetStopScale(1.0f);
}

int SoundRuntime::UpdateTrackedSlotPlayback(DualSlotSoundPlayer &player,
                                              const std::uint32_t sound_kit_id,
                                              const int zone_sound_type,
                                              const SoundKitPlaybackOptions &options,
                                              const bool immediate, const float fade_out_sec) {
  auto &active = player.ActiveSlot();
  auto &prev = player.PrevSlot();

  if (zone_sound_type >= 11) {
    StopDualSlotSlot(active, false, -1.0f, true);
    active.zone_sound_type = zone_sound_type;
    active.sound_kit_id = 0;
    return 18;
  }

  if (immediate) {
    StopDualSlotSlot(active, true, -1.0f, true);
    StopDualSlotSlot(prev, true, -1.0f, true);

    std::uint32_t handle_out = 0;
    const int result = PlaySoundKit(sound_kit_id, nullptr, &handle_out, options);
    if (result == 0) {
      active.sound_kit_id = sound_kit_id;
      active.zone_sound_type = zone_sound_type;
      active.follows_handle = true;
      active.is_playing_override = true;
      (void)AttachHandleBinding(active.handle_binding, handle_out);
    }
    return result;
  }

  if (sound_kit_id == active.sound_kit_id && active.handle_binding.active_handle_id != 0 &&
      IsSoundHandlePlaying(active.handle_binding.active_handle_id)) {
    return 15;
  }

  if (sound_kit_id == prev.sound_kit_id && prev.handle_binding.active_handle_id != 0 &&
      IsSoundHandlePlaying(prev.handle_binding.active_handle_id)) {

    if (active.handle_binding.active_handle_id != 0) {
      (void)RequestStopSoundHandle(active.handle_binding.active_handle_id, fade_out_sec);
    }

    const std::uint32_t prev_handle_id = prev.handle_binding.active_handle_id;
    if (prev_handle_id != 0) {
      const auto prev_it = active_handles_.find(prev_handle_id);
      if (prev_it != active_handles_.end() && options.fade_in_seconds >= 0.0f) {
        prev_it->second.fade_in_seconds = options.fade_in_seconds;
      }
    }
    ReverseSoundHandleFade(prev_handle_id);

    player.SwapSlots();
    return 21;
  }

  StopDualSlotSlot(prev, false, -1.0f, true);

  if (active.handle_binding.active_handle_id != 0) {
    (void)RequestStopSoundHandle(active.handle_binding.active_handle_id, fade_out_sec);
  }

  std::uint32_t handle_out = 0;
  const int result = PlaySoundKit(sound_kit_id, nullptr, &handle_out, options);
  if (result == 0) {
    prev.sound_kit_id = sound_kit_id;
    prev.zone_sound_type = zone_sound_type;
    prev.follows_handle = true;
    prev.is_playing_override = true;
    (void)AttachHandleBinding(prev.handle_binding, handle_out);
    player.SwapSlots();
  }
  return result;
}

void SoundRuntime::ResetWorldAudioStateForGlue() {
  UnregisterEnterWorldAudioCallbacks();
  ResetZoneAmbienceRuntime();
  SetWeatherSoundKit(-1);
  ResetZoneAndScriptMusicRuntime();
  ClearLiquidAmbienceRuntime();
  sound_provider_preference_ids_.fill(-1);
  zone_music_delay_deadline_ms_ = 0;
  skip_priority_8_sound_provider_selection_ = false;
  enable_priority_9_sound_provider_selection_ = false;
  indoor_sound_area_id_ = 0;
  indoor_sound_area_changed_ = false;
  SetWorldReverbProperties(BuildGlueWorldReverbProperties());
  ClearAdvancedKitPropertyRuntime();
  zone_music_repeat_delay_state_initialized_ = false;
  zone_music_repeat_delay_deadlines_ms_.clear();
  zone_music_delay_rng_initialized_ = false;
  zone_music_delay_rng_ = {};
  chunk_audio_cached_chunk_x_ = -1;
  chunk_audio_cached_chunk_y_ = -1;
}

void SoundRuntime::StopAllActiveSounds() {
  sound_engine_->StopAllSounds();

  auto &engine = *audio_engine_;
  if (engine.IsInitialized()) {
    engine.StopMusic(0.0f);
    engine.StopAmbience(0.0f);
    engine.StopMovieAudio();
  }

  for (auto &[handle_id, binding] : active_handle_bindings_) {
    (void)handle_id;
    if (binding != nullptr) {
      binding->active_handle_id = 0;
    }
  }
  active_handle_bindings_.clear();

  active_handles_.clear();
  active_sound_type_counts_.fill(0);
  active_exclusive_sound_kits_.clear();
  script_music_handle_id_.reset();
  script_music_playing_ = false;
  error_speech_handle_id_ = 0;
  cinematic_sound_handle_ = 0;
  zone_music_delay_deadline_ms_ = 0;
  ResetCurrentZoneAmbienceRuntime();
  ResetCurrentZoneMusicRuntime();
}

void SoundRuntime::StopAllSoundEffects(const float fade_seconds) {
  auto &sound_engine = *sound_engine_;
  if (!sound_engine.IsInitialized()) {
    return;
  }

  if (sound_engine.FindOrCreateChannelGroup("SFX", false, false) == nullptr) {
    return;
  }

  const float clamped_fade_seconds = std::max(fade_seconds, 0.0f);
  sound_engine.StopChannelGroupSounds("SFX", clamped_fade_seconds);

  for (auto &[handle_id, handle] : active_handles_) {
    if (!handle.has_active_sound || !handle.is_playing) {
      continue;
    }
    if (handle.sound_type >= kSoundTypeLabels.size() || !SoundTypeUsesSfxCVar(handle.sound_type)) {
      continue;
    }
    (void)RequestStopSoundHandle(handle_id, clamped_fade_seconds);
  }
}

void SoundRuntime::EnsureAdvancedKitPropertyManager() {
  advanced_kit_property_manager_initialized_ = true;
}

void SoundRuntime::DestroyAdvancedKitPropertyManager() {
  if (!advanced_kit_property_manager_initialized_ && active_advanced_kit_properties_.empty()) {
    return;
  }

  ClearAdvancedKitPropertyRuntime();
  advanced_kit_property_manager_initialized_ = false;
}

void SoundRuntime::ClearAdvancedKitPropertyRuntime() {
  active_advanced_kit_properties_.clear();
}

void SoundRuntime::AddAdvancedKitProperty(const std::uint32_t handle_id,
                                            const SoundKitData &kit) {
  if (!kit.advanced.has_value()) {
    return;
  }

  const auto &advanced = *kit.advanced;
  if (!IsAdvancedDuckValueActive(advanced.duck_to_sfx) &&
      !IsAdvancedDuckValueActive(advanced.duck_to_music) &&
      !IsAdvancedDuckValueActive(advanced.duck_to_ambience)) {
    return;
  }

  EnsureAdvancedKitPropertyManager();
  const auto existing_it = std::find_if(
      active_advanced_kit_properties_.begin(), active_advanced_kit_properties_.end(),
      [handle_id](const ActiveAdvancedKitProperty &property) {
        return property.owner_handle_id.has_value() && *property.owner_handle_id == handle_id;
      });
  if (existing_it != active_advanced_kit_properties_.end()) {
    return;
  }

  ActiveAdvancedKitProperty property;
  property.owner_handle_id = handle_id;
  property.fade_out_time_ms = advanced.duck_out_time_ms;
  active_advanced_kit_properties_.push_back(property);
}

void SoundRuntime::RefreshBoundObjectSoundPositions() {
  float position[3]{};

  for (auto &[handle_id, handle] : active_handles_) {
    (void)handle_id;

    if (!handle.has_active_sound || !handle.is_playing || !handle.bound_object_guid.has_value()) {
      continue;
    }

    if (!GetObjectPosition(*handle.bound_object_guid, position)) {
      continue;
    }

    if (handle.has_position && handle.position[0] == position[0] &&
        handle.position[1] == position[1] && handle.position[2] == position[2]) {
      continue;
    }

    handle.has_position = true;
    handle.position[0] = position[0];
    handle.position[1] = position[1];
    handle.position[2] = position[2];
    if (handle.audio_engine_handle_id.has_value()) {
      audio_engine_->SetSound3DPosition(
          AudioPlaybackHandle{*handle.audio_engine_handle_id},
          position[0], position[1], position[2]);
    }
  }
}

void SoundRuntime::UpdateAdvancedKitProperties(const int delta_ms) {
  float listener_position[3]{};
  if (!GetActivePlayerPosition(listener_position)) {
    return;
  }

  for (const auto &[handle_id, handle] : active_handles_) {
    if (!handle.has_active_sound || !handle.is_playing) {
      continue;
    }

    const auto *kit = GetSoundKitData(handle.sound_kit_id);
    if (!kit || !kit->advanced.has_value()) {
      continue;
    }

    if (!handle.has_position || IsZeroVector3(handle.position)) {
      AddAdvancedKitProperty(handle_id, *kit);
      continue;
    }

    const float distance = ComputeDistance3(listener_position, handle.position);
    if (distance <= kit->advanced->inner_radius) {
      AddAdvancedKitProperty(handle_id, *kit);
    }
  }

  for (auto it = active_advanced_kit_properties_.begin();
       it != active_advanced_kit_properties_.end();) {
    auto handle_it = active_handles_.end();
    if (it->owner_handle_id.has_value()) {
      handle_it = active_handles_.find(*it->owner_handle_id);
    }
    const SoundHandle *handle = nullptr;
    const SoundKitData *kit = nullptr;
    const AdvancedSoundEntryData *advanced = nullptr;
    if (handle_it != active_handles_.end() && handle_it->second.has_active_sound &&
        handle_it->second.is_playing) {
      handle = &handle_it->second;
      kit = GetSoundKitData(handle->sound_kit_id);
      if (kit && kit->advanced.has_value()) {
        advanced = &*kit->advanced;
      }
    }

    bool inside_advanced_radius = false;
    if (handle && advanced) {
      if (!handle->has_position || IsZeroVector3(handle->position)) {
        it->distance_to_listener = 0.0f;
        inside_advanced_radius = true;
      } else {
        it->distance_to_listener = ComputeDistance3(listener_position, handle->position);
        inside_advanced_radius = it->distance_to_listener < advanced->inner_radius ||
                                 (it->distance_to_listener < advanced->outer_radius && it->active);
      }
    }

    if (inside_advanced_radius && advanced) {
      it->active = true;
      const std::array<float, 3> targets = {advanced->duck_to_sfx, advanced->duck_to_music,
                                            advanced->duck_to_ambience};
      for (std::size_t channel = 0; channel < targets.size(); ++channel) {
        const float target = targets[channel];
        if (it->current_attenuation[channel] == target) {
          continue;
        }

        if (advanced->duck_in_time_ms == 0) {
          it->current_attenuation[channel] = target;
          continue;
        }

        it->current_attenuation[channel] -=
            static_cast<float>(delta_ms) / static_cast<float>(advanced->duck_in_time_ms);
        if (it->current_attenuation[channel] < target) {
          it->current_attenuation[channel] = target;
        }
      }
    } else {
      it->active = false;
      it->owner_handle_id.reset();

      const std::uint32_t fade_out_time_ms =
          (advanced != nullptr) ? advanced->duck_out_time_ms : it->fade_out_time_ms;
      it->fade_out_time_ms = fade_out_time_ms;
      for (float &current_attenuation : it->current_attenuation) {
        if (current_attenuation == 1.0f) {
          continue;
        }

        if (fade_out_time_ms == 0) {
          current_attenuation = 1.0f;
          continue;
        }

        current_attenuation += static_cast<float>(delta_ms) / static_cast<float>(fade_out_time_ms);
        if (current_attenuation > 1.0f) {
          current_attenuation = 1.0f;
        }
      }
    }

    if (!it->owner_handle_id.has_value() &&
        std::all_of(it->current_attenuation.begin(), it->current_attenuation.end(),
                    [](const float attenuation) { return attenuation == 1.0f; })) {
      it = active_advanced_kit_properties_.erase(it);
      continue;
    }

    ++it;
  }
}

float SoundRuntime::ComputeAdvancedDuckingScaleForHandle(const std::uint32_t handle_id,
                                                           const std::uint32_t sound_type) const {
  if (!SoundTypeUsesAdvancedDucking(sound_type)) {
    return 1.0f;
  }

  const std::size_t channel = ResolveAdvancedDuckChannel(sound_type);
  float scale = 1.0f;
  for (const auto &property : active_advanced_kit_properties_) {
    if (property.owner_handle_id.has_value() && *property.owner_handle_id == handle_id) {
      continue;
    }
    scale = std::min(scale, property.current_attenuation[channel]);
  }
  return scale;
}

bool SoundRuntime::StartDirectSoundKitPlayback(const std::uint32_t handle_id,
                                                 const SoundKitData &kit, const float *position,
                                                 const SoundKitPlaybackOptions &options) {
  auto handle_it = active_handles_.find(handle_id);
  if (handle_it == active_handles_.end()) {
    return false;
  }

  SoundHandle &handle = handle_it->second;
  ReleaseEngineHandle(handle);
  CleanupHandlePlaybackState(handle);

  ResetSoundHandlePlaybackStateForRestart(handle);
  handle.handle_id = handle_id;
  handle.sound_kit_id = kit.id;

  const std::uint32_t sound_type = ResolvePlaybackSoundType(options);
  if (sound_type >= kSoundTypeLabels.size()) {
    return false;
  }
  if (active_sound_type_counts_[sound_type] >= kSoundTypeMaxActiveCounts[sound_type]) {
    return false;
  }

  const bool tracks_exclusive_kit = ResolveExclusiveRepeatTracking(kit, options);
  if (tracks_exclusive_kit &&
      active_exclusive_sound_kits_.find(kit.id) != active_exclusive_sound_kits_.end()) {
    return false;
  }

  const std::int32_t preload_queue_hint = ResolvePlaybackPreloadQueueHint(sound_type, options);
  const auto selected_file = SelectSoundKitFileForPlayback(
      kit, options.variation_mode, options.forced_file_index, preload_queue_hint);
  if (!selected_file.has_value()) {
    return false;
  }

  ++active_sound_type_counts_[sound_type];
  if (tracks_exclusive_kit) {
    active_exclusive_sound_kits_.insert(kit.id);
  }

  handle.sound_type = sound_type;
  handle.playback_priority = options.playback_priority;
  handle.max_audible_behavior = ResolveMaxAudibleBehavior(options);
  handle.max_audible_mute_fade_speed = options.max_audible_mute_fade_speed;
  handle.selected_file_index = selected_file->index;
  handle.sound_model_override = options.sound_model_override;
  handle.has_active_sound = true;
  handle.is_playing = true;
  handle.loops = ResolveLoopingPlayback(kit, options);
  handle.bypass_virtual_play_window = options.force_ambient_loop;
  handle.min_distance = kit.min_distance;
  handle.max_distance = options.max_distance_override > -1.0f
                            ? options.max_distance_override
                            : kit.max_distance;
  handle.tracks_instance_limit = true;
  handle.tracks_exclusive_kit = tracks_exclusive_kit;
  handle.source_label = ResolvePlaybackSourceLabel(kit);
  handle.selected_file_path = std::string(selected_file->path);
  ApplySoundKitFadeOptionsToHandle(handle, options);

  const SoundKitPlaybackVolume resolved_volume =
      ResolveSoundKitPlaybackVolume(kit, options);
  handle.relative_volume.SetDirectVolume(resolved_volume.direct_volume);

  handle.has_position = position != nullptr;
  if (position != nullptr) {
    handle.position[0] = position[0];
    handle.position[1] = position[1];
    handle.position[2] = position[2];
  } else {
    handle.position[0] = 0.0f;
    handle.position[1] = 0.0f;
    handle.position[2] = 0.0f;
  }

  handle.relative_volume.SetPlaybackScale(resolved_volume.playback_scale);

  if (!MaterializeSoundHandle(handle)) {
    CleanupHandlePlaybackState(handle);
    ResetSoundHandlePlaybackStateForRestart(handle);
    handle.handle_id = handle_id;
    handle.sound_kit_id = kit.id;
    return false;
  }

  return true;
}

int SoundRuntime::QueueAdvancedSoundKitPlayback(const SoundKitData &kit,
                                                  const SoundKitPlaybackOptions &options,
                                                  const float *position,
                                                  std::uint32_t *handle_out) {
  if (!kit.advanced.has_value()) {
    return 16;
  }

  const auto &advanced = *kit.advanced;
  if (position != nullptr && options.suppress_near_duplicate_advanced_instances) {
    for (const auto &[existing_handle_id, existing_handle] : active_handles_) {
      (void)existing_handle_id;
      if (!existing_handle.managed_advanced.has_value()) {
        continue;
      }
      const SoundKitData *const existing_kit =
          GetSoundKitData(existing_handle.managed_advanced->sound_kit_id);
      if (existing_kit == nullptr || !existing_kit->advanced.has_value() ||
          existing_kit->advanced->id != advanced.id) {
        continue;
      }
      if (!existing_handle.has_position) {
        continue;
      }
      if (ComputeDistance3(position, existing_handle.position) < std::sqrt(6.0f)) {
        return 15;
      }
    }
  }

  const std::uint32_t handle_id = AllocateSoundHandle();
  auto &handle = active_handles_[handle_id];
  handle.sound_kit_id = kit.id;
  handle.sound_type = ResolvePlaybackSoundType(options);
  handle.max_audible_behavior = ResolveMaxAudibleBehavior(options);
  handle.max_audible_mute_fade_speed = options.max_audible_mute_fade_speed;
  handle.bypass_virtual_play_window = options.force_ambient_loop;
  handle.has_active_sound = false;
  handle.is_playing = false;
  handle.source_label = ResolvePlaybackSourceLabel(kit);

  handle.min_distance = kit.min_distance;
  handle.max_distance = options.max_distance_override > -1.0f
                            ? options.max_distance_override
                            : kit.max_distance;
  handle.has_position = position != nullptr;
  if (position != nullptr) {
    handle.position[0] = position[0];
    handle.position[1] = position[1];
    handle.position[2] = position[2];
  }

  ManagedAdvancedSoundState state;
  state.sound_kit_id = kit.id;
  state.playback_mode = options.loop_mode == SoundLoopMode::kForceLoop ? 0u : advanced.usage;
  state.playback_options = options;
  state.playback_options.allow_advanced_kit_properties = false;
  state.playback_options.loop_mode =
      state.playback_mode == 0u ? SoundLoopMode::kForceLoop : SoundLoopMode::kForceOneShot;

  if (advanced.random_offset_ms != 0) {
    EnsurePlaybackRandomStateSeeded(random_seed_);
    const std::uint32_t span = advanced.random_offset_ms * 2u;
    const std::int32_t signed_offset =
        -static_cast<std::int32_t>(advanced.random_offset_ms) +
        static_cast<std::int32_t>(ConsumePlaybackRandomBoundedValue(span));
    state.schedule_day_offset_ms = advanced.time_of_day_window.day_offset_ms + signed_offset;
  } else {
    state.schedule_day_offset_ms = advanced.time_of_day_window.day_offset_ms;
  }

  if (advanced.time_interval_max_ms > advanced.time_interval_min_ms) {
    EnsurePlaybackRandomStateSeeded(random_seed_);
    state.retrigger_countdown_ms =
        static_cast<std::int32_t>(advanced.time_interval_min_ms) +
        static_cast<std::int32_t>(ConsumePlaybackRandomBoundedValue(advanced.time_interval_max_ms -
                                                                    advanced.time_interval_min_ms));
  } else {
    state.retrigger_countdown_ms = static_cast<std::int32_t>(advanced.time_interval_max_ms);
  }

  handle.managed_advanced = state;

  if (handle_out != nullptr) {
    *handle_out = handle_id;
  }

  if (state.playback_mode == 2u) {
    if (!StartDirectSoundKitPlayback(
            handle_id, kit, position, state.playback_options)) {
      active_handles_.erase(handle_id);
      if (handle_out != nullptr) {
        *handle_out = 0;
      }
      return 16;
    }
    AddAdvancedKitProperty(handle_id, kit);
  }

  return 0;
}

void SoundRuntime::UpdateManagedAdvancedSound(const std::uint32_t handle_id,
                                                SoundHandle &handle,
                                                const std::uint32_t current_day_time_ms,
                                                const int delta_ms,
                                                const float *listener_position) {
  ManagedAdvancedSoundState &state = *handle.managed_advanced;
  const SoundKitData *const kit = GetSoundKitData(state.sound_kit_id);
  if (kit == nullptr || !kit->advanced.has_value()) {
    return;
  }

  const bool within_audible_range =
      listener_position == nullptr || !handle.has_position ||
      IsZeroVector3(handle.position) || handle.max_distance <= 0.0f ||
      ComputeDistance3(listener_position, handle.position) < handle.max_distance;

  TimeOfDaySoundWindow window = kit->advanced->time_of_day_window;
  window.day_offset_ms = state.schedule_day_offset_ms;
  state.time_of_day_scale = window.ComputeEnvelopeScaleAt(current_day_time_ms);

  const bool window_active = window.IsActiveAt(current_day_time_ms);
  if (handle.is_playing) {
    if (state.playback_mode != 0u && !window_active) {
      ReleaseEngineHandle(handle);
      CleanupHandlePlaybackState(handle);
      ResetSoundHandlePlaybackStateForRestart(handle);
      handle.handle_id = handle_id;
      handle.sound_kit_id = state.sound_kit_id;
      handle.sound_type = ResolvePlaybackSoundType(state.playback_options);
      handle.source_label = ResolvePlaybackSourceLabel(*kit);
      return;
    }

    if (state.playback_mode == 1u && kit->file_count > 1 &&
        kit->advanced->time_interval_min_ms > 0 && kit->advanced->time_interval_max_ms > 0) {
      state.retrigger_countdown_ms -= delta_ms;
      if (state.retrigger_countdown_ms < 0) {
        (void)StartDirectSoundKitPlayback(handle_id, *kit,
                                          handle.has_position ? handle.position : nullptr,
                                          state.playback_options);
        if (kit->advanced->time_interval_max_ms > kit->advanced->time_interval_min_ms) {
          EnsurePlaybackRandomStateSeeded(random_seed_);
          state.retrigger_countdown_ms =
              static_cast<std::int32_t>(kit->advanced->time_interval_min_ms) +
              static_cast<std::int32_t>(ConsumePlaybackRandomBoundedValue(
                  kit->advanced->time_interval_max_ms - kit->advanced->time_interval_min_ms));
        } else {
          state.retrigger_countdown_ms =
              static_cast<std::int32_t>(kit->advanced->time_interval_max_ms);
        }
      }
    }
    return;
  }

  if (!window_active) {
    return;
  }

  if (state.playback_mode == 0u) {

    if (handle.virtual_play_state.active || !within_audible_range) {
      return;
    }
    (void)StartDirectSoundKitPlayback(
        handle_id, *kit, handle.has_position ? handle.position : nullptr, state.playback_options);
    return;
  }

  if (state.playback_mode == 1u) {
    state.retrigger_countdown_ms -= delta_ms;
    if (state.retrigger_countdown_ms < 0) {

      if (within_audible_range)
        (void)StartDirectSoundKitPlayback(
            handle_id, *kit, handle.has_position ? handle.position : nullptr,
            state.playback_options);
      if (kit->advanced->time_interval_max_ms > kit->advanced->time_interval_min_ms) {
        EnsurePlaybackRandomStateSeeded(random_seed_);
        state.retrigger_countdown_ms =
            static_cast<std::int32_t>(kit->advanced->time_interval_min_ms) +
            static_cast<std::int32_t>(ConsumePlaybackRandomBoundedValue(
                kit->advanced->time_interval_max_ms - kit->advanced->time_interval_min_ms));
      } else {
        state.retrigger_countdown_ms =
            static_cast<std::int32_t>(kit->advanced->time_interval_max_ms);
      }
    }
  }
}

std::uint32_t SoundRuntime::AllocateSoundHandle() {

  if (next_handle_ == 0) {
    next_handle_ = 1;
  }

  for (;;) {
    const std::uint32_t handle = next_handle_;
    next_handle_ = handle == std::numeric_limits<std::uint32_t>::max()
                       ? 1u
                       : handle + 1u;

    const auto [it, inserted] = active_handles_.try_emplace(handle);
    if (!inserted) {
      continue;
    }

    it->second.handle_id = handle;
    it->second.is_playing = true;
    return handle;
  }
}

void SoundRuntime::ClearHandleBinding(const std::uint32_t handle_id) {
  const auto binding_it = active_handle_bindings_.find(handle_id);
  if (binding_it == active_handle_bindings_.end()) {
    return;
  }

  SoundHandleBinding *const binding = binding_it->second;
  if (binding != nullptr && binding->active_handle_id == handle_id) {
    binding->active_handle_id = 0;
  }

  active_handle_bindings_.erase(binding_it);
}

void SoundRuntime::CleanupHandlePlaybackState(const SoundHandle &handle) {
  if (handle.tracks_instance_limit && handle.sound_type < active_sound_type_counts_.size() &&
      active_sound_type_counts_[handle.sound_type] != 0) {
    --active_sound_type_counts_[handle.sound_type];
  }

  if (handle.tracks_exclusive_kit && handle.sound_kit_id != 0) {
    active_exclusive_sound_kits_.erase(handle.sound_kit_id);
  }
}

void SoundRuntime::OnNaturalPlaybackComplete(SoundHandle &handle) {
  CleanupHandlePlaybackState(handle);
  ReleaseEngineHandle(handle);
  handle.tracks_instance_limit = false;
  handle.tracks_exclusive_kit = false;
  handle.has_active_sound = false;
  handle.is_playing = false;
  HandleStoppedSoundHandle(handle);
}

void SoundRuntime::PollNaturalPlaybackCompletions() {
  auto &engine = *audio_engine_;
  if (!engine.IsInitialized()) {
    return;
  }

  const auto qualifies = [](const SoundHandle &handle) {
    return handle.is_playing && handle.has_active_sound && !handle.stop_state.active &&
           handle.audio_engine_handle_id.has_value();
  };

  std::vector<AudioPlaybackHandle> query;
  std::vector<std::uint32_t> owners;
  query.reserve(active_handles_.size());
  owners.reserve(active_handles_.size());
  for (const auto &[handle_id, handle] : active_handles_) {
    if (!qualifies(handle)) {
      continue;
    }
    query.push_back(AudioPlaybackHandle{*handle.audio_engine_handle_id});
    owners.push_back(handle_id);
  }
  std::vector<bool> channel_active;
  engine.QueryHandleChannelsActive(query, channel_active);

  for (std::size_t index = 0; index < query.size(); ++index) {
    if (channel_active[index]) {
      continue;
    }
    const auto handle_it = active_handles_.find(owners[index]);
    if (handle_it == active_handles_.end()) {
      continue;
    }
    SoundHandle &handle = handle_it->second;
    if (!qualifies(handle) || *handle.audio_engine_handle_id != query[index].handleId) {
      continue;
    }
    OnNaturalPlaybackComplete(handle);
  }
}

void SoundRuntime::ReleaseExclusiveRepeatTracking(SoundHandle &handle) {
  if (!handle.tracks_exclusive_kit || handle.sound_kit_id == 0) {
    return;
  }

  active_exclusive_sound_kits_.erase(handle.sound_kit_id);
  handle.tracks_exclusive_kit = false;
}

void SoundRuntime::ReleaseEngineHandle(SoundHandle &handle) {
  if (!handle.audio_engine_handle_id.has_value()) {
    return;
  }
  const AudioPlaybackHandle engine_handle{*handle.audio_engine_handle_id};
  audio_engine_->Stop(engine_handle);
  audio_engine_->DestroyHandle(engine_handle);
  handle.audio_engine_handle_id.reset();
  handle.engine_pushed_channel_volume.reset();
}

void SoundRuntime::FinalizeStoppedSoundHandle(SoundHandle &handle) {
  ReleaseEngineHandle(handle);

  ReleaseExclusiveRepeatTracking(handle);
  handle.stop_state = {};
  handle.relative_volume.SetStopScale(0.0f);
  const bool was_playing = handle.is_playing;
  handle.is_playing = false;
  if (was_playing) {
    HandleStoppedSoundHandle(handle);
  }
}

bool SoundRuntime::UpdateStoppingSoundHandle(SoundHandle &handle, const int delta_ms) {
  if (!handle.stop_state.active) {
    return false;
  }

  if (handle.stop_state.fade_total_ms == 0) {
    FinalizeStoppedSoundHandle(handle);
    return true;
  }

  if (delta_ms > 0) {
    const std::uint32_t advanced_elapsed =
        handle.stop_state.fade_elapsed_ms + static_cast<std::uint32_t>(delta_ms);
    handle.stop_state.fade_elapsed_ms = std::min(handle.stop_state.fade_total_ms, advanced_elapsed);
  }

  const float progress = static_cast<float>(handle.stop_state.fade_elapsed_ms) /
                         static_cast<float>(handle.stop_state.fade_total_ms);
  const float remaining_scale =
      std::clamp(handle.stop_state.start_scale * (1.0f - progress), 0.0f, 1.0f);
  handle.relative_volume.SetStopScale(remaining_scale);
  if (handle.stop_state.fade_elapsed_ms < handle.stop_state.fade_total_ms) {
    return false;
  }

  FinalizeStoppedSoundHandle(handle);
  return true;
}

void SoundRuntime::FreeSoundHandle(std::uint32_t handle_id) {
  const bool was_script_music_handle =
      script_music_handle_id_.has_value() && *script_music_handle_id_ == handle_id;
  const auto handle_it = active_handles_.find(handle_id);
  if (handle_it == active_handles_.end()) {
    ClearHandleBinding(handle_id);
    if (was_script_music_handle) {
      script_music_handle_id_.reset();
      script_music_playing_ = false;
    }
    return;
  }

  if (handle_it->second.is_playing) {
    FinalizeStoppedSoundHandle(handle_it->second);
  }

  ReleaseEngineHandle(handle_it->second);

  ClearHandleBinding(handle_id);
  if (was_script_music_handle) {
    script_music_handle_id_.reset();
    script_music_playing_ = false;
  }
  CleanupHandlePlaybackState(handle_it->second);
  active_handles_.erase(handle_it);
}

std::optional<SoundHandle> SoundRuntime::GetSoundHandle(std::uint32_t handle_id) const {
  const auto it = active_handles_.find(handle_id);
  if (it == active_handles_.end()) {
    return std::nullopt;
  }
  return it->second;
}

float SoundRuntime::ConsumePlaybackRandomUnitFloat() {
  EnsurePlaybackRandomStateSeeded(random_seed_);
  return retail_rng::AdlerSeedNextUnitFloat(random_seed_);
}

SoundKitPlaybackVolume SoundRuntime::ResolveSoundKitPlaybackVolume(
    const SoundKitData& kit, const SoundKitPlaybackOptions& options) {
  SoundKitPlaybackVolume result;
  result.direct_volume = options.explicit_volume;
  if (!IsNormalizedVolume(result.direct_volume)) {
    result.direct_volume = kit.volume * options.volume_scale;
    if ((kit.flags & kSoundKitFlagRandomizeVolume) != 0) {
      EnsurePlaybackRandomStateSeeded(random_seed_);
      result.direct_volume +=
          retail_rng::AdlerSeedNextRangeFloat(-0.15f, 0.15f, random_seed_);
    }
  }

  if ((kit.flags & kSoundKitFlagRandomizeRate) != 0) {
    EnsurePlaybackRandomStateSeeded(random_seed_);
    result.playback_scale =
        retail_rng::AdlerSeedNextRangeFloat(0.85f, 1.15f, random_seed_);
  }
  return result;
}

std::uint32_t SoundRuntime::ConsumePlaybackRandomBoundedValue(const std::uint32_t bound) {
  EnsurePlaybackRandomStateSeeded(random_seed_);
  return retail_rng::AdlerSeedNextBoundedValue(bound, random_seed_);
}

bool SoundRuntime::IsSoundHandlePlaying(const std::uint32_t handle_id) const {
  const auto it = active_handles_.find(handle_id);
  if (it == active_handles_.end()) {
    return false;
  }

  return it->second.has_active_sound && it->second.is_playing;
}

bool SoundRuntime::IsSoundKitPlayingNearby(const std::uint32_t sound_kit_id,
                                             const float *position,
                                             const float max_distance_squared) const {
  if (position == nullptr) {
    return false;
  }

  for (const auto &[handle_id, handle] : active_handles_) {
    (void)handle_id;
    if (!handle.has_active_sound || !handle.is_playing || !handle.has_position ||
        handle.sound_kit_id != sound_kit_id) {
      continue;
    }

    const float dx = position[0] - handle.position[0];
    const float dy = position[1] - handle.position[1];
    const float dz = position[2] - handle.position[2];
    if (dx * dx + dy * dy + dz * dz < max_distance_squared) {
      return true;
    }
  }

  return false;
}

bool SoundRuntime::AttachHandleBinding(SoundHandleBinding &binding,
                                         const std::uint32_t handle_id) {
  if (active_handles_.find(handle_id) == active_handles_.end()) {
    return false;
  }

  if (binding.active_handle_id != 0 && binding.active_handle_id != handle_id) {
    ClearHandleBinding(binding.active_handle_id);
    binding.active_handle_id = 0;
  }

  const auto existing_it = active_handle_bindings_.find(handle_id);
  if (existing_it != active_handle_bindings_.end() && existing_it->second != nullptr &&
      existing_it->second != &binding && existing_it->second->active_handle_id == handle_id) {
    existing_it->second->active_handle_id = 0;
  }

  active_handle_bindings_[handle_id] = &binding;
  binding.active_handle_id = handle_id;
  return true;
}

std::uint32_t SoundRuntime::DetachHandleBinding(SoundHandleBinding &binding) {
  const std::uint32_t result = binding.active_handle_id;
  if (result == 0) {
    return 0;
  }

  const auto binding_it = active_handle_bindings_.find(result);
  if (binding_it != active_handle_bindings_.end() && binding_it->second == &binding) {
    active_handle_bindings_.erase(binding_it);
  }

  binding.active_handle_id = 0;
  return result;
}

bool SoundRuntime::IsHandleBound(const std::uint32_t handle_id) const {
  return active_handle_bindings_.find(handle_id) != active_handle_bindings_.end();
}

bool SoundRuntime::SetSoundHandleVolume(std::uint32_t handle_id, float volume) {
  const auto it = active_handles_.find(handle_id);
  if (it == active_handles_.end()) {
    return false;
  }
  it->second.relative_volume.SetDirectVolume(volume);
  PushSoundHandleVolumeToAudioEngine(it->second);

  return true;
}

bool SoundRuntime::SetSoundHandlePosition(const std::uint32_t handle_id, const float *position) {
  if (position == nullptr) {
    return false;
  }

  const auto it = active_handles_.find(handle_id);
  if (it == active_handles_.end()) {
    return false;
  }

  it->second.has_position = true;
  it->second.position[0] = position[0];
  it->second.position[1] = position[1];
  it->second.position[2] = position[2];
  if (it->second.audio_engine_handle_id.has_value()) {
    audio_engine_->SetSound3DPosition(
        AudioPlaybackHandle{*it->second.audio_engine_handle_id},
        position[0], position[1], position[2]);
  }
  return true;
}

bool SoundRuntime::MaterializeSoundHandle(SoundHandle &handle) {
  if (handle.selected_file_path.empty() || !audio_engine_->IsInitialized()) {
    return false;
  }

  AudioClipInfo clip;
  clip.clipId = handle.handle_id;
  clip.path = handle.selected_file_path;
  clip.channel = ResolveAudioPlaybackChannel(handle.sound_type);
  clip.volume = handle.relative_volume.channel_volume;
  clip.loop = handle.loops;
  clip.priority = handle.playback_priority.has_value()
                      ? static_cast<int>(*handle.playback_priority)
                      : -1;
  clip.soundModelOverride = handle.sound_model_override;

  const AudioPlaybackHandle engine_handle = audio_engine_->Play(clip);
  if (engine_handle.handleId == 0 ||
      !audio_engine_->IsHandleMaterialized(engine_handle)) {
    if (engine_handle.handleId != 0) {
      audio_engine_->DestroyHandle(engine_handle);
    }
    return false;
  }

  handle.audio_engine_handle_id = engine_handle.handleId;
  handle.engine_pushed_channel_volume.reset();
  if (handle.has_position) {
    audio_engine_->SetSound3DPosition(
        engine_handle, handle.position[0], handle.position[1], handle.position[2]);
    audio_engine_->SetHandleDistanceRange(
        engine_handle, handle.min_distance, handle.max_distance);
  }
  return true;
}

bool SoundRuntime::BindSoundHandleToObjectGuid(const std::uint32_t handle_id,
                                                 const std::uint64_t object_guid) {
  const auto it = active_handles_.find(handle_id);
  if (it == active_handles_.end()) {
    return false;
  }

  if (!it->second.has_position) {
    return false;
  }

  it->second.bound_object_guid = object_guid;
  return true;
}

bool SoundRuntime::ApplySoundHandleRelativeVolumeScale(std::uint32_t handle_id, float scale) {
  const auto it = active_handles_.find(handle_id);
  if (it == active_handles_.end()) {
    return false;
  }
  it->second.relative_volume.ApplyRelativeScale(scale);
  PushSoundHandleVolumeToAudioEngine(it->second);
  return true;
}

void SoundRuntime::PushSoundHandleVolumeToAudioEngine(SoundHandle &handle) {
  if (!handle.audio_engine_handle_id.has_value()) {
    return;
  }

  const float channel_volume = handle.relative_volume.channel_volume;
  if (handle.engine_pushed_channel_volume.has_value() &&
      channel_volume == *handle.engine_pushed_channel_volume) {
    return;
  }
  handle.engine_pushed_channel_volume = channel_volume;
  audio_engine_->SetHandleVolume(
      AudioPlaybackHandle{*handle.audio_engine_handle_id}, channel_volume);
}

bool SoundRuntime::RequestStopSoundHandle(const std::uint32_t handle_id,
                                            const float fade_seconds) {
  const auto it = active_handles_.find(handle_id);
  if (it == active_handles_.end()) {
    return false;
  }

  SoundHandle &handle = it->second;
  if (!handle.is_playing) {
    FinalizeStoppedSoundHandle(handle);
    return true;
  }

  if (fade_seconds >= 0.0f) {
    handle.fade_out_seconds = fade_seconds;
  }

  handle.fade_in_state = {};

  const float effective_fade_seconds = handle.fade_out_seconds;
  if (effective_fade_seconds > 0.0f) {
    handle.stop_state.active = true;
    handle.stop_state.fade_total_ms =
        std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(effective_fade_seconds * 1000.0f));
    handle.stop_state.fade_elapsed_ms = 0;
    handle.stop_state.start_scale = std::clamp(handle.relative_volume.stop_scale, 0.0f, 1.0f);
    handle.relative_volume.SetStopScale(handle.stop_state.start_scale);
    return true;
  }

  FinalizeStoppedSoundHandle(handle);
  return true;
}

bool SoundRuntime::StopActiveSoundHandle(const std::uint32_t handle_id, const bool immediate,
                                           const float fade_seconds, const bool release_handle) {
  const auto it = active_handles_.find(handle_id);
  if (it == active_handles_.end()) {
    if (release_handle) {
      FreeSoundHandle(handle_id);
      return true;
    }
    return false;
  }

  if (!immediate && fade_seconds > 0.0f && it->second.is_playing) {
    return RequestStopSoundHandle(handle_id, fade_seconds);
  }

  FinalizeStoppedSoundHandle(it->second);
  if (release_handle) {
    FreeSoundHandle(handle_id);
  }
  return true;
}

bool SoundRuntime::IsCinematicSoundPlaying() const {
  return IsSoundHandlePlaying(cinematic_sound_handle_);
}

}
