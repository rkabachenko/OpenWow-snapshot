#pragma once
#include "openwow/audio/playback/sound_callbacks.h"
#include "openwow/audio/playback/sound_device_types.h"
#include "openwow/audio/playback/sound_playback_types.h"
#include "openwow/audio/playback/sound_spatial_types.h"
#include "openwow/audio/playback/sound_runtime_state.h"
namespace openwow::ui::game { class CVarSystem; }
namespace openwow::game {
class CGPlayer_C;
class CGUnit_C;
class WorldSession;
}
namespace openwow::audio {
enum class PlaybackChannel : std::uint8_t;
class SoundRuntime;
class AudioEngine;
class IMovieAudioSource;
class SoundEngine;
class VoiceChatLoopback;
struct WorldAudioCallbackRegistrationState;
class SoundRuntime : private SoundRuntimeState {
public:
  SoundRuntime();
  ~SoundRuntime();
  SoundRuntime(const SoundRuntime &) = delete;
  SoundRuntime &operator=(const SoundRuntime &) = delete;
  [[nodiscard]] SoundEngine& sound_engine() noexcept { return *sound_engine_; }
  [[nodiscard]] VoiceChatLoopback& voice_loopback() noexcept { return *voice_loopback_; }
  using VfsLoader = std::function<std::optional<std::vector<std::uint8_t>>(const std::string&)>;
  bool PrepareOutputDevice(int sample_rate, int channels, int buffer_size);
  [[nodiscard]] bool IsOutputDeviceReady() const;
  void SetVfsLoader(VfsLoader loader);
  void ShutdownOutputDevice();
  bool PlayMusicFile(const std::string& path, bool loop, float fade_in_ms,
                     float volume = 1.0f);
  void StopMusicFile(float fade_out_ms);
  [[nodiscard]] bool IsMusicFilePlaying() const;
  bool PlayAmbienceFile(const std::string& path, float volume, float fade_in_ms);
  void StopAmbienceFile(float fade_out_ms);
  [[nodiscard]] bool IsAmbienceFilePlaying() const;
  bool PlayCreditsFile(const std::string& path, bool loop, float volume = 1.0f);
  void StopCreditsFile(float fade_out_ms);
  [[nodiscard]] bool IsCreditsFilePlaying() const;
  void PlayMovieAudio(std::shared_ptr<IMovieAudioSource> source, float volume);
  void StopMovieAudio();
  [[nodiscard]] bool IsMovieAudioPlaying() const;
  [[nodiscard]] double MovieAudioTimeSeconds() const;
  [[nodiscard]] int GetOutputSampleRate() const;
  [[nodiscard]] int GetOutputChannelCount() const;
  [[nodiscard]] float GetMasterVolume() const;
  [[nodiscard]] float GetPlaybackChannelVolume(PlaybackChannel channel) const;
  void SetMasterVolume(float volume);
  void SetPlaybackChannelVolume(PlaybackChannel channel, float volume);
  void SetListener3DAttributes(const float* position, const float* velocity,
                               const float* forward, const float* up);
  void LoadSoundEntries(const std::vector<SoundKitData> &entries);
  void LoadSoundEntriesAdvanced(const std::vector<AdvancedSoundEntryData> &entries);
  void LoadWorldStateZoneSounds(const std::vector<WorldStateZoneSoundEntryData> &entries);
  void FreeWorldStateZoneSounds();
  void LoadSoundAmbienceTable(const std::vector<SoundAmbienceTableEntryData> &entries);
  void LoadChunkAudioBindings(const std::vector<ChunkAudioBindingEntry> &entries);
  void LoadSoundProviderPreferences(const std::vector<SoundProviderPreferenceData> &entries);
  void LoadEmotesTextSound( const std::vector<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t>> &entries, std::uint32_t max_emote_text_id);
  void
  LoadSoundFilters(const std::vector<std::pair<std::string, std::vector<DspFilterNode>>> &filters);
  void LoadZoneMusicTable(const std::vector<ZoneMusicTableEntryData> &entries, std::uint32_t max_zone_id);
  void LoadZoneIntroMusicTable(const std::vector<ZoneIntroMusicTableEntryData> &entries);
  void LoadWoundDeathSoundTable( const std::vector<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>> &rows);
  void LoadWeaponImpactSounds(const std::vector<WeaponImpactSoundRowData> &rows, std::uint32_t item_subclass_count);
  void LoadLiquidTypeSoundData(const std::vector<LiquidTypeSoundData> &entries);
  void RebuildUnitSoundKitLookup(const std::vector<UnitSoundLookupRow> &rows, std::uint32_t max_sound_slot_index);
  void ConfigureUnitSoundDisplayInfoRange(std::uint32_t min_display_info_id, std::uint32_t max_display_info_id);
  [[nodiscard]] bool SetUnitSoundDisplayInfo(std::uint32_t display_info_id, std::uint32_t sound_slot_index);
  [[nodiscard]] bool ClearUnitSoundDisplayInfo(std::uint32_t display_info_id);
  [[nodiscard]] const SoundKitData *GetSoundKitData(std::uint32_t id) const;
  void ApplyExclusiveRepeatModeToSoundKit(std::uint32_t id, SoundKitExclusivityMode mode);
  [[nodiscard]] bool SoundKitHasLoopFlag(std::uint32_t sound_kit_id) const;
  [[nodiscard]] std::uint32_t GetSoundKitAdvancedUsage(std::uint32_t sound_kit_id) const;
  [[nodiscard]] std::uint32_t GetSoundKitVariationCount(std::uint32_t id) const;
  [[nodiscard]] SoundKitPlaybackVolume ResolveSoundKitPlaybackVolume(
      const SoundKitData& kit, const SoundKitPlaybackOptions& options = {});
  [[nodiscard]] std::uint32_t LookupSoundKitIdByName(std::string_view name) const;
  void ResetSoundKitVariationSelectionState(std::uint32_t sound_kit_id);
  [[nodiscard]] std::uint32_t ResolveUnitSoundKit(std::uint32_t display_info_id, std::uint32_t sound_lookup_id, bool use_wet_variant) const;
  [[nodiscard]] std::size_t GetDspFilterChainCount() const { return dsp_filters_.size(); }
  [[nodiscard]] std::size_t GetDspFilterNodeCount(std::string_view filter_name) const;
  [[nodiscard]] const DspFilterNode *GetDspFilterNode(std::string_view filter_name, std::size_t index) const;
  [[nodiscard]] std::size_t
  GetDspFilterOutputParameterWriteCount(std::string_view filter_name) const;
  [[nodiscard]] const DspParameterWrite *
  GetDspFilterOutputParameterWrite(std::string_view filter_name, std::size_t index) const;
  [[nodiscard]] float GetDspFilterActivationDelay(std::string_view filter_name) const;
  [[nodiscard]] std::uint32_t GetDspFilterBoundHandleId(std::string_view filter_name) const;
  [[nodiscard]] const EmotesTextSoundTable &GetEmotesTextSoundTable() const { return emotes_text_sound_; }
  [[nodiscard]] std::uint32_t GetWeaponImpactSoundKit(std::uint32_t weapon_subclass_id, std::uint32_t parry_type, std::uint32_t impact_slot, bool critical) const { return weapon_impact_sounds_.GetSoundKit(weapon_subclass_id, parry_type, impact_slot, critical); }
  void PlayWeaponImpactSound(const WeaponImpactSelection &selection, bool critical, const float *position, bool use_listener_priority);
  [[nodiscard]] std::uint32_t GetWeaponImpactItemSubclassCount() const { return weapon_impact_sounds_.GetItemSubclassCount(); }
  [[nodiscard]] std::size_t GetWeaponImpactSoundPairCount() const { return weapon_impact_sounds_.GetPairCount(); }
  [[nodiscard]] const WoundDeathSoundTable &GetWoundDeathSoundTable() const { return wound_death_sound_table_; }
  [[nodiscard]] bool
  ResolveWorldStateZoneSoundBinding(std::uint32_t previous_area_id, std::uint32_t current_area_id, std::uint32_t previous_wmo_area_id, std::uint32_t current_wmo_area_id, bool exact_wmo_match_mode, const std::function<std::uint32_t(std::uint32_t)> &resolver, WorldAudioBindingValue *out) const;
  [[nodiscard]] bool LookupChunkAudioBinding(const ChunkAudioBindingKey &key, ChunkAudioBindingValue *out) const;
  [[nodiscard]] bool LookupChunkAudioBindingForWorldPosition(std::uint32_t map_id, float world_x, float world_y, ChunkAudioBindingValue *out) const;
  void UpdateChunkAudioForPlayerPosition(std::uint32_t map_id, float world_x, float world_y);
  [[nodiscard]] std::size_t GetWorldStateZoneSoundCount() const { return world_state_zone_sounds_.size(); }
  [[nodiscard]] std::size_t GetChunkAudioBindingCount() const { return chunk_audio_bindings_.size(); }
  [[nodiscard]] std::uint32_t GetChunkAudioBindingBucketMask() const { return chunk_audio_bindings_.table_.bucket_mask(); }
  [[nodiscard]] std::uint32_t GetChunkAudioBindingBucketSlotCount() const { return chunk_audio_bindings_.table_.bucket_slot_count(); }
  [[nodiscard]] std::uint32_t GetChunkAudioBindingProbeCounter() const { return chunk_audio_bindings_.table_.probe_counter(); }
  [[nodiscard]] const WorldStateZoneSoundEntryData *
  GetWorldStateZoneSound(const std::size_t index) const {
    if (index >= world_state_zone_sounds_.size()) { return nullptr; }
    return &world_state_zone_sounds_[index];
  }
  [[nodiscard]] const SoundProviderPreferenceData *
  GetSoundProviderPreferenceData(std::uint32_t id) const;
  void SetSoundProviderPreferenceForPriority(std::uint32_t priority, std::int32_t id);
  void SetIndoorSoundArea(std::int32_t area_id);
  [[nodiscard]] std::int32_t GetIndoorSoundAreaId() const { return indoor_sound_area_id_; }
  void SetSoundProviderSelectionGuards(bool skip_priority_8, bool enable_priority_9);
  [[nodiscard]] std::int32_t SelectActiveSoundProviderPreferenceId() const;
  void RefreshWorldReverbFromActiveSoundProvider();
  [[nodiscard]] const WorldReverbProperties &GetWorldReverbProperties() const { return world_reverb_; }
  [[nodiscard]] const WorldReverbRoomLfDspState &GetWorldReverbRoomLfDspState() const { return world_reverb_room_lf_dsp_state_; }
  void SetWorldReverbProperties(const WorldReverbProperties &properties);
  void InitializeZoneMusicRepeatDelayState();
  [[nodiscard]] bool HasZoneMusicRepeatDelayState() const { return zone_music_repeat_delay_state_initialized_; }
  [[nodiscard]] std::size_t GetZoneMusicRepeatDelayCount() const { return zone_music_repeat_delay_deadlines_ms_.size(); }
  [[nodiscard]] std::uint32_t GetZoneMusicRepeatDelayDeadline(std::uint32_t sound_kit_id) const {
    if (sound_kit_id >= zone_music_repeat_delay_deadlines_ms_.size()) { return 0; }
    return zone_music_repeat_delay_deadlines_ms_[sound_kit_id];
  }
  void ResetWorldAudioStateForGlue();
  void RegisterSoundEngineUpdateCallback();
  void UnregisterSoundEngineUpdateCallback();
  void RegisterEnterWorldAudioCallbacks();
  void UnregisterEnterWorldAudioCallbacks();
  void StopAllActiveSounds();
  void StopAllSoundEffects(float fade_seconds);
  class NonPositionalPlaybackBlockScope {
  public:
    explicit NonPositionalPlaybackBlockScope(SoundRuntime &sound_interface)
        : sound_interface_(&sound_interface) { sound_interface_->PushNonPositionalPlaybackBlock(); }
    NonPositionalPlaybackBlockScope(const NonPositionalPlaybackBlockScope &) = delete;
    NonPositionalPlaybackBlockScope &operator=(const NonPositionalPlaybackBlockScope &) = delete;
    ~NonPositionalPlaybackBlockScope() {
      if (sound_interface_ != nullptr) { sound_interface_->PopNonPositionalPlaybackBlock(); }
    }
  private:
    SoundRuntime *sound_interface_{nullptr};
  };
  void PushNonPositionalPlaybackBlock();
  void PopNonPositionalPlaybackBlock();
  [[nodiscard]] std::uint32_t GetNonPositionalPlaybackBlockDepth() const { return non_positional_playback_block_depth_; }
  [[nodiscard]] NonPositionalPlaybackBlockScope BlockNonPositionalPlayback() { return NonPositionalPlaybackBlockScope(*this); }
  [[nodiscard]] float ConsumePlaybackRandomUnitFloat();
  [[nodiscard]] std::uint32_t ConsumePlaybackRandomBoundedValue(std::uint32_t bound);
  std::uint32_t AllocateSoundHandle();
  void FreeSoundHandle(std::uint32_t handle_id);
  [[nodiscard]] std::optional<SoundHandle> GetSoundHandle(std::uint32_t handle_id) const;
  [[nodiscard]] bool IsSoundHandlePlaying(std::uint32_t handle_id) const;
  [[nodiscard]] bool IsSoundKitPlayingNearby(std::uint32_t sound_kit_id, const float *position, float max_distance_squared = 6.0f) const;
  bool AttachHandleBinding(SoundHandleBinding &binding, std::uint32_t handle_id);
  std::uint32_t DetachHandleBinding(SoundHandleBinding &binding);
  [[nodiscard]] bool IsHandleBound(std::uint32_t handle_id) const;
  bool SetSoundHandleVolume(std::uint32_t handle_id, float volume);
  bool SetSoundHandlePosition(std::uint32_t handle_id, const float *position);
  bool BindSoundHandleToObjectGuid(std::uint32_t handle_id, std::uint64_t object_guid);
  bool ApplySoundHandleRelativeVolumeScale(std::uint32_t handle_id, float scale);
  void StartSoundHandleFadeIn(std::uint32_t handle_id);
  bool RequestStopSoundHandle(std::uint32_t handle_id, float fade_seconds);
  void SetBackgroundSoundState(std::uint32_t state) { background_sound_state_.slot = state; background_sound_state_.has_selection = true; }
  [[nodiscard]] bool HasBackgroundSoundSelection() const { return background_sound_state_.has_selection; }
  [[nodiscard]] std::uint32_t GetBackgroundSoundState() const { return background_sound_state_.slot; }
  void ClearBackgroundSoundState() {
    background_sound_state_ = {};
  }
  void SetCinematicSoundHandle(std::uint32_t handle) { cinematic_sound_handle_ = handle; }
  [[nodiscard]] bool IsCinematicSoundPlaying() const;
  void ApplyDspEffect(const std::string &filter_name, DspEffectType type, const std::vector<float> &params);
  [[nodiscard]] bool PrimeDspFilterForPlaybackHandle(std::string_view filter_name, std::uint32_t handle_id);
  void UpdateDspFilterActivationDelays(float delta_seconds);
  void SetCvarCallbacks(CvarSetCallback set_cb, CvarGetBoolCallback get_bool_cb, ChannelMuteCallback mute_cb);
  void SetCvarGetIntCallback(CvarGetIntCallback get_int_cb);
  void SetActivePlayerPositionCallback(ActivePlayerPositionCallback callback);
  void SetObjectPositionCallback(ObjectPositionCallback callback);
  void SetUnitLookupCallback(UnitLookupCallback callback);
  void SetPlayerLookupCallback(PlayerLookupCallback callback);
  void SetActivePlayerCallback(ActivePlayerCallback callback);
  void SetNormalizedTimeOfDayCallback(NormalizedTimeOfDayCallback callback);
  void SetLiquidQueryCallback(LiquidQueryCallback callback);
  [[nodiscard]] bool GetCVarBool(const std::string &name) const { return cvar_get_bool_cb_ ? cvar_get_bool_cb_(name) : false; }
  bool OnSoundEnableAllSound(bool value);
  bool OnSoundEnableSFX(bool value);
  bool OnSoundEnableAmbience(bool value);
  bool OnSoundEnableMusic(bool value);
  void SetListenerAtCharacter(bool at_character) { listener_at_character_ = at_character; }
  [[nodiscard]] bool IsListenerAtCharacter() const { return listener_at_character_; }
  int InitializeFull(bool restart);
  SoundDeviceRefreshResult ReconcileEnumeratedDeviceState( const SoundDeviceEnumerationState &state, bool fire_script_event);
  SoundDeviceRefreshResult RefreshEnumeratedDevicesAndReconcile(bool fire_script_event);
  [[nodiscard]] int GetEnumeratedOutputDriverCount(bool use_voice_output_devices);
  [[nodiscard]] std::string GetEnumeratedOutputDriverName(int driver_index, bool use_voice_output_devices);
  [[nodiscard]] int GetEnumeratedGameInputDriverCount();
  [[nodiscard]] std::string GetEnumeratedGameInputDriverName(int driver_index);
  [[nodiscard]] int GetEnumeratedRecordInputDriverCount();
  [[nodiscard]] std::string GetEnumeratedRecordInputDriverName(int driver_index);
  void RestartGameSoundSystemFromLua();
  void ShutdownSoundEngine();
  void ClearSoundHashTables();
  void ClearDspFilterChains();
  void ClearSoundKitProviderCaches();
  int UpdateLoop();
  int UpdateLiquidQueryPosition(double delta_seconds);
  int UpdateLiquidAmbience(double delta_seconds);
  void StopLiquidAmbience();
  int HandleBackground(openwow::game::WorldSession& session, const std::uint32_t* foreground);
  bool GetObjectPosition(std::uint64_t guid, float *pos_out) const;
  [[nodiscard]] const openwow::game::CGUnit_C *GetUnitForSound( std::uint64_t guid) const;
  [[nodiscard]] const openwow::game::CGPlayer_C *GetPlayerForSound( std::uint64_t guid) const;
  [[nodiscard]] const openwow::game::CGPlayer_C *GetActivePlayerForSound() const;
  int PlaySoundKit(std::uint32_t sound_kit_id, const float *position = nullptr, std::uint32_t *handle_out = nullptr, const SoundKitPlaybackOptions &options = {});
  int PlaySoundKitByName(std::string_view name, const float *position = nullptr, std::uint32_t *handle_out = nullptr, const SoundKitPlaybackOptions &options = {});
  [[nodiscard]] std::optional<SelectedSoundKitFile>
  SelectSoundKitFileForPlayback(std::uint32_t sound_kit_id, SoundKitVariationSelectionMode mode = SoundKitVariationSelectionMode::kConsumeFrequenciesAcrossCalls, std::optional<std::int32_t> forced_file_index = std::nullopt, std::int32_t preload_queue_hint = 5);
  std::uint32_t SelectZoneAmbienceSoundKit(int *priority_out);
  void SetZoneAmbienceSelectionForPriority(std::uint32_t priority, std::int32_t sound_ambience_id);
  void SetWeatherSoundKit(std::int32_t sound_kit_id);
  void SetZoneMusicSelectionForPriority(std::uint32_t priority, std::int32_t zone_music_id);
  void SetZoneIntroMusicSelectionForPriority(std::uint32_t priority, std::int32_t zone_intro_music_id);
  void ApplyScreenEffectAudioSelections(std::int32_t sound_ambience_id, std::int32_t zone_music_id);
  int SelectZoneMusic(int *priority_out, std::uint32_t *repeat_delay_min_out, std::uint32_t *repeat_delay_max_out);
  int UpdateZoneMusic();
  int PlayVoiceChatToggle(std::uint32_t sound_kit_id);
  void PlayErrorSpeech(std::uint32_t sound_kit_id);
  void PlayAmbientIdleSound(std::uint64_t unit_guid);
  int PlayScriptSound(const std::string &path, std::uint32_t sound_type);
  bool StopActiveSoundHandle(std::uint32_t handle_id, bool immediate, float fade_seconds, bool release_handle);

  int StopScriptMusic();

  int StopScriptMusicImmediately();
  void DetachScriptMusicHandle();
  void ResetZoneAndScriptMusicRuntime();
  void ResetZoneAmbienceRuntime();
  void InitializePlayMusicRuntime(std::int32_t sound_kit_id);
  [[nodiscard]] bool IsScriptMusicPlaying() const { return script_music_playing_; }
  [[nodiscard]] std::optional<std::uint32_t> GetActiveScriptMusicHandleId() const { return script_music_handle_id_; }
  [[nodiscard]] bool IsMusicCVarEnabled() const;
  int ChaosMode();
  ChaosModeFrameStats AdvanceChaosModeFrame(int mode, double delta_seconds, bool allow_engine_restart = false);
  [[nodiscard]] const ChaosModeFrameStats &GetLastChaosModeFrameStats() const { return last_chaos_mode_frame_stats_; }
  void RegisterChaosModeIfEnabled();
  [[nodiscard]] bool HasAdvancedKitPropertyManager() const { return advanced_kit_property_manager_initialized_; }
  [[nodiscard]] std::size_t GetAdvancedKitPropertyCount() const { return active_advanced_kit_properties_.size(); }
  void Shutdown(bool is_restart);
  void Reset();
  void BindRegisteredCvars(openwow::ui::game::CVarSystem &cvars);
  [[nodiscard]] std::size_t GetActiveHandleCount() const;
private:
  [[nodiscard]] bool AreDspEffectsEnabled() const;
  void RebuildDspFilterChains();
  [[nodiscard]] DspFilterChain &EnsureDspFilterChain(const std::string &filter_name);
  void ApplyDspEffectToChain(DspFilterChain &chain, DspEffectType type, const std::vector<float> &params);
  void ApplyDspFilterOutputScale(DspFilterChain &chain);
  void DetachDspFilterOutputBinding(DspFilterChain &chain);
  void PushSoundHandleVolumeToAudioEngine(SoundHandle &handle);
  [[nodiscard]] bool MaterializeSoundHandle(
      SoundHandle &handle);
  void ReleaseRuntimeDsp(void *runtime_dsp);
  void ClearDspFilterNodes(DspFilterChain &chain);
  void SetDspFilterChainBypass(DspFilterChain &chain, bool bypassed);
  void ApplySoundEnableReverbCVarValue(std::string_view value);
  void ApplyChannelGroupMute(const char *group_name, bool muted);
  void ClearAdvancedKitPropertyRuntime();
  void ResetCurrentZoneAmbienceRuntime();
  void SetCurrentZoneAmbienceRuntime(std::int32_t sound_kit_id, int priority, bool is_playing);
  [[nodiscard]] bool BindCurrentZoneAmbienceHandle(std::uint32_t handle_id);
  [[nodiscard]] bool IsCurrentZoneAmbiencePlaying() const;
  [[nodiscard]] std::int32_t GetCurrentZoneAmbienceSoundKitId() const;
  void ResetCurrentZoneMusicRuntime();
  void ResetZoneMusicRuntime();
  void SetCurrentZoneMusicRuntime(std::int32_t sound_kit_id, int priority, bool is_playing);
  [[nodiscard]] bool BindCurrentZoneMusicHandle(std::uint32_t handle_id);
  [[nodiscard]] bool IsCurrentZoneMusicPlaying() const;
  [[nodiscard]] std::int32_t GetCurrentZoneMusicPriority() const;
  [[nodiscard]] std::int32_t GetCurrentZoneMusicSoundKitId() const;
  int UpdateTrackedSlotPlayback(DualSlotSoundPlayer &player, std::uint32_t sound_kit_id, int zone_sound_type, const SoundKitPlaybackOptions &options, bool immediate, float fade_out_sec);
  [[nodiscard]] bool IsDualSlotActivePlaying(const DualSlotSoundPlayer &player) const;
  [[nodiscard]] std::uint32_t
  GetDualSlotActiveSoundKitId(const DualSlotSoundPlayer &player) const;
  void ResetDualSlotPlayer(DualSlotSoundPlayer &player);
  void StopDualSlotSlot(DualSlotSoundPlayer::Slot &slot, bool immediate, float fade_sec, bool release);
  void ReverseSoundHandleFade(std::uint32_t handle_id);
  [[nodiscard]] bool CanSelectZoneMusicCandidate(std::int32_t sound_kit_id, int priority) const;
  [[nodiscard]] const SoundAmbienceTableEntryData *
  GetSoundAmbienceTableEntry(std::uint32_t id) const;
  [[nodiscard]] const ZoneMusicTableEntryData *GetZoneMusicTableEntry(std::uint32_t id) const;
  [[nodiscard]] const ZoneIntroMusicTableEntryData *
  GetZoneIntroMusicTableEntry(std::uint32_t id) const;
  void ArmZoneMusicSelectionDelayStopAction(std::int32_t expected_sound_kit_id, std::uint32_t repeat_delay_min_ms, std::uint32_t repeat_delay_max_ms);
  void ArmZoneMusicRuntimeResetStopAction();
  void ArmZoneMusicRepeatDelayDeadlineStopAction(std::int32_t sound_kit_id, std::uint32_t repeat_delay_minutes);
  void ClearZoneMusicStopAction();
  void ApplyZoneMusicSelectionDelayStopAction();
  void ApplyZoneMusicRepeatDelayDeadlineStopAction(std::int32_t sound_kit_id, std::uint32_t repeat_delay_minutes);
  void HandleStoppedSoundHandle(const SoundHandle &handle);
  void ClearHandleBinding(std::uint32_t handle_id);
  void CleanupHandlePlaybackState(const SoundHandle &handle);
  void OnNaturalPlaybackComplete(SoundHandle &handle);
  void PollNaturalPlaybackCompletions();
  void ReleaseExclusiveRepeatTracking(SoundHandle &handle);
  void ReleaseEngineHandle(SoundHandle &handle);
  void FinalizeStoppedSoundHandle(SoundHandle &handle);
  [[nodiscard]] bool UpdateStoppingSoundHandle(SoundHandle &handle, int delta_ms);
  void UpdateFadingInSoundHandle(SoundHandle &handle, int delta_ms);
  void EnsureAdvancedKitPropertyManager();
  void DestroyAdvancedKitPropertyManager();
  void AddAdvancedKitProperty(std::uint32_t handle_id, const SoundKitData &kit);
  void RefreshBoundObjectSoundPositions();
  void UpdateAdvancedKitProperties(int delta_ms);
  [[nodiscard]] std::uint32_t ResolveVirtualPlayWindowMs() const;
  [[nodiscard]] bool UsesVirtualPlayWindow(const SoundHandle &handle) const;
  [[nodiscard]] bool IsHandleAudibleAtListener(const SoundHandle &handle, const float *listener_position) const;
  void RestartVirtualizedHandle(SoundHandle &handle);
  void UpdateVirtualPlayWindow(SoundHandle &handle, const float *listener_position,
                               int delta_ms);
  [[nodiscard]] float ComputeAdvancedDuckingScaleForHandle(std::uint32_t handle_id, std::uint32_t sound_type) const;
  [[nodiscard]] bool StartDirectSoundKitPlayback(std::uint32_t handle_id, const SoundKitData &kit, const float *position, const SoundKitPlaybackOptions &options);
  [[nodiscard]] int QueueAdvancedSoundKitPlayback(const SoundKitData &kit, const SoundKitPlaybackOptions &options, const float *position, std::uint32_t *handle_out);
  void UpdateManagedAdvancedSound(std::uint32_t handle_id, SoundHandle &handle, std::uint32_t current_day_time_ms, int delta_ms, const float *listener_position);
  void LinkAdvancedSoundEntriesToKits();
  [[nodiscard]] std::optional<SelectedSoundKitFile>
  SelectSoundKitFileForPlayback(const SoundKitData &kit, SoundKitVariationSelectionMode mode, std::optional<std::int32_t> forced_file_index, std::int32_t preload_queue_hint);
  [[nodiscard]] bool GetActivePlayerPosition(float *position_out) const;
  [[nodiscard]] std::uint32_t ResolveCurrentDayTimeMs() const;
  [[nodiscard]] std::size_t ResolveTimeOfDayIndex() const;
  [[nodiscard]] const LiquidTypeSoundData *
  GetLiquidTypeSoundData(std::uint32_t liquid_type_id) const;
  void RequestLiquidAmbienceStop();
  void ClearLiquidAmbienceRuntime();
};
[[nodiscard]] std::uint32_t GetUnitGender(SoundRuntime& sound_runtime, std::uint64_t guid);
void PlayEmoteSound(SoundRuntime& sound_runtime, std::uint64_t unit_guid,
                    std::uint32_t sound_kit_id, bool is_npc,
                    float explicit_volume);
void PlayEmoteSoundForUnit(SoundRuntime& sound_runtime, std::uint64_t unit_guid,
                           std::uint32_t emote_text_id,
                           float explicit_volume);
void PlayEmoteAmbientSound(SoundRuntime& sound_runtime, std::uint64_t unit_guid);
void PlayMaterialFoleySound(SoundRuntime& sound_runtime, std::uint32_t material_id, const float* position, bool is_self);
void PlayMaterialSheatheSound(SoundRuntime& sound_runtime, std::uint32_t material_id, bool sheathe, const float* position, bool is_self);
void PlayArmorFoleySound(SoundRuntime& sound_runtime, std::uint64_t unit_guid, std::uint64_t active_player_guid);
void SoundInterface_RegisterCVars(SoundRuntime& sound_runtime);
void SetVoiceChatOutputDriverIndexCVar(int driver_index);
void SetVoiceChatInputDriverIndexCVar(int driver_index);
}
