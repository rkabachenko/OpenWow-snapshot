#include "openwow/audio/playback/sound_runtime_internal.h"

namespace openwow::audio {

void SoundRuntime::SetCvarCallbacks(CvarSetCallback set_cb, CvarGetBoolCallback get_bool_cb,
                                      ChannelMuteCallback mute_cb) {
  cvar_set_cb_ = std::move(set_cb);
  cvar_get_bool_cb_ = std::move(get_bool_cb);
  channel_mute_cb_ = std::move(mute_cb);
}

void SoundRuntime::SetCvarGetIntCallback(CvarGetIntCallback get_int_cb) {
  cvar_get_int_cb_ = std::move(get_int_cb);
}

void SoundRuntime::SetActivePlayerPositionCallback(ActivePlayerPositionCallback callback) {
  active_player_position_cb_ = std::move(callback);
}

void SoundRuntime::SetObjectPositionCallback(ObjectPositionCallback callback) {
  object_position_cb_ = std::move(callback);
}

void SoundRuntime::SetUnitLookupCallback(UnitLookupCallback callback) {
  unit_lookup_cb_ = std::move(callback);
}

void SoundRuntime::SetPlayerLookupCallback(PlayerLookupCallback callback) {
  player_lookup_cb_ = std::move(callback);
}

void SoundRuntime::SetActivePlayerCallback(ActivePlayerCallback callback) {
  active_player_cb_ = std::move(callback);
}

void SoundRuntime::SetNormalizedTimeOfDayCallback(NormalizedTimeOfDayCallback callback) {
  normalized_time_of_day_cb_ = std::move(callback);
}

void SoundRuntime::SetLiquidQueryCallback(LiquidQueryCallback callback) {
  liquid_query_cb_ = std::move(callback);
}

void SoundRuntime::ApplyChannelGroupMute(const char *group_name, const bool muted) {
  sound_engine_->SetChannelGroupMuted(group_name, muted);

  auto &audio = *audio_engine_;
  if (openwow::text::EqualsIgnoreCaseAscii(group_name, "SFX")) {
    audio.SetPlaybackChannelMuted(PlaybackChannel::SFX, muted);
  } else if (openwow::text::EqualsIgnoreCaseAscii(group_name, "MUSIC") ||
             openwow::text::EqualsIgnoreCaseAscii(group_name, "SCRIPTMUSIC")) {
    audio.SetPlaybackChannelMuted(PlaybackChannel::Music, muted);
  } else if (openwow::text::EqualsIgnoreCaseAscii(group_name, "AMBIENCE")) {
    audio.SetPlaybackChannelMuted(PlaybackChannel::Ambience, muted);
  }
  if (channel_mute_cb_) {
    channel_mute_cb_(group_name, muted);
  }
}

bool SoundRuntime::OnSoundEnableAllSound(const bool value) {
  if (value) {
    const bool sfx_enabled = cvar_get_bool_cb_ ? cvar_get_bool_cb_("Sound_EnableSFX") : true;
    const bool music_enabled = cvar_get_bool_cb_ ? cvar_get_bool_cb_("Sound_EnableMusic") : true;
    const bool ambience_enabled =
        cvar_get_bool_cb_ ? cvar_get_bool_cb_("Sound_EnableAmbience") : true;

    ApplyChannelGroupMute("SFX", !sfx_enabled);
    ApplyChannelGroupMute("MUSIC", !music_enabled);
    ApplyChannelGroupMute("AMBIENCE", !ambience_enabled);
  } else {
    ApplyChannelGroupMute("SFX", true);
    ApplyChannelGroupMute("MUSIC", true);
    ApplyChannelGroupMute("AMBIENCE", true);
  }
  return true;
}

bool SoundRuntime::OnSoundEnableSFX(const bool value) {
  const bool all_sound_enabled =
      cvar_get_bool_cb_ ? cvar_get_bool_cb_("Sound_EnableAllSound") : true;
  const bool should_mute = !value || !all_sound_enabled;
  ApplyChannelGroupMute("SFX", should_mute);
  return true;
}

bool SoundRuntime::OnSoundEnableAmbience(const bool value) {
  const bool all_sound_enabled =
      cvar_get_bool_cb_ ? cvar_get_bool_cb_("Sound_EnableAllSound") : true;
  const bool should_mute = !value || !all_sound_enabled || IsMovieAudioPlaying();
  ApplyChannelGroupMute("AMBIENCE", should_mute);
  if (should_mute) {
    ResetZoneAmbienceRuntime();
  }
  return true;
}

bool SoundRuntime::OnSoundEnableMusic(const bool value) {
  const bool all_sound_enabled =
      cvar_get_bool_cb_ ? cvar_get_bool_cb_("Sound_EnableAllSound") : true;
  const bool should_mute = !value || !all_sound_enabled || IsMovieAudioPlaying();
  ApplyChannelGroupMute("MUSIC", should_mute);
  if (should_mute) {
    ResetZoneAndScriptMusicRuntime();
  }
  return true;
}

void SoundRuntime::LinkAdvancedSoundEntriesToKits() {
  for (auto &sound_kit : sound_kit_storage_) {
    sound_kit.advanced.reset();
    if (sound_kit.advanced_id == 0) {
      continue;
    }

    const auto advanced_it = advanced_sound_entries_.find(sound_kit.advanced_id);
    if (advanced_it == advanced_sound_entries_.end()) {
      continue;
    }

    sound_kit.advanced = advanced_it->second;
  }
}

void SoundRuntime::Shutdown(bool is_restart) {
  if (!is_restart) {
    sound_validation_callbacks_->Reset();
  }

  UnregisterSoundEngineUpdateCallback();
  UnregisterEnterWorldAudioCallbacks();

  if (!is_restart) {
    ClearAdvancedKitPropertyRuntime();
  }

  sound_engine_->StopAllSounds();
  auto& audio_engine = *audio_engine_;
  const int pump_limit = is_restart ? 2 : 20;
  for (int attempt = 0;
       attempt < pump_limit &&
       audio_engine.GetActiveNonVoicePlaybackCount() != 0;
       ++attempt) {
    sound_engine_->ProcessUpdateTick();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  if (audio_engine.GetActiveNonVoicePlaybackCount() != 0) {
    audio_engine.StopNonVoiceSounds();
  }

  if (!is_restart) {
    FreeWorldStateZoneSounds();
    chunk_audio_bindings_.Reset();
    unit_sound_kit_lookup_.Reset();
    emotes_text_sound_ = EmotesTextSoundTable{};
  }

  ClearDspFilterChains();

  ClearSoundKitProviderCaches();

  if (!is_restart) {
    sound_kit_storage_.clear();
    sound_kit_index_.clear();
    sound_kit_playback_runtime_.clear();
    sound_kit_name_hash_index_.clear();
    max_sound_kit_id_ = 0;
    advanced_sound_entries_.clear();
  }

  sound_engine_->ShutdownGameSound(is_restart);

  if (!is_restart) {

    for (auto& [handle_id, handle] : active_handles_) {
      ReleaseEngineHandle(handle);
    }

    for (auto &[handle_id, binding] : active_handle_bindings_) {
      if (binding != nullptr) {
        binding->active_handle_id = 0;
      }
    }
    active_handle_bindings_.clear();
    active_handles_.clear();
    active_sound_type_counts_.fill(0);
    active_exclusive_sound_kits_.clear();
    chaos_runtime_.Reset();
    last_chaos_mode_frame_stats_ = {};
  }

  sound_engine_initialized_ = false;
}

std::size_t SoundRuntime::GetActiveHandleCount() const {
  return active_handles_.size();
}

void SoundRuntime::Reset() {
  ResetWorldAudioStateForGlue();

  sound_kit_index_.clear();
  sound_kit_storage_.clear();
  sound_kit_playback_runtime_.clear();
  sound_kit_name_hash_index_.clear();
  max_sound_kit_id_ = 0;
  sound_ambience_index_.clear();
  advanced_sound_entries_.clear();
  FreeWorldStateZoneSounds();
  chunk_audio_bindings_.Reset();
  sound_provider_preferences_.clear();
  zone_ambience_selection_entries_ = {};
  zone_music_selection_entries_ = {};
  zone_music_delay_deadline_ms_ = 0;
  ResetCurrentZoneAmbienceRuntime();
  ResetCurrentZoneMusicRuntime();
  emotes_text_sound_ = EmotesTextSoundTable{};
  dsp_filter_definitions_.clear();
  ClearDspFilterChains();
  zone_music_.clear();
  zone_intro_music_.clear();
  wound_death_sound_table_ = WoundDeathSoundTable{};
  weapon_impact_sounds_.Reset();
  liquid_type_sound_data_.clear();
  unit_sound_kit_lookup_.Reset();
  DestroyAdvancedKitPropertyManager();
  non_positional_playback_block_depth_ = 0;
  next_handle_ = 1;
  for (auto &[handle_id, binding] : active_handle_bindings_) {
    if (binding != nullptr && binding->active_handle_id == handle_id) {
      binding->active_handle_id = 0;
    }
  }
  active_handle_bindings_.clear();

  for (auto& [handle_id, handle] : active_handles_) {
    ReleaseEngineHandle(handle);
  }
  active_handles_.clear();
  active_sound_type_counts_.fill(0);
  active_exclusive_sound_kits_.clear();
  script_music_handle_id_.reset();
  script_music_playing_ = false;
  error_speech_handle_id_ = 0;
  background_sound_state_ = {};
  suspended_background_display_channel_.reset();
  cinematic_sound_handle_ = 0;
  listener_at_character_ = false;
  sound_engine_initialized_ = false;
  update_time_ms_ = 0;
  last_update_time_ms_ = 0;
  voice_chat_toggle_lookup_flags_ = 0;
  voice_chat_on_kit_ = 0;
  voice_chat_off_kit_ = 0;
  time_of_day_index_ = 0;
  random_seed_ = {};
  chaos_runtime_.Reset();
  last_chaos_mode_frame_stats_ = {};
  active_player_position_cb_ = nullptr;
  object_position_cb_ = nullptr;
  unit_lookup_cb_ = nullptr;
  player_lookup_cb_ = nullptr;
  active_player_cb_ = nullptr;
  normalized_time_of_day_cb_ = nullptr;
  liquid_query_cb_ = nullptr;
  cvar_get_int_cb_ = nullptr;
  liquid_query_elapsed_ms_ = 0.0;
  liquid_query_position_[0] = 0.0f;
  liquid_query_position_[1] = 0.0f;
  liquid_query_position_[2] = 0.0f;
  liquid_query_position_initialized_ = false;
  liquid_query_result_buffer_.Reset();
  liquid_ambience_ = {};
  world_reverb_enabled_ = false;
  audio_engine_->SetWorldReverbEnabled(false);
}

int SoundRuntime::InitializeFull(bool restart) {
  if (openwow::core::StormCmd::Instance().IsCommandEnabled(
          openwow::core::StartupCommandId::kNoSound)) {
    SoundInterface_RegisterCVars(*this);
    return 17;
  }

  auto &cvars = openwow::ui::game::CVarSystem::Instance();
  auto &engine = *sound_engine_;

  if (!restart) {
    RegisterSoundInterfaceCVarDefaults(cvars);
    RegisterSoundPlaybackCVarDefaults(cvars);
  }

  const int num_channels = std::max(cvars.GetCVarInt("Sound_NumChannels"), 4);
  int output_driver_index = cvars.GetCVarInt("Sound_OutputDriverIndex");
  const int output_quality = cvars.GetCVarInt("Sound_OutputQuality");

  int error_code = 0;
  const std::weak_ptr<void> position_lifetime = callback_lifetime_;
  const bool engine_initialized = engine.Init(num_channels,
              [this, position_lifetime](const std::uint32_t guid_lo,
                                        const std::uint32_t guid_hi, float* position) {
                if (position_lifetime.expired()) return false;
                const std::uint64_t guid =
                    (static_cast<std::uint64_t>(guid_hi) << 32) | guid_lo;
                return GetObjectPosition(guid, position);
              },
              output_driver_index,
              output_quality,
              &error_code,
              &output_driver_index);
  if (!engine_initialized) {
    sound_engine_initialized_ = false;
    return error_code != 0 ? error_code : 33;
  }

  {
    auto& audio_engine = *audio_engine_;
    audio_engine.SetVfsLoader(
        [](const std::string& path) -> std::optional<std::vector<std::uint8_t>> {
          void* data = nullptr;
          std::size_t size = 0;
          if (openwow::vfs::SFileReadFileToBuffer_Wrapper(path.c_str(), &data, &size, 0, 0) ==
              0) {
            if (data != nullptr && size > 0) {
              auto* bytes = static_cast<std::uint8_t*>(data);
              std::vector<std::uint8_t> result(bytes, bytes + size);
              openwow::vfs::SFileFreeLoadedData(data);
              return result;
            }
            openwow::vfs::SFileFreeLoadedData(data);
          }
          return std::nullopt;
        });
  }

  if (cvars.Exists("Sound_OutputDriverIndex")) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", output_driver_index);
    cvars.SetCVar("Sound_OutputDriverIndex", buf);
  }

  const float master_volume = cvars.GetCVarFloat("Sound_MasterVolume");
  engine.SetMasterVolume(master_volume);

  BindRegisteredCvars(cvars);

  RegisterChaosModeIfEnabled();

  const std::weak_ptr<void> device_lifetime = callback_lifetime_;
  engine.SetDeviceChangedCallback([this, device_lifetime] {
    if (device_lifetime.expired()) return;
    RefreshEnumeratedDevicesAndReconcile(true);
  });

  if (!dsp_filter_definitions_.empty()) {
    RebuildDspFilterChains();
  }

  sound_engine_initialized_ = true;
  RegisterSoundEngineUpdateCallback();
  return 0;
}

SoundDeviceRefreshResult
SoundRuntime::ReconcileEnumeratedDeviceState(const SoundDeviceEnumerationState &state,
                                               const bool fire_script_event) {
  auto &cvars = openwow::ui::game::CVarSystem::Instance();
  auto &engine = *sound_engine_;
  SoundDeviceRefreshResult result;

  const int output_index = ParseDriverIndex(cvars.GetCVar("Sound_OutputDriverIndex"));
  if (output_index >= static_cast<int>(state.output_devices.size())) {
    ResetDeviceSelectionToDefault(cvars, "Sound_OutputDriverIndex", "Sound_OutputDriverName",
                                  state.output_devices);
    result.output_driver_reset = true;
  }

  const int voice_output_index =
      ParseDriverIndex(cvars.GetCVar("Sound_VoiceChatOutputDriverIndex"));
  const bool voice_output_missing =
      !ActiveDeviceIsEnumerated(state.voice_output_devices, state.current_voice_output_device_name);
  const bool voice_output_system_default_changed =
      SystemDefaultSelectionChanged(voice_output_index, state.current_voice_output_device_name,
                                    state.enumerated_default_voice_output_device_name);
  if (voice_output_index >= static_cast<int>(state.voice_output_devices.size()) ||
      voice_output_missing || voice_output_system_default_changed) {
    ResetDeviceSelectionToDefault(cvars, "Sound_VoiceChatOutputDriverIndex",
                                  "Sound_VoiceChatOutputDriverName", state.voice_output_devices);
    engine.SetCurrentVoiceOutputDeviceName(state.enumerated_default_voice_output_device_name);
    result.voice_output_driver_reset = true;
  }

  const int voice_input_index = ParseDriverIndex(cvars.GetCVar("Sound_VoiceChatInputDriverIndex"));
  const bool voice_input_missing =
      !ActiveDeviceIsEnumerated(state.input_devices, state.current_input_device_name);
  const bool voice_input_system_default_changed =
      SystemDefaultSelectionChanged(voice_input_index, state.current_input_device_name,
                                    state.enumerated_default_input_device_name);
  if (voice_input_index >= static_cast<int>(state.input_devices.size()) || voice_input_missing ||
      voice_input_system_default_changed) {
    ResetDeviceSelectionToDefault(cvars, "Sound_VoiceChatInputDriverIndex",
                                  "Sound_VoiceChatInputDriverName", state.input_devices);
    engine.SetCurrentInputDeviceName(state.enumerated_default_input_device_name);
    result.voice_input_driver_reset = true;
  }

  const bool output_missing =
      !state.current_output_device_name.empty() &&
      !ContainsExactDeviceName(state.output_devices, state.current_output_device_name);
  const bool system_default_changed =
      output_index == 0 && !state.current_output_device_name.empty() &&
      !state.enumerated_default_output_device_name.empty() &&
      state.current_output_device_name != state.enumerated_default_output_device_name;
  const bool recover_pending_reopen =
      state.output_reopen_pending && !state.enumerated_default_output_device_name.empty();

  if (output_missing || system_default_changed || recover_pending_reopen) {
    ResetDeviceSelectionToDefault(cvars, "Sound_OutputDriverIndex", "Sound_OutputDriverName",
                                  state.output_devices);
    result.output_driver_reset = true;
    if (recover_pending_reopen || audio_engine_->IsInitialized()) {
      result.output_backend_reopened =
          audio_engine_->ReopenOutputDevicePreservingMovieAudio({});
    }
  }

  if (fire_script_event) {
    openwow::ui::game::ScriptEventDispatch::Get().FireEvent(
        openwow::ui::game::events::SOUND_DEVICE_UPDATE);
  }

  return result;
}

SoundDeviceRefreshResult
SoundRuntime::RefreshEnumeratedDevicesAndReconcile(const bool fire_script_event) {
  auto &engine = *sound_engine_;
  engine.EnumerateDevices();

  SoundDeviceEnumerationState state;
  state.output_devices.reserve(static_cast<std::size_t>(engine.GetOutputDeviceCount()));
  for (int index = 0; index < engine.GetOutputDeviceCount(); ++index) {
    state.output_devices.emplace_back(engine.GetOutputDeviceName(index));
  }

  state.voice_output_devices.reserve(static_cast<std::size_t>(engine.GetVoiceOutputDeviceCount()));
  for (int index = 0; index < engine.GetVoiceOutputDeviceCount(); ++index) {
    state.voice_output_devices.emplace_back(engine.GetVoiceOutputDeviceName(index));
  }

  state.input_devices.reserve(static_cast<std::size_t>(engine.GetInputDeviceCount()));
  for (int index = 0; index < engine.GetInputDeviceCount(); ++index) {
    state.input_devices.emplace_back(engine.GetInputDeviceName(index));
  }

  const bool seed_current_output = engine.GetCurrentOutputDeviceName().empty();
  state.current_output_device_name = engine.GetCurrentOutputDeviceName();
  state.enumerated_default_output_device_name = engine.GetEnumeratedDefaultOutputDeviceName();
  state.current_voice_output_device_name = engine.GetCurrentVoiceOutputDeviceName();
  state.enumerated_default_voice_output_device_name =
      engine.GetEnumeratedDefaultVoiceOutputDeviceName();
  state.current_input_device_name = engine.GetCurrentInputDeviceName();
  state.enumerated_default_input_device_name = engine.GetEnumeratedDefaultInputDeviceName();
  state.output_reopen_pending = engine.IsOutputDeviceReopenPending();

  SoundDeviceRefreshResult result = ReconcileEnumeratedDeviceState(state, fire_script_event);
  if (result.output_backend_reopened || !state.enumerated_default_output_device_name.empty()) {
    engine.SetOutputDeviceReopenPending(false);
  } else if (result.output_driver_reset && !state.current_output_device_name.empty()) {
    engine.SetOutputDeviceReopenPending(true);
  }
  if (seed_current_output || result.output_backend_reopened || result.output_driver_reset) {
    engine.CommitEnumeratedDefaultOutputDeviceName();
  }
  return result;
}

void SoundRuntime::BindRegisteredCvars(openwow::ui::game::CVarSystem &cvars) {
  using openwow::audio::OnSoundAmbienceVolumeChanged;
  using openwow::audio::OnSoundMasterVolumeChanged;
  using openwow::audio::OnSoundMusicVolumeChanged;
  using openwow::audio::OnSoundOutputDriverIndexChanged;
  using openwow::audio::OnSoundSfxVolumeChanged;
  using openwow::audio::OnVoiceChatInputDriverIndexChanged;
  using openwow::audio::OnVoiceChatOutputDriverIndexChanged;

  auto &engine = *sound_engine_;
  const std::weak_ptr<void> lifetime = callback_lifetime_;
  cvar_set_cb_ = [&cvars](const std::string &name, const std::string &value) {
    cvars.SetCVar(name, value, true);
  };
  cvar_get_bool_cb_ = [&cvars](const std::string &name) {
    return openwow::core::ParseSignedDecimalLikeSub76F0D0(cvars.GetCVar(name)) != 0u;
  };
  cvar_get_int_cb_ = [&cvars](const std::string &name) {
    return static_cast<int>(openwow::core::ParseSignedDecimalLikeSub76F0D0(cvars.GetCVar(name)));
  };

  const auto parse_sound_toggle = [](const std::string &value) {
    return openwow::core::ParseSignedDecimalLikeSub76F0D0(value) != 0u;
  };

  if (sound_enable_reverb_cvar_callback_handle_ != 0) {
    cvars.RemoveCallback("Sound_EnableReverb", sound_enable_reverb_cvar_callback_handle_);
  }

  sound_enable_reverb_cvar_callback_handle_ = cvars.AddCallback(
      "Sound_EnableReverb", [this, lifetime](const std::string &, const std::string &new_value) {
        if (lifetime.expired()) return;
        ApplySoundEnableReverbCVarValue(new_value);
      });
  ApplySoundEnableReverbCVarValue(cvars.GetCVar("Sound_EnableReverb"));

  if (sound_enable_software_hrtf_cvar_callback_handle_ != 0) {
    cvars.RemoveCallback("Sound_EnableSoftwareHRTF",
                         sound_enable_software_hrtf_cvar_callback_handle_);
  }
  sound_enable_software_hrtf_cvar_callback_handle_ = cvars.AddCallback(
      "Sound_EnableSoftwareHRTF",
      [this, lifetime, parse_sound_toggle](const std::string &,
                                           const std::string &new_value) {
        if (lifetime.expired()) return;
        audio_engine_->SetSoftwareHrtfEnabled(parse_sound_toggle(new_value));
      });
  audio_engine_->SetSoftwareHrtfEnabled(
      parse_sound_toggle(cvars.GetCVar("Sound_EnableSoftwareHRTF")));

  const auto sync_voice_input_driver_name = [this, &cvars, &engine](const std::string &new_value) {
    const int driver_index = ParseDriverIndex(new_value);
    const std::string driver_name =
        GetEnumeratedGameInputDriverName(driver_index);
    const std::string active_driver_name =
        ResolveActiveEnumeratedDeviceName(driver_index, driver_name,
                                          engine.GetEnumeratedDefaultInputDeviceName());
    const bool handled = OnVoiceChatInputDriverIndexChanged(
        new_value,
        [](const std::string &value) {
          return static_cast<std::uint32_t>(ParseDriverIndex(value));
        },
        [this](const std::uint32_t index, char *name_out, const std::uint32_t max_len, const bool) {
          return CopyDriverNameToBuffer(
              GetEnumeratedGameInputDriverName(static_cast<int>(index)),
              name_out, max_len);
        },
        [&cvars](const std::string &name, const std::string &value) {
          cvars.SetCVar(name, value, true);
        });
    if (driver_index == 0 || !active_driver_name.empty()) {
      engine.SetCurrentInputDeviceName(active_driver_name);
    }
    return handled;
  };

  if (sound_voice_input_driver_index_cvar_callback_handle_ != 0) {
    cvars.RemoveCallback("Sound_VoiceChatInputDriverIndex",
                         sound_voice_input_driver_index_cvar_callback_handle_);
  }
  sound_voice_input_driver_index_cvar_callback_handle_ = cvars.AddCallback(
      "Sound_VoiceChatInputDriverIndex",
      [sync_voice_input_driver_name, lifetime](const std::string &, const std::string &new_value) {
        if (lifetime.expired()) return;
        sync_voice_input_driver_name(new_value);
      });
  sync_voice_input_driver_name(cvars.GetCVar("Sound_VoiceChatInputDriverIndex"));

  const auto sync_voice_output_driver_name = [this, &cvars, &engine](const std::string &new_value) {
    const int driver_index = ParseDriverIndex(new_value);
    const std::string driver_name =
        GetEnumeratedOutputDriverName(driver_index, true);
    const std::string active_driver_name =
        ResolveActiveEnumeratedDeviceName(driver_index, driver_name,
                                          engine.GetEnumeratedDefaultVoiceOutputDeviceName());
    const bool handled = OnVoiceChatOutputDriverIndexChanged(
        new_value,
        [](const std::string &value) {
          return static_cast<std::uint32_t>(ParseDriverIndex(value));
        },
        [this](const std::uint32_t index, char *name_out, const std::uint32_t max_len, const bool) {
          return CopyDriverNameToBuffer(GetEnumeratedOutputDriverName(
                                            static_cast<int>(index), true),
                                        name_out, max_len);
        },
        [&cvars](const std::string &name, const std::string &value) {
          cvars.SetCVar(name, value, true);
        });
    if (driver_index == 0 || !active_driver_name.empty()) {
      engine.SetCurrentVoiceOutputDeviceName(active_driver_name);
    }
    return handled;
  };

  if (sound_voice_output_driver_index_cvar_callback_handle_ != 0) {
    cvars.RemoveCallback("Sound_VoiceChatOutputDriverIndex",
                         sound_voice_output_driver_index_cvar_callback_handle_);
  }
  sound_voice_output_driver_index_cvar_callback_handle_ = cvars.AddCallback(
      "Sound_VoiceChatOutputDriverIndex",
      [sync_voice_output_driver_name, lifetime](const std::string &, const std::string &new_value) {
        if (lifetime.expired()) return;
        sync_voice_output_driver_name(new_value);
      });
  sync_voice_output_driver_name(cvars.GetCVar("Sound_VoiceChatOutputDriverIndex"));

  const auto sync_output_driver_name = [this, &cvars](const std::string &new_value) {
    return OnSoundOutputDriverIndexChanged(
        new_value,
        [](const std::string &value) {
          return static_cast<std::uint32_t>(ParseDriverIndex(value));
        },
        [this](const std::uint32_t index, char *name_out, const std::uint32_t max_len, const bool) {
          return CopyDriverNameToBuffer(GetEnumeratedOutputDriverName(
                                            static_cast<int>(index), false),
                                        name_out, max_len);
        },
        [&cvars](const std::string &name, const std::string &value) {
          cvars.SetCVar(name, value, true);
        });
  };

  if (sound_output_driver_index_cvar_callback_handle_ != 0) {
    cvars.RemoveCallback("Sound_OutputDriverIndex",
                         sound_output_driver_index_cvar_callback_handle_);
  }
  sound_output_driver_index_cvar_callback_handle_ = cvars.AddCallback(
      "Sound_OutputDriverIndex",
      [sync_output_driver_name, lifetime](const std::string &, const std::string &new_value) {
        if (lifetime.expired()) return;
        sync_output_driver_name(new_value);
      });
  sync_output_driver_name(cvars.GetCVar("Sound_OutputDriverIndex"));

  sound_validation_callbacks_->handles[0] = BindValidationCallbackAndApplyCurrentValue(
      cvars, "Sound_EnableAllSound",
      [this, lifetime, parse_sound_toggle](const std::string &, const std::string &, const std::string &new_value) {
        if (lifetime.expired()) return false;
        return OnSoundEnableAllSound(parse_sound_toggle(new_value));
      });

  sound_validation_callbacks_->handles[1] = BindValidationCallbackAndApplyCurrentValue(
      cvars, "Sound_EnableSFX",
      [this, lifetime, parse_sound_toggle](const std::string &, const std::string &, const std::string &new_value) {
        if (lifetime.expired()) return false;
        return OnSoundEnableSFX(parse_sound_toggle(new_value));
      });

  sound_validation_callbacks_->handles[2] = BindValidationCallbackAndApplyCurrentValue(
      cvars, "Sound_EnableAmbience",
      [this, lifetime, parse_sound_toggle](const std::string &, const std::string &, const std::string &new_value) {
        if (lifetime.expired()) return false;
        return OnSoundEnableAmbience(parse_sound_toggle(new_value));
      });

  sound_validation_callbacks_->handles[3] = BindValidationCallbackAndApplyCurrentValue(
      cvars, "Sound_EnableMusic",
      [this, lifetime, parse_sound_toggle](const std::string &, const std::string &, const std::string &new_value) {
        if (lifetime.expired()) return false;
        return OnSoundEnableMusic(parse_sound_toggle(new_value));
      });

  sound_validation_callbacks_->handles[4] = BindValidationCallbackAndApplyCurrentValue(
      cvars, "Sound_MasterVolume",
      [this, lifetime](const std::string &, const std::string &, const std::string &new_value) {
        if (lifetime.expired()) return false;
        return OnSoundMasterVolumeChanged(
            new_value, [](const std::string &value) { return ParseSoundVolumeCVar(value); },
            [this](const float volume) { sound_engine_->SetMasterVolume(volume); });
      });

  sound_validation_callbacks_->handles[5] = BindValidationCallbackAndApplyCurrentValue(
      cvars, "Sound_SFXVolume",
      [this, lifetime](const std::string &, const std::string &, const std::string &new_value) {
        if (lifetime.expired()) return false;
        return OnSoundSfxVolumeChanged(
            new_value, [](const std::string &value) { return ParseSoundVolumeCVar(value); },
            [this](const std::string &category, const float volume) {
              ApplySoundChannelGroupVolume(*sound_engine_, *audio_engine_, category.c_str(), volume);
            });
      });

  sound_validation_callbacks_->handles[6] = BindValidationCallbackAndApplyCurrentValue(
      cvars, "Sound_MusicVolume",
      [this, lifetime](const std::string &, const std::string &, const std::string &new_value) {
        if (lifetime.expired()) return false;
        return OnSoundMusicVolumeChanged(
            new_value, [](const std::string &value) { return ParseSoundVolumeCVar(value); },
            [this](const std::string &category, const float volume) {
              ApplySoundChannelGroupVolume(*sound_engine_, *audio_engine_, category.c_str(), volume);
            });
      });

  sound_validation_callbacks_->handles[7] = BindValidationCallbackAndApplyCurrentValue(
      cvars, "Sound_AmbienceVolume",
      [this, lifetime](const std::string &, const std::string &, const std::string &new_value) {
        if (lifetime.expired()) return false;
        return OnSoundAmbienceVolumeChanged(
            new_value, [](const std::string &value) { return ParseSoundVolumeCVar(value); },
            [this](const std::string &category, const float volume) {
              ApplySoundChannelGroupVolume(*sound_engine_, *audio_engine_, category.c_str(), volume);
            });
      });

  if (outbound_chat_volume_cvar_callback_handle_ != 0) {
    cvars.RemoveCallback("OutboundChatVolume",
                         outbound_chat_volume_cvar_callback_handle_);
  }
  outbound_chat_volume_cvar_callback_handle_ = cvars.AddCallback(
      "OutboundChatVolume", [](const std::string &, const std::string &new_value) {
        VoiceChat_SetOutboundChatVolume(ParseSoundVolumeCVar(new_value));
      });
  VoiceChat_SetOutboundChatVolume(
      ParseSoundVolumeCVar(cvars.GetCVar("OutboundChatVolume")));
}

void SoundRuntime::ApplySoundEnableReverbCVarValue(const std::string_view value) {
  world_reverb_enabled_ = openwow::core::ParseSignedDecimalLikeSub76F0D0(value) != 0u;
  if (world_reverb_enabled_) {
    RefreshWorldReverbFromActiveSoundProvider();
    return;
  }

  SetWorldReverbProperties(BuildGlueWorldReverbProperties());
}

int SoundRuntime::GetEnumeratedOutputDriverCount(const bool use_voice_output_devices) {
  auto &engine = *sound_engine_;

  return use_voice_output_devices ? engine.GetVoiceOutputDeviceCount()
                                  : engine.GetOutputDeviceCount();
}

std::string SoundRuntime::GetEnumeratedOutputDriverName(const int driver_index,
                                                          const bool use_voice_output_devices) {
  auto &engine = *sound_engine_;
  const int device_count =
      use_voice_output_devices ? engine.GetVoiceOutputDeviceCount() : engine.GetOutputDeviceCount();

  if (driver_index < 0 || driver_index >= device_count) {
    return {};
  }
  return use_voice_output_devices ? engine.GetVoiceOutputDeviceName(driver_index)
                                  : engine.GetOutputDeviceName(driver_index);
}

int SoundRuntime::GetEnumeratedGameInputDriverCount() {
  auto &engine = *sound_engine_;

  return engine.GetInputDeviceCount();
}

std::string SoundRuntime::GetEnumeratedGameInputDriverName(const int driver_index) {
  auto &engine = *sound_engine_;

  if (driver_index < 0 || driver_index >= engine.GetInputDeviceCount()) {
    return {};
  }
  return engine.GetInputDeviceName(driver_index);
}

int SoundRuntime::GetEnumeratedRecordInputDriverCount() {
  auto &engine = *sound_engine_;

  return engine.GetRecordDeviceCount();
}

std::string SoundRuntime::GetEnumeratedRecordInputDriverName(const int driver_index) {
  auto &engine = *sound_engine_;

  if (driver_index < 0 || driver_index >= engine.GetRecordDeviceCount()) {
    return {};
  }
  return engine.GetRecordDeviceName(driver_index);
}

void SoundRuntime::RestartGameSoundSystemFromLua() {

  Shutdown(true);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  if (InitializeFull(true) == 0) {

    RegisterEnterWorldAudioCallbacks();
  }
}

void SoundRuntime::ShutdownSoundEngine() {
  UnregisterSoundEngineUpdateCallback();
  ClearSoundHashTables();
  openwow::core::FreeEvtContextTlsSlot();
  DestroyAdvancedKitPropertyManager();
  sound_engine_initialized_ = false;
}

void SoundRuntime::ClearSoundHashTables() {
  sound_engine_->PurgeSoundCache(true);
}

}
