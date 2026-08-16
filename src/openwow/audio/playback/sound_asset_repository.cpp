#include "openwow/audio/playback/sound_runtime_internal.h"

namespace openwow::audio {

const SoundKitData *SoundRuntime::GetSoundKitData(std::uint32_t id) const {
  if (id == 0 || id >= max_sound_kit_id_)
    return nullptr;
  return sound_kit_index_[id];
}

void SoundRuntime::ApplyExclusiveRepeatModeToSoundKit(const std::uint32_t id,
                                                      const SoundKitExclusivityMode mode) {
  if (id == 0 || id >= max_sound_kit_id_) {
    return;
  }
  auto *kit = sound_kit_index_[id];
  if (kit == nullptr) {
    return;
  }
  switch (mode) {
  case SoundKitExclusivityMode::kEnableExclusiveRepeat:
    kit->flags |= kSoundKitFlagExclusiveRepeat;
    break;
  case SoundKitExclusivityMode::kDisableExclusiveRepeat:
    kit->flags &= ~kSoundKitFlagExclusiveRepeat;
    break;
  case SoundKitExclusivityMode::kUseSoundKit:
  default:
    break;
  }
}

bool SoundRuntime::SoundKitHasLoopFlag(std::uint32_t sound_kit_id) const {
  const auto *kit = GetSoundKitData(sound_kit_id);
  if (!kit) {
    return false;
  }
  return (kit->flags & kSoundKitFlagLoop) != 0;
}

std::uint32_t SoundRuntime::GetSoundKitAdvancedUsage(std::uint32_t sound_kit_id) const {
  const auto *kit = GetSoundKitData(sound_kit_id);
  if (kit && kit->advanced.has_value()) {
    return kit->advanced->usage;
  }
  return 2;
}

std::uint32_t SoundRuntime::GetSoundKitVariationCount(const std::uint32_t id) const {
  const auto *kit = GetSoundKitData(id);
  return kit != nullptr ? kit->file_count : 0u;
}

std::optional<SelectedSoundKitFile> SoundRuntime::SelectSoundKitFileForPlayback(
    const std::uint32_t sound_kit_id, const SoundKitVariationSelectionMode mode,
    const std::optional<std::int32_t> forced_file_index, const std::int32_t preload_queue_hint) {
  const auto *kit = GetSoundKitData(sound_kit_id);
  if (!kit || kit->file_count == 0) {
    return std::nullopt;
  }

  return SelectSoundKitFileForPlayback(*kit, mode, forced_file_index, preload_queue_hint);
}

std::optional<SelectedSoundKitFile> SoundRuntime::SelectSoundKitFileForPlayback(
    const SoundKitData &kit, const SoundKitVariationSelectionMode mode,
    const std::optional<std::int32_t> forced_file_index, const std::int32_t preload_queue_hint) {
  if (kit.file_count == 0 || kit.id >= sound_kit_playback_runtime_.size()) {
    return std::nullopt;
  }

  auto &runtime = sound_kit_playback_runtime_[kit.id];
  auto build_selection =
      [&](const std::int32_t selected_index) -> std::optional<SelectedSoundKitFile> {
    if (selected_index < 0 || static_cast<std::uint32_t>(selected_index) >= kit.file_count) {
      return std::nullopt;
    }

    return SelectedSoundKitFile{
        .sound_kit_id = kit.id,
        .index = selected_index,
        .path = kit.file_paths[static_cast<std::size_t>(selected_index)],
    };
  };

  if (forced_file_index.has_value() && *forced_file_index >= 0) {
    const auto selected_index =
        std::min<std::uint32_t>(static_cast<std::uint32_t>(*forced_file_index), kit.file_count - 1);
    runtime.last_selected_index = static_cast<std::int32_t>(selected_index);
    return build_selection(runtime.last_selected_index);
  }

  if (runtime.hold_last_selected_file) {
    if (runtime.last_selected_index < 0) {
      runtime.last_selected_index = 0;
    }
    return build_selection(runtime.last_selected_index);
  }

  if (kit.file_count == 1) {
    runtime.last_selected_index = 0;
    return build_selection(0);
  }

  std::uint32_t available_frequency = 0;
  std::int32_t retry_count = 0;
  while (true) {
    if (retry_count == 2) {
      runtime.last_selected_index = -1;
    } else if (retry_count >= 3) {
      return std::nullopt;
    }

    available_frequency = 0;
    for (std::uint32_t index = 0; index < kit.file_count; ++index) {
      if (static_cast<std::int32_t>(index) == runtime.last_selected_index) {
        continue;
      }
      available_frequency += runtime.remaining_frequencies[index];
    }
    if (available_frequency != 0) {
      break;
    }

    switch (mode) {
    case SoundKitVariationSelectionMode::kCycleWithoutImmediateRepeat:
      runtime.remaining_frequencies.fill(0);
      for (std::uint32_t index = 0; index < kit.file_count; ++index) {
        runtime.remaining_frequencies[index] = 1;
      }
      break;
    case SoundKitVariationSelectionMode::kResetFrequenciesEachCall:
    case SoundKitVariationSelectionMode::kConsumeFrequenciesAcrossCalls:
      runtime.remaining_frequencies.fill(0);
      for (std::uint32_t index = 0; index < kit.file_count; ++index) {
        runtime.remaining_frequencies[index] = kit.frequencies[index];
      }
      break;
    default:
      return std::nullopt;
    }

    ++retry_count;
  }

  std::uint32_t chooser = 0;
  if (mode != SoundKitVariationSelectionMode::kCycleWithoutImmediateRepeat) {
    EnsurePlaybackRandomStateSeeded(random_seed_);
    chooser = retail_rng::AdlerSeedNextBoundedValue(available_frequency,
                                                     random_seed_);
  }

  auto pending_ready_state = openwow::vfs::DataPreloadPathReadyState::kReady;
  if (runtime.pending_preload_index >= 0) {
    if (static_cast<std::uint32_t>(runtime.pending_preload_index) >= kit.file_count) {
      runtime.pending_preload_index = -1;
    } else {
      pending_ready_state = openwow::vfs::QueryDataPreloadPathReadyState(
          kit.file_paths[static_cast<std::size_t>(runtime.pending_preload_index)].c_str());
      if (!KeepsPendingPreloadRequest(pending_ready_state)) {
        runtime.pending_preload_index = -1;
      }
    }
  }

  std::int32_t candidate_index = -1;
  for (std::uint32_t index = 0; index < kit.file_count; ++index) {
    if (static_cast<std::int32_t>(index) == runtime.last_selected_index) {
      continue;
    }

    const std::uint32_t weight = runtime.remaining_frequencies[index];
    if (chooser < weight) {
      candidate_index = static_cast<std::int32_t>(index);
      break;
    }
    chooser -= weight;
  }

  if (candidate_index < 0) {
    runtime.last_selected_index = 0;
    return build_selection(0);
  }

  auto candidate_ready_state = pending_ready_state;
  if (IsStreamingPlaybackSelectionEnabled()) {
    candidate_ready_state = openwow::vfs::QueryDataPreloadPathReadyState(
        kit.file_paths[static_cast<std::size_t>(candidate_index)].c_str());
  }

  std::int32_t selected_index = candidate_index;
  if (!UsesReadySoundFile(candidate_ready_state)) {
    if (runtime.pending_preload_index < 0) {
      (void)openwow::vfs::RequestDataPreloadPathAvailability(
          kit.file_paths[static_cast<std::size_t>(candidate_index)].c_str(), preload_queue_hint,
          false);
      runtime.pending_preload_index = candidate_index;
    }

    if (IsStreamingPlaybackSelectionEnabled()) {
      for (std::int32_t fallback_index = candidate_index - 1; fallback_index >= 0;
           --fallback_index) {
        const auto ready_state = openwow::vfs::QueryDataPreloadPathReadyState(
            kit.file_paths[static_cast<std::size_t>(fallback_index)].c_str());
        if (UsesReadySoundFile(ready_state)) {
          selected_index = fallback_index;
          break;
        }
      }
    }
  } else if (runtime.remaining_frequencies[static_cast<std::size_t>(candidate_index)] != 0) {
    --runtime.remaining_frequencies[static_cast<std::size_t>(candidate_index)];
  }

  if (mode == SoundKitVariationSelectionMode::kResetFrequenciesEachCall) {
    runtime.remaining_frequencies.fill(0);
  }

  runtime.last_selected_index = selected_index < 0 ? 0 : selected_index;
  return build_selection(runtime.last_selected_index);
}

std::uint32_t SoundRuntime::LookupSoundKitIdByName(std::string_view name) const {
  if (name.empty()) {
    return 0;
  }

  const std::string query(name);
  const auto bucket_it = sound_kit_name_hash_index_.find(openwow::core::SStrHashCI(query.c_str()));
  if (bucket_it == sound_kit_name_hash_index_.end()) {
    return 0;
  }

  for (auto entry_it = bucket_it->second.rbegin(); entry_it != bucket_it->second.rend();
       ++entry_it) {
    const auto &kit = sound_kit_storage_[*entry_it];
    if (openwow::core::SStrCmpNoCase(kit.name.c_str(), query.c_str(), 0x7FFFFFFFu) == 0) {
      return kit.id;
    }
  }

  return 0;
}

void SoundRuntime::ResetSoundKitVariationSelectionState(const std::uint32_t sound_kit_id) {
  const auto *kit = GetSoundKitData(sound_kit_id);
  if (kit == nullptr || kit->file_count == 0 ||
      sound_kit_id >= sound_kit_playback_runtime_.size()) {
    return;
  }

  auto &runtime = sound_kit_playback_runtime_[sound_kit_id];
  runtime.remaining_frequencies.fill(0);
  runtime.last_selected_index = -1;
}

std::uint32_t SoundRuntime::ResolveUnitSoundKit(const std::uint32_t display_info_id,
                                                  const std::uint32_t sound_lookup_id,
                                                  const bool use_wet_variant) const {
  return unit_sound_kit_lookup_.ResolveSoundKit(display_info_id, sound_lookup_id, use_wet_variant);
}

std::size_t SoundRuntime::GetDspFilterNodeCount(const std::string_view filter_name) const {
  const auto it = dsp_filters_.find(std::string(filter_name));
  if (it == dsp_filters_.end()) {
    return 0;
  }
  return it->second.nodes.size();
}

const DspFilterNode *SoundRuntime::GetDspFilterNode(const std::string_view filter_name,
                                                      const std::size_t index) const {
  const auto it = dsp_filters_.find(std::string(filter_name));
  if (it == dsp_filters_.end() || index >= it->second.nodes.size()) {
    return nullptr;
  }
  return &it->second.nodes[index];
}

std::size_t
SoundRuntime::GetDspFilterOutputParameterWriteCount(const std::string_view filter_name) const {
  const auto it = dsp_filters_.find(std::string(filter_name));
  if (it == dsp_filters_.end()) {
    return 0;
  }
  return it->second.output_parameter_writes.size();
}

const DspParameterWrite *
SoundRuntime::GetDspFilterOutputParameterWrite(const std::string_view filter_name,
                                                 const std::size_t index) const {
  const auto it = dsp_filters_.find(std::string(filter_name));
  if (it == dsp_filters_.end() || index >= it->second.output_parameter_writes.size()) {
    return nullptr;
  }
  return &it->second.output_parameter_writes[index];
}

float SoundRuntime::GetDspFilterActivationDelay(const std::string_view filter_name) const {
  const auto it = dsp_filters_.find(std::string(filter_name));
  if (it == dsp_filters_.end()) {
    return 0.0f;
  }
  return it->second.activation_delay_seconds;
}

std::uint32_t SoundRuntime::GetDspFilterBoundHandleId(const std::string_view filter_name) const {
  const auto it = dsp_filters_.find(std::string(filter_name));
  if (it == dsp_filters_.end()) {
    return 0;
  }
  return it->second.output_binding.active_handle_id;
}

void SoundRuntime::SetWorldReverbProperties(const WorldReverbProperties &properties) {
  world_reverb_ = properties;
  world_reverb_room_lf_dsp_state_ = BuildWorldReverbRoomLfDspState(
      world_reverb_, static_cast<float>(audio_engine_->GetOutputRate()));
  auto &audio_engine = *audio_engine_;
  if (world_reverb_enabled_) {
    const auto mixer_controls = BuildWorldReverbMixerControls(world_reverb_);
    audio_engine.ConfigureWorldReverb(mixer_controls.room_size, mixer_controls.damping,
                                      mixer_controls.wet, mixer_controls.dry,
                                      mixer_controls.width);
  }
  audio_engine.SetWorldReverbEnabled(world_reverb_enabled_);
}

void SoundRuntime::InitializeZoneMusicRepeatDelayState() {
  zone_music_repeat_delay_state_initialized_ = true;
  zone_music_repeat_delay_deadlines_ms_.assign(max_sound_kit_id_, 0);
}

void SoundRuntime::LoadSoundEntries(const std::vector<SoundKitData> &entries) {
  sound_kit_storage_.clear();
  sound_kit_storage_.reserve(entries.size());
  for (const auto &entry : entries) {
    sound_kit_storage_.push_back(BuildLoadedSoundKitData(entry));
  }
  sound_kit_name_hash_index_.clear();

  max_sound_kit_id_ = 0;
  for (const auto &e : sound_kit_storage_) {
    if (e.id >= max_sound_kit_id_) {
      max_sound_kit_id_ = e.id + 1;
    }
  }

  sound_kit_index_.assign(max_sound_kit_id_, nullptr);
  sound_kit_playback_runtime_.assign(max_sound_kit_id_, SoundKitPlaybackRuntimeState{});
  for (std::size_t storage_index = 0; storage_index < sound_kit_storage_.size(); ++storage_index) {
    auto &e = sound_kit_storage_[storage_index];
    if (e.id > 0 && e.id < max_sound_kit_id_) {
      sound_kit_index_[e.id] = &e;
    }
    if (!e.name.empty()) {
      sound_kit_name_hash_index_[openwow::core::SStrHashCI(e.name.c_str())].push_back(
          storage_index);
    }
  }

  LinkAdvancedSoundEntriesToKits();

  if (zone_music_repeat_delay_state_initialized_) {
    InitializeZoneMusicRepeatDelayState();
  }
}

void SoundRuntime::LoadSoundEntriesAdvanced(const std::vector<AdvancedSoundEntryData> &entries) {
  advanced_sound_entries_.clear();
  for (const auto &entry : entries) {
    advanced_sound_entries_[entry.id] = entry;
  }

  LinkAdvancedSoundEntriesToKits();
}

void SoundRuntime::LoadWorldStateZoneSounds(
    const std::vector<WorldStateZoneSoundEntryData> &entries) {
  world_state_zone_sounds_.clear();
  world_state_zone_sounds_.reserve(entries.size());
  for (const auto &entry : entries) {
    world_state_zone_sounds_.push_back(entry);
  }
}

void SoundRuntime::FreeWorldStateZoneSounds() {
  std::vector<WorldStateZoneSoundEntryData>().swap(world_state_zone_sounds_);
}

void SoundRuntime::LoadSoundAmbienceTable(
    const std::vector<SoundAmbienceTableEntryData> &entries) {
  std::uint32_t max_id = 0;
  for (const auto &entry : entries) {
    if (entry.id >= max_id) {
      max_id = entry.id + 1;
    }
  }

  sound_ambience_index_.assign(max_id, std::nullopt);
  for (const auto &entry : entries) {
    if (entry.id < sound_ambience_index_.size()) {
      sound_ambience_index_[entry.id] = entry;
    }
  }
}

std::uint32_t ComputeChunkAudioBindingLookupHash(const ChunkAudioBindingKey &key) {
  char buffer[64];
  openwow::core::SStrPrintf(
      buffer, sizeof(buffer), "%010d_%010d_%010d_%010d_%010d",
      static_cast<std::int32_t>(key.map_id), static_cast<std::int32_t>(key.tile_y),
      static_cast<std::int32_t>(key.tile_x), static_cast<std::int32_t>(key.chunk_y),
      static_cast<std::int32_t>(key.chunk_x));
  return openwow::core::SStrHashCI(buffer);
}

void SoundAssetRuntimeState::ChunkAudioLookupTable::Reset() {
  std::unordered_map<ChunkAudioBindingKey, ChunkAudioBindingValue, KeyHash>().swap(bindings_);
  table_.Reset();
}

void SoundAssetRuntimeState::ChunkAudioLookupTable::Reserve(const std::size_t entry_count) {
  bindings_.reserve(entry_count);
}

void SoundAssetRuntimeState::ChunkAudioLookupTable::Upsert(const ChunkAudioBindingEntry &entry) {
  const auto [it, inserted] = bindings_.try_emplace(entry.key, entry.value);
  if (!inserted) {
    it->second = entry.value;
    return;
  }
  (void)table_.InsertHashedKey(ComputeChunkAudioBindingLookupHash(entry.key));
}

bool SoundAssetRuntimeState::ChunkAudioLookupTable::Lookup(
    const ChunkAudioBindingKey &key, ChunkAudioBindingValue *out) const {
  const auto it = bindings_.find(key);
  if (it == bindings_.end()) return false;
  if (out != nullptr) *out = it->second;
  return true;
}

void SoundRuntime::LoadChunkAudioBindings(
    const std::vector<ChunkAudioBindingEntry> &entries) {
  chunk_audio_bindings_.Reset();
  chunk_audio_bindings_.Reserve(entries.size());
  for (const auto &entry : entries) chunk_audio_bindings_.Upsert(entry);
}

void SoundRuntime::LoadSoundProviderPreferences(
    const std::vector<SoundProviderPreferenceData> &entries) {
  sound_provider_preferences_.clear();
  sound_provider_preferences_.reserve(entries.size());
  for (const auto &entry : entries) {
    sound_provider_preferences_[entry.id] = entry;
  }

  RefreshWorldReverbFromActiveSoundProvider();
}

void SoundRuntime::LoadLiquidTypeSoundData(const std::vector<LiquidTypeSoundData> &entries) {
  liquid_type_sound_data_.clear();
  liquid_type_sound_data_.reserve(entries.size());
  for (const auto &entry : entries) {
    liquid_type_sound_data_[entry.liquid_type_id] = entry;
  }
}

void SoundRuntime::LoadEmotesTextSound(
    const std::vector<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t>>
        &entries,
    std::uint32_t max_emote_text_id) {
  emotes_text_sound_.Load(max_emote_text_id);
  for (const auto &[emote_text_id, race_id, gender_id, sound_kit_id] : entries) {
    emotes_text_sound_.Insert(emote_text_id, race_id, gender_id, sound_kit_id);
  }
}

void SoundRuntime::LoadSoundFilters(
    const std::vector<std::pair<std::string, std::vector<DspFilterNode>>> &filters) {
  dsp_filter_definitions_ = filters;
  RebuildDspFilterChains();
}

void SoundRuntime::RebuildDspFilterChains() {
  ClearDspFilterChains();
  for (const auto &[name, nodes] : dsp_filter_definitions_) {

    (void)EnsureDspFilterChain(name);

    if (!AreDspEffectsEnabled()) {
      continue;
    }

    for (const auto &node : nodes) {
      ApplyDspEffect(name, node.type, node.params);
    }
  }

  for (auto &[_, chain] : dsp_filters_) {
    SetDspFilterChainBypass(chain, false);
  }
}

void SoundRuntime::ClearDspFilterChains() {
  auto &engine = *sound_engine_;
  for (auto &[_, chain] : dsp_filters_) {
    ClearDspFilterNodes(chain);
    if (chain.output_dsp != nullptr) {
      engine.DestroyDSP(chain.output_dsp);
      chain.output_dsp = nullptr;
    }
    DetachDspFilterOutputBinding(chain);
    chain.output_parameter_writes.clear();
    chain.activation_delay_seconds = 0.0f;
  }
  dsp_filters_.clear();
}

void SoundRuntime::ClearSoundKitProviderCaches() {
  sound_engine_->PurgeSoundCache(true);
}

void SoundRuntime::LoadZoneMusicTable(const std::vector<ZoneMusicTableEntryData> &entries,
                                        const std::uint32_t max_zone_id) {
  zone_music_.clear();
  zone_music_.reserve(entries.size());
  for (const auto &entry : entries) {
    zone_music_[entry.id] = entry;
  }
  (void)max_zone_id;
}

void SoundRuntime::LoadZoneIntroMusicTable(
    const std::vector<ZoneIntroMusicTableEntryData> &entries) {
  zone_intro_music_.clear();
  zone_intro_music_.reserve(entries.size());
  for (const auto &entry : entries) {
    zone_intro_music_[entry.id] = entry;
  }
}

void SoundRuntime::LoadWoundDeathSoundTable(
    const std::vector<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>> &rows) {
  wound_death_sound_table_.Load(rows);
}

void SoundRuntime::LoadWeaponImpactSounds(const std::vector<WeaponImpactSoundRowData> &rows,
                                            const std::uint32_t item_subclass_count) {
  weapon_impact_sounds_.Load(rows, item_subclass_count);
}

void SoundRuntime::PlayWeaponImpactSound(const WeaponImpactSelection &selection,
                                           const bool critical, const float *position,
                                           const bool use_listener_priority) {
  if (weapon_impact_sounds_.GetItemSubclassCount() == 0 || position == nullptr) {
    return;
  }

  SoundKitPlaybackOptions options;
  options.sound_type = kWeaponImpactSoundType;
  if (use_listener_priority) {
    options.playback_priority = kWeaponImpactListenerPriority;
  }

  const float adjusted_position[3] = {
      position[0],
      position[1],
      position[2] + kWeaponImpactHeightBias,
  };

  (void)PlaySoundKit(weapon_impact_sounds_.GetSoundKit(selection.weapon_subclass_id,
                                                       selection.parry_type, selection.impact_slot,
                                                       critical),
                     adjusted_position, nullptr, options);
}

void SoundRuntime::RebuildUnitSoundKitLookup(const std::vector<UnitSoundLookupRow> &rows,
                                               const std::uint32_t max_sound_slot_index) {
  unit_sound_kit_lookup_.RebuildSoundKitIndex(rows, max_sound_slot_index);
}

void SoundRuntime::ConfigureUnitSoundDisplayInfoRange(const std::uint32_t min_display_info_id,
                                                        const std::uint32_t max_display_info_id) {
  unit_sound_kit_lookup_.ConfigureDisplayInfoRange(min_display_info_id, max_display_info_id);
}

bool SoundRuntime::SetUnitSoundDisplayInfo(const std::uint32_t display_info_id,
                                             const std::uint32_t sound_slot_index) {
  return unit_sound_kit_lookup_.SetDisplayInfo(display_info_id, sound_slot_index);
}

bool SoundRuntime::ClearUnitSoundDisplayInfo(const std::uint32_t display_info_id) {
  return unit_sound_kit_lookup_.ClearDisplayInfo(display_info_id);
}

bool SoundRuntime::LookupChunkAudioBinding(const ChunkAudioBindingKey &key,
                                           ChunkAudioBindingValue *out) const {
  return chunk_audio_bindings_.Lookup(key, out);
}

bool SoundRuntime::LookupChunkAudioBindingForWorldPosition(
    const std::uint32_t map_id, const float world_x, const float world_y,
    ChunkAudioBindingValue *out) const {
  openwow::world::TileCoord tile;
  openwow::world::ChunkCoord chunk;
  openwow::world::WorldToChunk(world_x, world_y, tile, chunk);
  return LookupChunkAudioBinding(
      {map_id, static_cast<std::uint32_t>(tile.y),
       static_cast<std::uint32_t>(tile.x), static_cast<std::uint32_t>(chunk.y),
       static_cast<std::uint32_t>(chunk.x)},
      out);
}

void SoundRuntime::UpdateChunkAudioForPlayerPosition(
    const std::uint32_t map_id, const float world_x, const float world_y) {
  openwow::world::TileCoord tile;
  openwow::world::ChunkCoord chunk;
  openwow::world::WorldToChunk(world_x, world_y, tile, chunk);
  const auto chunk_x = static_cast<std::int32_t>(chunk.x);
  const auto chunk_y = static_cast<std::int32_t>(chunk.y);
  if (chunk_x == chunk_audio_cached_chunk_x_ &&
      chunk_y == chunk_audio_cached_chunk_y_) {
    return;
  }

  ChunkAudioBindingValue binding{};
  const ChunkAudioBindingKey key{
      map_id, static_cast<std::uint32_t>(tile.y),
      static_cast<std::uint32_t>(tile.x), static_cast<std::uint32_t>(chunk.y),
      static_cast<std::uint32_t>(chunk.x)};
  if (LookupChunkAudioBinding(key, &binding)) {
    if (binding.sound_provider_preferences_id >= 0)
      SetSoundProviderPreferenceForPriority(2, binding.sound_provider_preferences_id);
    if (binding.sound_ambience_id >= 0)
      SetZoneAmbienceSelectionForPriority(2, binding.sound_ambience_id);
    if (binding.zone_intro_music_id >= 0)
      SetZoneIntroMusicSelectionForPriority(7, binding.zone_intro_music_id);
    if (binding.zone_music_id >= 0)
      SetZoneMusicSelectionForPriority(2, binding.zone_music_id);
  } else {
    SetSoundProviderPreferenceForPriority(2, -1);
    SetZoneAmbienceSelectionForPriority(2, -1);
    SetZoneIntroMusicSelectionForPriority(7, -1);
    SetZoneMusicSelectionForPriority(2, -1);
  }

  chunk_audio_cached_chunk_x_ = chunk_x;
  chunk_audio_cached_chunk_y_ = chunk_y;
}

bool SoundRuntime::ResolveWorldStateZoneSoundBinding(
    const std::uint32_t previous_area_id, const std::uint32_t current_area_id,
    const std::uint32_t previous_wmo_area_id, const std::uint32_t current_wmo_area_id,
    const bool exact_wmo_match_mode, const std::function<std::uint32_t(std::uint32_t)> &resolver,
    WorldAudioBindingValue *out) const {
  WorldAudioBindingValue selected{};
  bool found = false;

  if (exact_wmo_match_mode) {
    for (const auto &entry : world_state_zone_sounds_) {
      const bool matches_previous =
          previous_wmo_area_id > 0 && previous_wmo_area_id == entry.wmo_area_id;
      const bool matches_current =
          current_wmo_area_id > 0 && current_wmo_area_id == entry.wmo_area_id;
      if (!matches_previous && !matches_current) {
        continue;
      }
      if (!MatchesWorldStateZoneSoundGate(entry, resolver)) {
        continue;
      }

      selected = entry.value;
      found = true;
      if (matches_current) {
        if (out != nullptr) {
          *out = selected;
        }
        return true;
      }
    }
  } else if (previous_wmo_area_id > 0 || current_wmo_area_id > 0) {
    int best_rank = 4;
    for (const auto &entry : world_state_zone_sounds_) {
      int rank = 4;
      if (previous_area_id > 0 && previous_area_id == entry.area_id) {
        rank = 3;
      }
      if (current_area_id > 0 && current_area_id == entry.area_id) {
        rank = 2;
      }
      if (current_wmo_area_id > 0 && current_wmo_area_id == entry.wmo_area_id) {
        rank = 0;
      }
      if (rank >= best_rank || !MatchesWorldStateZoneSoundGate(entry, resolver)) {
        continue;
      }

      selected = entry.value;
      found = true;
      best_rank = rank;
      if (rank == 0) {
        if (out != nullptr) {
          *out = selected;
        }
        return true;
      }
    }
  } else {
    for (const auto &entry : world_state_zone_sounds_) {
      const bool matches_previous = previous_area_id > 0 && previous_area_id == entry.area_id;
      const bool matches_current = current_area_id > 0 && current_area_id == entry.area_id;
      if (!matches_previous && !matches_current) {
        continue;
      }
      if (!MatchesWorldStateZoneSoundGate(entry, resolver)) {
        continue;
      }

      selected = entry.value;
      found = true;
      if (matches_current) {
        if (out != nullptr) {
          *out = selected;
        }
        return true;
      }
    }
  }

  if (out != nullptr) {
    *out = found ? selected : WorldAudioBindingValue{};
  }
  return found;
}

const SoundProviderPreferenceData *
SoundRuntime::GetSoundProviderPreferenceData(const std::uint32_t id) const {
  const auto it = sound_provider_preferences_.find(id);
  if (it == sound_provider_preferences_.end()) {
    return nullptr;
  }
  return &it->second;
}

const SoundAmbienceTableEntryData *
SoundRuntime::GetSoundAmbienceTableEntry(const std::uint32_t id) const {
  if (id >= sound_ambience_index_.size()) {
    return nullptr;
  }

  const auto &entry = sound_ambience_index_[id];
  if (!entry.has_value()) {
    return nullptr;
  }

  return &*entry;
}

const ZoneMusicTableEntryData *
SoundRuntime::GetZoneMusicTableEntry(const std::uint32_t id) const {
  const auto it = zone_music_.find(id);
  if (it == zone_music_.end()) {
    return nullptr;
  }
  return &it->second;
}

const ZoneIntroMusicTableEntryData *
SoundRuntime::GetZoneIntroMusicTableEntry(const std::uint32_t id) const {
  const auto it = zone_intro_music_.find(id);
  if (it == zone_intro_music_.end()) {
    return nullptr;
  }
  return &it->second;
}

void SoundRuntime::SetSoundProviderPreferenceForPriority(const std::uint32_t priority,
                                                           const std::int32_t id) {
  if (priority >= sound_provider_preference_ids_.size()) {
    return;
  }

  sound_provider_preference_ids_[priority] = id;
  RefreshWorldReverbFromActiveSoundProvider();
}

void SoundRuntime::SetIndoorSoundArea(const std::int32_t area_id) {
  const bool changed = (area_id != indoor_sound_area_id_);
  indoor_sound_area_id_ = area_id;
  indoor_sound_area_changed_ = changed;
  enable_priority_9_sound_provider_selection_ = (area_id != 0);
  RefreshWorldReverbFromActiveSoundProvider();
}

void SoundRuntime::SetSoundProviderSelectionGuards(const bool skip_priority_8,
                                                     const bool enable_priority_9) {
  skip_priority_8_sound_provider_selection_ = skip_priority_8;
  enable_priority_9_sound_provider_selection_ = enable_priority_9;
  RefreshWorldReverbFromActiveSoundProvider();
}

std::int32_t SoundRuntime::SelectActiveSoundProviderPreferenceId() const {
  for (int priority = static_cast<int>(sound_provider_preference_ids_.size()) - 1; priority >= 0;
       --priority) {
    const std::int32_t id = sound_provider_preference_ids_[priority];
    if (id < 0) {
      continue;
    }
    if (priority == 9 && !enable_priority_9_sound_provider_selection_) {
      continue;
    }
    if (priority == 8 && skip_priority_8_sound_provider_selection_) {
      continue;
    }
    return id;
  }

  return 0;
}

void SoundRuntime::RefreshWorldReverbFromActiveSoundProvider() {
  if (!world_reverb_enabled_) {
    return;
  }

  const std::int32_t active_id = SelectActiveSoundProviderPreferenceId();
  if (active_id >= 0) {
    if (const auto *provider =
            GetSoundProviderPreferenceData(static_cast<std::uint32_t>(active_id));
        provider != nullptr) {
      SetWorldReverbProperties(BuildWorldReverbPropertiesFromSoundProvider(*provider));
      return;
    }
  }

  SetWorldReverbProperties(BuildGlueWorldReverbProperties());
}

void SoundRuntime::PushNonPositionalPlaybackBlock() {
  ++non_positional_playback_block_depth_;
}

void SoundRuntime::PopNonPositionalPlaybackBlock() {
  if (non_positional_playback_block_depth_ != 0) {
    --non_positional_playback_block_depth_;
  }
}

}
