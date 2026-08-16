#pragma once
namespace openwow::audio {
class SoundAssetRuntimeState {
protected:
  struct DspFilterChain {
    std::vector<DspFilterNode> nodes;
    std::vector<DspParameterWrite> output_parameter_writes;
    void *output_dsp{nullptr};
    SoundHandleBinding output_binding{};
    float output_volume_scale{1.0f};
    float activation_delay_seconds{0.1f};
  };
  struct DualSlotSoundPlayer {
    struct Slot {
      std::uint32_t sound_kit_id{0};
      int zone_sound_type{0};
      SoundHandleBinding handle_binding{};
      bool is_playing_override{false};
      bool follows_handle{false};
    };
    std::array<Slot, 2> slots{};
    int active_index{0};
    int prev_index{1};
    void SwapSlots() {
      const int old = active_index;
      prev_index = old;
      active_index = 1 - old;
    }
    Slot &ActiveSlot() { return slots[static_cast<std::size_t>(active_index)]; }
    [[nodiscard]] const Slot &ActiveSlot() const {
      return slots[static_cast<std::size_t>(active_index)];
    }
    Slot &PrevSlot() { return slots[static_cast<std::size_t>(prev_index)]; }
    [[nodiscard]] const Slot &PrevSlot() const {
      return slots[static_cast<std::size_t>(prev_index)];
    }
  };
  enum class ZoneMusicStopActionKind : std::uint8_t {
    kNone = 0,
    kSelectionDelayGate,
    kResetRuntimeOnStop,
    kSpecialRepeatDeadline,
  };
  struct ZoneMusicStopAction {
    ZoneMusicStopActionKind kind{ZoneMusicStopActionKind::kNone};
    std::int32_t expected_sound_kit_id{0};
    std::uint32_t repeat_delay_min_ms{0};
    std::uint32_t repeat_delay_max_ms{0};
  };
  std::vector<SoundKitData *> sound_kit_index_;
  std::vector<SoundKitData> sound_kit_storage_;
  struct SoundKitPlaybackRuntimeState {
    std::array<std::uint32_t, 10> remaining_frequencies{};
    std::int32_t last_selected_index{-1};
    std::int32_t pending_preload_index{-1};
    bool hold_last_selected_file{false};
  };
  std::vector<SoundKitPlaybackRuntimeState> sound_kit_playback_runtime_;
  std::unordered_map<std::uint32_t, std::vector<std::size_t>> sound_kit_name_hash_index_;
  std::uint32_t max_sound_kit_id_{0};
  std::vector<std::optional<SoundAmbienceTableEntryData>> sound_ambience_index_;
  std::unordered_map<std::uint32_t, AdvancedSoundEntryData> advanced_sound_entries_;
  std::vector<WorldStateZoneSoundEntryData> world_state_zone_sounds_;
  struct ChunkAudioLookupTable {
    struct KeyHash {
      [[nodiscard]] std::size_t operator()(const ChunkAudioBindingKey &key) const {
        std::size_t seed = std::hash<std::uint32_t>{}(key.map_id);
        seed ^= std::hash<std::uint32_t>{}(key.tile_y) + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
        seed ^= std::hash<std::uint32_t>{}(key.tile_x) + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
        seed ^= std::hash<std::uint32_t>{}(key.chunk_y) + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
        seed ^= std::hash<std::uint32_t>{}(key.chunk_x) + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
        return seed;
      }
    };
    void Reset();
    void Reserve(std::size_t entry_count);
    void Upsert(const ChunkAudioBindingEntry &entry);
    [[nodiscard]] bool Lookup(const ChunkAudioBindingKey &key, ChunkAudioBindingValue *out) const;
    [[nodiscard]] std::size_t size() const { return bindings_.size(); }
    openwow::core::CMapTable table_;
  private:
    std::unordered_map<ChunkAudioBindingKey, ChunkAudioBindingValue, KeyHash> bindings_;
  };
  ChunkAudioLookupTable chunk_audio_bindings_;
  std::unordered_map<std::uint32_t, SoundProviderPreferenceData> sound_provider_preferences_;
  std::array<std::int32_t, 11> sound_provider_preference_ids_{};
  bool skip_priority_8_sound_provider_selection_{false};
  bool enable_priority_9_sound_provider_selection_{false};
  std::int32_t indoor_sound_area_id_{0};
  bool indoor_sound_area_changed_{false};
  struct ZoneAmbienceSelectionEntry {
    std::array<std::int32_t, 2> sound_kit_ids{{-1, -1}};
  };
  std::array<ZoneAmbienceSelectionEntry, 11> zone_ambience_selection_entries_{};
  struct ZoneMusicSelectionEntry {
    std::array<std::int32_t, 2> sound_kit_ids{{-1, -1}};
    std::array<std::uint32_t, 2> repeat_delay_min_ms{};
    std::array<std::uint32_t, 2> repeat_delay_max_ms{};
  };
  std::array<ZoneMusicSelectionEntry, 11> zone_music_selection_entries_{};
  std::uint32_t zone_music_delay_deadline_ms_{0};
  DualSlotSoundPlayer zone_ambience_player_{};
  DualSlotSoundPlayer zone_music_player_{};
  ZoneMusicStopAction zone_music_stop_action_{};
  PlayMusicRuntimeState play_music_runtime_{};
  EmotesTextSoundTable emotes_text_sound_;
  std::vector<std::pair<std::string, std::vector<DspFilterNode>>> dsp_filter_definitions_;
  std::unordered_map<std::string, DspFilterChain> dsp_filters_;
  std::unordered_map<std::uint32_t, ZoneMusicTableEntryData> zone_music_;
  std::unordered_map<std::uint32_t, ZoneIntroMusicTableEntryData> zone_intro_music_;
  WoundDeathSoundTable wound_death_sound_table_;
  WeaponImpactSoundTable weapon_impact_sounds_;
  UnitSoundKitLookup unit_sound_kit_lookup_;
  bool advanced_kit_property_manager_initialized_{false};
  std::vector<ActiveAdvancedKitProperty> active_advanced_kit_properties_;
  bool zone_music_repeat_delay_state_initialized_{false};
  std::vector<std::uint32_t> zone_music_repeat_delay_deadlines_ms_;
  bool zone_music_delay_rng_initialized_{false};
  openwow::foundation::hashing::AdlerSeedState zone_music_delay_rng_{};
};
}
