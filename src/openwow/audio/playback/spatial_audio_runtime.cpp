#include "openwow/audio/playback/sound_runtime_internal.h"

namespace openwow::audio {

namespace {

constexpr std::uint32_t kVirtualPlayWindowLookupFailureMs = 400u;

}

std::uint32_t SoundRuntime::ResolveCurrentDayTimeMs() const {
  if (!normalized_time_of_day_cb_) {
    return update_time_ms_ > 0 ? static_cast<std::uint32_t>(update_time_ms_) : 0u;
  }

  const double day_time_ms =
      normalized_time_of_day_cb_() * static_cast<double>(TimeOfDaySoundWindow::kMillisecondsPerDay);
  if (day_time_ms <= 0.0) {
    return 0;
  }
  if (day_time_ms >= static_cast<double>(TimeOfDaySoundWindow::kMillisecondsPerDay)) {
    return TimeOfDaySoundWindow::kMillisecondsPerDay;
  }
  return static_cast<std::uint32_t>(day_time_ms);
}

std::size_t SoundRuntime::ResolveTimeOfDayIndex() const {

  constexpr std::uint32_t kDayStartMs = 19800000u;
  constexpr std::uint32_t kDayEndMs = 75600000u;
  const std::uint32_t day_time_ms = ResolveCurrentDayTimeMs();
  return day_time_ms >= kDayStartMs && day_time_ms < kDayEndMs ? 0u : 1u;
}

int SoundRuntime::UpdateLoop() {
  const int current_day_time_ms = static_cast<int>(ResolveCurrentDayTimeMs());
  update_time_ms_ = current_day_time_ms;

  int delta = current_day_time_ms - last_update_time_ms_;
  if (delta < 0) {
    delta = 0;
  }
  last_update_time_ms_ = current_day_time_ms;

  RefreshBoundObjectSoundPositions();
  UpdateAdvancedKitProperties(delta);
  PollNaturalPlaybackCompletions();

  float listener_position[3]{};
  const bool have_listener_position = GetActivePlayerPosition(listener_position);

  for (auto &[handle_id, handle] : active_handles_) {
    if (!handle.managed_advanced.has_value()) {
      continue;
    }
    UpdateManagedAdvancedSound(handle_id, handle, static_cast<std::uint32_t>(current_day_time_ms),
                               delta, have_listener_position ? listener_position : nullptr);
  }

  std::vector<std::uint32_t> handles_to_free;
  handles_to_free.reserve(active_handles_.size());
  for (auto &[handle_id, handle] : active_handles_) {

    const bool managed_advanced = handle.managed_advanced.has_value();
    const ManagedAdvancedSoundState *managed_state =
        managed_advanced ? &*handle.managed_advanced : nullptr;

    if (have_listener_position && !handle.stop_state.active) {
      UpdateVirtualPlayWindow(handle, listener_position, delta);
    }

    if (!managed_advanced && !handle.is_playing && !handle.virtual_play_state.active) {
      handles_to_free.push_back(handle_id);
      continue;
    }

    float advanced_scale = ComputeAdvancedDuckingScaleForHandle(handle_id, handle.sound_type);
    if (managed_state != nullptr) {
      advanced_scale *= managed_state->time_of_day_scale;
    }
    handle.relative_volume.SetAdvancedScale(advanced_scale);

    PushSoundHandleVolumeToAudioEngine(handle);

    if (UpdateStoppingSoundHandle(handle, delta)) {
      if (!managed_advanced) {
        handles_to_free.push_back(handle_id);
      }
    } else {
      UpdateFadingInSoundHandle(handle, delta);
    }

    if (managed_advanced && !handle.is_playing && handle.managed_advanced.has_value() &&
        handle.managed_advanced->playback_mode == 2u) {
      handles_to_free.push_back(handle_id);
    }
  }

  for (const auto handle_id : handles_to_free) {
    FreeSoundHandle(handle_id);
  }

  if (liquid_ambience_.handle_id.has_value() &&
      active_handles_.find(*liquid_ambience_.handle_id) == active_handles_.end()) {
    liquid_ambience_.handle_id.reset();
    if (liquid_ambience_.stop_pending) {
      liquid_ambience_.stop_pending = false;
    }
  }

  return 1;
}

std::uint32_t SoundRuntime::ResolveVirtualPlayWindowMs() const {
  if (cvar_get_int_cb_ == nullptr) {
    return kVirtualPlayWindowLookupFailureMs;
  }

  const int configured_window_ms = cvar_get_int_cb_("Sound_VirtualPlayWindow");
  if (configured_window_ms <= 0) {
    return kVirtualPlayWindowLookupFailureMs;
  }

  return static_cast<std::uint32_t>(configured_window_ms);
}

bool SoundRuntime::UsesVirtualPlayWindow(const SoundHandle &handle) const {
  return !handle.bypass_virtual_play_window && handle.loops && handle.has_position &&
         !IsZeroVector3(handle.position) &&
         handle.max_distance > 0.0f;
}

bool SoundRuntime::IsHandleAudibleAtListener(const SoundHandle &handle,
                                               const float *listener_position) const {
  if (!handle.has_position || listener_position == nullptr || IsZeroVector3(handle.position)) {
    return true;
  }

  if (handle.max_distance <= 0.0f) {
    return false;
  }

  const float distance = ComputeDistance3(listener_position, handle.position);
  if (distance <= handle.min_distance) {
    return handle.relative_volume.channel_volume > 0.0f;
  }
  if (distance >= handle.max_distance) {
    return false;
  }

  if (handle.max_distance <= handle.min_distance) {
    return handle.relative_volume.channel_volume > 0.0f;
  }

  const float attenuation =
      1.0f - ((distance - handle.min_distance) /
              (handle.max_distance - handle.min_distance));
  return handle.relative_volume.channel_volume * std::clamp(attenuation, 0.0f, 1.0f) > 0.0f;
}

void SoundRuntime::RestartVirtualizedHandle(SoundHandle &handle) {
  if (!MaterializeSoundHandle(handle)) {
    return;
  }
  handle.has_active_sound = true;
  handle.is_playing = true;
  handle.virtual_play_state = {};
}

void SoundRuntime::UpdateVirtualPlayWindow(SoundHandle &handle,
                                             const float *listener_position,
                                             const int delta_ms) {
  if (!UsesVirtualPlayWindow(handle)) {
    handle.virtual_play_state = {};
    return;
  }

  if (IsHandleAudibleAtListener(handle, listener_position)) {
    if (handle.virtual_play_state.active && !handle.is_playing && !handle.stop_state.active) {
      RestartVirtualizedHandle(handle);
    } else {
      handle.virtual_play_state = {};
    }
    return;
  }

  const std::uint32_t virtual_play_window_ms = ResolveVirtualPlayWindowMs();

  if (delta_ms > 0) {
    const auto advanced_elapsed =
        handle.virtual_play_state.inactive_elapsed_ms + static_cast<std::uint32_t>(delta_ms);
    handle.virtual_play_state.inactive_elapsed_ms =
        std::min(virtual_play_window_ms, advanced_elapsed);
  }

  if (handle.virtual_play_state.inactive_elapsed_ms < virtual_play_window_ms) {
    return;
  }

  ReleaseEngineHandle(handle);
  handle.has_active_sound = false;
  handle.is_playing = false;
  handle.virtual_play_state.active = true;
}

int SoundRuntime::UpdateLiquidQueryPosition(const double delta_seconds) {
  if (delta_seconds > 0.0) {
    liquid_query_elapsed_ms_ += delta_seconds * 1000.0;
  }
  if (liquid_query_elapsed_ms_ < 500.0) {
    return 0;
  }
  liquid_query_elapsed_ms_ = 0.0;

  if (cvar_get_bool_cb_) {
    if (!cvar_get_bool_cb_("Sound_EnableAllSound")) {
      return 0;
    }
    if (!cvar_get_bool_cb_("Sound_EnableAmbience")) {
      return 0;
    }
  }

  float current_position[3]{};
  if (!GetActivePlayerPosition(current_position)) {
    return 0;
  }

  if (!liquid_query_position_initialized_) {
    liquid_query_position_[0] = 0.0f;
    liquid_query_position_[1] = 0.0f;
    liquid_query_position_[2] = 0.0f;
    liquid_query_position_initialized_ = true;
  }

  if (openwow::math::vec3::AllComponentsEqual(current_position,
                                               liquid_query_position_)) {
    return 0;
  }

  std::memcpy(liquid_query_position_, current_position, sizeof(liquid_query_position_));
  return 1;
}

int SoundRuntime::UpdateLiquidAmbience(const double delta_seconds) {
  if (liquid_ambience_.stop_pending && liquid_ambience_.handle_id.has_value() &&
      active_handles_.find(*liquid_ambience_.handle_id) == active_handles_.end()) {
    liquid_ambience_.handle_id.reset();
  }
  if (liquid_ambience_.stop_pending && !liquid_ambience_.handle_id.has_value()) {
    liquid_ambience_.stop_pending = false;
  }

  if (enable_priority_9_sound_provider_selection_) {
    RequestLiquidAmbienceStop();
    return 1;
  }

  if (!liquid_query_cb_) {
    return 1;
  }

  float player_position[3]{};
  if (!GetActivePlayerPosition(player_position)) {
    return 1;
  }

  if (!UpdateLiquidQueryPosition(delta_seconds)) {
    return 1;
  }

  LiquidQueryWorldSnapshot snapshot;
  if (!liquid_query_cb_(60.0f, snapshot)) {
    return 1;
  }

  liquid_query_result_buffer_.Reset();
  for (const auto &entry : snapshot.entries) {
    const LiquidTypeSoundData *const liquid_type = GetLiquidTypeSoundData(entry.liquid_type_id);
    if (liquid_type == nullptr || liquid_type->sound_kit_id == 0) {
      continue;
    }
    if ((liquid_type->flags & 0x80u) != 0 && snapshot.suppress_flagged_types) {
      continue;
    }

    const float world_x = player_position[0] + entry.relative_offset[0];
    const float world_y = player_position[1] + entry.relative_offset[1];
    const float world_z = player_position[2] + entry.relative_offset[2];
    const float distance_squared = entry.relative_offset[0] * entry.relative_offset[0] +
                                   entry.relative_offset[1] * entry.relative_offset[1];

    (void)liquid_query_result_buffer_.InsertSorted(
        distance_squared, world_x, world_y, world_z, liquid_type->sound_kit_id);
  }

  const LiquidQueryResultEntry &nearest = liquid_query_result_buffer_[0];
  bool cached_selection_active = false;
  bool retarget_existing_handle = false;

  if (!nearest.IsEmpty() && liquid_ambience_.sound_kit_id == nearest.sound_kit_id) {
    const bool exact_position_match = liquid_ambience_.position[0] == nearest.position[0] &&
                                      liquid_ambience_.position[1] == nearest.position[1] &&
                                      liquid_ambience_.position[2] == nearest.position[2];
    if (exact_position_match) {
      cached_selection_active = true;
    } else {
      const float dx = liquid_ambience_.position[0] - nearest.position[0];
      const float dy = liquid_ambience_.position[1] - nearest.position[1];
      const float dz = liquid_ambience_.position[2] - nearest.position[2];
      if (dx * dx + dy * dy + dz * dz < 400.0f) {
        liquid_ambience_.position = nearest.position;
        cached_selection_active = true;
        retarget_existing_handle = true;
      }
    }
  }

  if (!cached_selection_active) {
    RequestLiquidAmbienceStop();
    liquid_ambience_.sound_kit_id = nearest.IsEmpty() ? 0 : nearest.sound_kit_id;
    if (!nearest.IsEmpty()) {
      liquid_ambience_.position = nearest.position;
    }
  }

  if (liquid_ambience_.handle_id.has_value()) {
    const std::uint32_t active_handle_id = *liquid_ambience_.handle_id;
    const auto active_handle_it = active_handles_.find(active_handle_id);
    if (active_handle_it != active_handles_.end() && active_handle_it->second.is_playing) {
      if (retarget_existing_handle) {
        (void)SetSoundHandlePosition(active_handle_id, liquid_ambience_.position.data());
      }
      return 1;
    }
  }

  if (liquid_ambience_.stop_pending || liquid_ambience_.sound_kit_id == 0) {
    return 1;
  }

  SoundKitPlaybackOptions options;
  options.sound_type = 2;
  options.loop_mode = SoundLoopMode::kForceLoop;

  std::uint32_t handle_id = 0;
  if (PlaySoundKit(liquid_ambience_.sound_kit_id, liquid_ambience_.position.data(), &handle_id,
                   options) == 0) {
    liquid_ambience_.stop_pending = false;
    liquid_ambience_.handle_id = handle_id;
  }

  return 1;
}

void SoundRuntime::StopLiquidAmbience() {
  ClearLiquidAmbienceRuntime();
}

int SoundRuntime::HandleBackground(
    openwow::game::WorldSession& session,
    const std::uint32_t* foreground) {
  if (!foreground) {
    return 1;
  }

  const bool enable_bg_sound =
      cvar_get_bool_cb_ ? cvar_get_bool_cb_("Sound_EnableSoundWhenGameIsInBG") : false;
  const bool enable_voice_chat = cvar_get_bool_cb_ ? cvar_get_bool_cb_("EnableVoiceChat") : false;
  const bool voice_chat_active = openwow::game::VoiceChat::Get().IsEnabledAndActive();

  auto set_focus_mute_state = [this](const bool muted) {
    sound_engine_->SetMasterMuted(muted);
    sound_engine_->SetChannelGroupMuted("CINEMATIC", muted);

    if (channel_mute_cb_) {
      channel_mute_cb_("<master>", muted);
      channel_mute_cb_("CINEMATIC", muted);
    }
  };

  auto clear_active_voice_selection = [&session]() {
    (void)openwow::game::VoiceChat_ApplyActiveSessionSelection(
        session, std::nullopt);
  };

  auto restore_active_voice_selection = [this, &session]() {
    if (!suspended_background_display_channel_.has_value()) {
      return;
    }

    const auto restored = *suspended_background_display_channel_;
    switch (restored.session_type_code) {
    case 0:
      (void)openwow::game::VoiceChat_SelectActiveSessionByChannel(
          session, openwow::game::VoiceChatChannelType::kCustom,
          restored.session_name);
      break;
    case 1:
      (void)openwow::game::VoiceChat_SelectActiveSessionByChannel(
          session, openwow::game::VoiceChatChannelType::kBattleground,
          std::string_view{});
      break;
    case 2:
      (void)openwow::game::VoiceChat_SelectActiveSessionByChannel(
          session, openwow::game::VoiceChatChannelType::kParty,
          std::string_view{});
      break;
    case 3:
      (void)openwow::game::VoiceChat_SelectActiveSessionByChannel(
          session, openwow::game::VoiceChatChannelType::kRaid,
          std::string_view{});
      break;
    default:
      break;
    }
    suspended_background_display_channel_.reset();
  };

  if (*foreground) {
    set_focus_mute_state(false);

    if (voice_chat_active && enable_voice_chat &&
        suspended_background_display_channel_.has_value()) {
      restore_active_voice_selection();
    }
    return 1;
  }

  if (enable_bg_sound) {
    set_focus_mute_state(false);
    return 1;
  }

  set_focus_mute_state(true);

  if (voice_chat_active &&
      !suspended_background_display_channel_.has_value()) {
    const auto active_slot = openwow::game::VoiceChat::Get().GetActiveVoiceDisplaySlot();
    const auto &chat_system = openwow::game::ChatSystem::Get();
    if (active_slot.has_value()) {
      if (const auto resolved = chat_system.ResolveDisplayChannel(*active_slot);
          resolved.has_value()) {
        openwow::audio::SoundRuntime::SuspendedVoiceSessionSelection suspended{};
        switch (resolved->kind) {
        case openwow::game::DisplayChannelKind::kJoinedChannel:
          suspended.session_type_code = 0;
          if (resolved->channel.has_value()) {
            suspended.session_name = resolved->channel->name;
          }
          suspended_background_display_channel_ = std::move(suspended);
          break;
        case openwow::game::DisplayChannelKind::kSpecialSlot1:
          suspended.session_type_code = 1;
          suspended_background_display_channel_ = std::move(suspended);
          break;
        case openwow::game::DisplayChannelKind::kSpecialSlot2:
          suspended.session_type_code = 2;
          suspended_background_display_channel_ = std::move(suspended);
          break;
        case openwow::game::DisplayChannelKind::kSpecialSlot3:
          suspended.session_type_code = 3;
          suspended_background_display_channel_ = std::move(suspended);
          break;
        case openwow::game::DisplayChannelKind::kInvalid:
          break;
        }
      }
    }

    clear_active_voice_selection();
  }

  return 1;
}

bool SoundRuntime::GetObjectPosition(const std::uint64_t guid, float *pos_out) const {
  return object_position_cb_ && object_position_cb_(guid, pos_out);
}

const openwow::game::CGUnit_C *SoundRuntime::GetUnitForSound(
    const std::uint64_t guid) const {
  return unit_lookup_cb_ ? unit_lookup_cb_(guid) : nullptr;
}

const openwow::game::CGPlayer_C *SoundRuntime::GetPlayerForSound(
    const std::uint64_t guid) const {
  return player_lookup_cb_ ? player_lookup_cb_(guid) : nullptr;
}

const openwow::game::CGPlayer_C *SoundRuntime::GetActivePlayerForSound() const {
  return active_player_cb_ ? active_player_cb_() : nullptr;
}

}
