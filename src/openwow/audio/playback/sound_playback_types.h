#pragma once
#include "openwow/audio/effects/impact_sounds.h"
#include "openwow/audio/effects/unit_sounds.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>
namespace openwow::audio {
class SoundRuntime;
enum class DspEffectType : std::uint32_t {
  kNone = 0,
  kLowPass = 1,
  kHighPass = 2,
  kEcho = 3,
  kParametricEQ = 4,
  kCompressor = 5,
  kVolume = 6,
};
[[nodiscard]] constexpr std::uint32_t
MapDspEffectToRuntimeType(DspEffectType type) {
  switch (type) {
  case DspEffectType::kLowPass:
    return 11;
  case DspEffectType::kHighPass:
    return 5;
  case DspEffectType::kEcho:
    return 3;
  case DspEffectType::kParametricEQ:
    return 12;
  case DspEffectType::kCompressor:
    return 6;
  default:
    return 0;
  }
}
struct TimeOfDaySoundWindow {
  static constexpr std::uint32_t kMillisecondsPerDay = 86400000u;
  std::uint32_t time_a_ms{0};
  std::uint32_t time_b_ms{0};
  std::uint32_t time_c_ms{0};
  std::uint32_t time_d_ms{0};
  std::int32_t day_offset_ms{0};
  [[nodiscard]] bool IsAlwaysActive() const {
    return time_a_ms == 0 && time_b_ms == 0 && time_c_ms == 0 && time_d_ms == 0;
  }
  [[nodiscard]] bool IsActiveAt(const std::uint32_t day_time_ms) const {
    if (IsAlwaysActive()) {
      return true;
    }
    std::int64_t adjusted_day_time = day_time_ms;
    const std::int64_t start_time = static_cast<std::int64_t>(day_offset_ms) + time_a_ms;
    const std::int64_t end_time = static_cast<std::int64_t>(day_offset_ms) + time_d_ms;
    if (end_time >= kMillisecondsPerDay && adjusted_day_time < start_time) {
      adjusted_day_time += kMillisecondsPerDay;
    }
    return start_time <= adjusted_day_time && adjusted_day_time <= end_time;
  }
  [[nodiscard]] float ComputeEnvelopeScaleAt(const std::uint32_t day_time_ms) const {
    if (IsAlwaysActive()) {
      return 1.0f;
    }
    std::int64_t adjusted_day_time = day_time_ms;
    const std::int64_t start_time = static_cast<std::int64_t>(day_offset_ms) + time_a_ms;
    const std::int64_t fade_in_end = static_cast<std::int64_t>(day_offset_ms) + time_b_ms;
    const std::int64_t fade_out_start = static_cast<std::int64_t>(day_offset_ms) + time_c_ms;
    const std::int64_t end_time = static_cast<std::int64_t>(day_offset_ms) + time_d_ms;
    if (end_time >= kMillisecondsPerDay && adjusted_day_time < start_time) {
      adjusted_day_time += kMillisecondsPerDay;
    }
    if (adjusted_day_time < fade_in_end) {
      const std::uint32_t fade_in_span = time_b_ms - time_a_ms;
      if (fade_in_span == 0) {
        return 1.0f;
      }
      const double fade_in_progress =
          (static_cast<double>(adjusted_day_time) - static_cast<double>(start_time)) /
          static_cast<double>(fade_in_span);
      return std::clamp(static_cast<float>(fade_in_progress), 0.0f, 1.0f);
    }
    if (adjusted_day_time >= fade_out_start) {
      const std::uint32_t fade_out_span = time_d_ms - time_c_ms;
      if (fade_out_span == 0) {
        return adjusted_day_time > end_time ? 0.0f : 1.0f;
      }
      const double fade_out_progress =
          1.0 - (static_cast<double>(adjusted_day_time) - static_cast<double>(fade_out_start)) /
                    static_cast<double>(fade_out_span);
      return std::clamp(static_cast<float>(fade_out_progress), 0.0f, 1.0f);
    }
    return 1.0f;
  }
};
struct AdvancedSoundEntryData {
  std::uint32_t id{0};
  float inner_radius_2d{0.0f};
  float outer_radius_2d{0.0f};
  TimeOfDaySoundWindow time_of_day_window{};
  std::uint32_t random_offset_ms{0};
  std::uint32_t usage{0};
  float inner_radius{0.0f};
  float outer_radius{0.0f};
  std::uint32_t time_interval_min_ms{0};
  std::uint32_t time_interval_max_ms{0};
  std::uint32_t volume_slider_category{0};
  float duck_to_sfx{1.0f};
  float duck_to_music{1.0f};
  float duck_to_ambience{1.0f};
  std::uint32_t duck_in_time_ms{1000};
  std::uint32_t duck_out_time_ms{1000};
};
struct SoundKitData {
  std::uint32_t id{0};
  std::string name;
  std::array<std::string, 10> file_paths{};
  std::array<std::uint32_t, 10> frequencies{};
  std::uint32_t file_count{0};
  float volume{1.0f};
  float min_distance{0.0f};
  float max_distance{40.0f};
  float eax_def{0.0f};

  std::uint32_t dbc_sound_type{0};
  std::uint32_t flags{0};
  std::uint32_t advanced_id{0};
  std::optional<AdvancedSoundEntryData> advanced{};
};
class EmotesTextSoundTable {
public:
  void Load(std::uint32_t max_emote_text_id);
  void Insert(std::uint32_t emote_text_id, std::uint32_t race_id, std::uint32_t gender_id,
              std::uint32_t sound_kit_id);
  [[nodiscard]] std::uint32_t Lookup(std::uint32_t emote_text_id, std::uint32_t race_id,
                                     std::uint32_t gender_id) const;
  [[nodiscard]] std::int32_t LookupAndSelectVariant(SoundRuntime& sound_runtime,
                                                     std::uint32_t emote_text_id,
                                                    std::uint32_t race_id,
                                                    std::uint32_t gender_id) const;
  [[nodiscard]] std::uint32_t MaxEmoteTextId() const {
    return max_emote_text_id_;
  }
  [[nodiscard]] std::uint32_t EntryCount() const {
    return entry_count_;
  }
private:
  static constexpr std::uint32_t kMaxRaces = 22;
  static constexpr std::uint32_t kMaxGenders = 2;
  static constexpr std::uint32_t kEntriesPerEmote = kMaxRaces * kMaxGenders;
  std::uint32_t max_emote_text_id_{0};
  std::uint32_t entry_count_{0};
  std::unordered_map<std::uint32_t, std::array<std::uint32_t, kEntriesPerEmote>> table_;
};
struct WoundDeathSoundTable {
  static constexpr int kSoundClasses = 3;
  static constexpr int kVariantsPerClass = 2;
  std::array<std::uint32_t, kSoundClasses * kVariantsPerClass> entries{};
  void Load(const std::vector<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>> &rows);
  [[nodiscard]] std::uint32_t Get(std::uint32_t sound_class, bool critical_variant) const;
};
struct DspParameterWrite {
  std::uint32_t index{0};
  float value{0.0f};
  [[nodiscard]] constexpr bool operator==(const DspParameterWrite &other) const = default;
};
struct DspFilterNode {
  DspEffectType type{DspEffectType::kNone};
  std::vector<float> params;
  std::vector<DspParameterWrite> applied_parameter_writes;
  bool bypassed{false};
  bool has_runtime_dsp{false};
  void *runtime_dsp{nullptr};
  DspFilterNode() = default;
  DspFilterNode(const DspEffectType effect_type, std::vector<float> effect_params,
                const bool initially_bypassed)
      : type(effect_type), params(std::move(effect_params)), bypassed(initially_bypassed),
        has_runtime_dsp(effect_type != DspEffectType::kVolume) {}
};
struct RelativeSoundVolumeState {
  float channel_volume{1.0f};
  float cached_base_volume{0.0f};
  float requested_scale{1.0f};
  float playback_scale{1.0f};
  float api_scale{1.0f};
  float advanced_scale{1.0f};
  float stop_scale{1.0f};
  void SetDirectVolume(float volume) {
    channel_volume = std::clamp(volume, 0.0f, 1.0f);
  }
  void ApplyRelativeScale(float scale) {
    api_scale = scale;
    RecomputeScaledVolume();
  }
  void SetPlaybackScale(float scale) {
    playback_scale = scale;
    RecomputeScaledVolume();
  }
  void SetAdvancedScale(float scale) {
    advanced_scale = scale;
    RecomputeScaledVolume();
  }
  void SetStopScale(float scale) {
    stop_scale = scale;
    RecomputeScaledVolume();
  }
private:
  void RecomputeScaledVolume() {
    requested_scale = playback_scale * api_scale * advanced_scale * stop_scale;
    if (cached_base_volume == 0.0f) {
      cached_base_volume = channel_volume;
    }
    channel_volume = cached_base_volume * requested_scale;
  }
};
struct SoundHandleStopState {
  bool active{false};
  std::uint32_t fade_total_ms{0};
  std::uint32_t fade_elapsed_ms{0};
  float start_scale{1.0f};
};

struct SoundHandleFadeInState {
  bool active{false};
  std::uint32_t fade_total_ms{0};
  std::uint32_t fade_elapsed_ms{0};
  float start_scale{0.0f};
};
struct SoundHandleVirtualPlayState {
  bool active{false};
  std::uint32_t inactive_elapsed_ms{0};
};
enum class SoundKitMaxAudibleBehavior : std::uint32_t {
  kMuteAndContinue = 0,
  kStealLowest = 1,
  kFailNew = 2,
};
struct SoundHandleBinding {
  std::uint32_t active_handle_id{0};
};
enum class SoundLoopMode : std::uint32_t {
  kUseSoundKit = 0,
  kForceLoop = 1,
  kForceOneShot = 2,
};
enum class SoundKitExclusivityMode : std::uint32_t {
  kUseSoundKit = 0,
  kDisableExclusiveRepeat = 1,
  kEnableExclusiveRepeat = 2,
};
enum class SoundKitVariationSelectionMode : std::uint32_t {
  kCycleWithoutImmediateRepeat = 0,
  kResetFrequenciesEachCall = 1,
  kConsumeFrequenciesAcrossCalls = 2,
};
[[nodiscard]] constexpr std::int32_t
ResolveDataPreloadQueueForSoundType(const std::uint32_t sound_type) {
  return sound_type > 0 && sound_type <= 2 ? 6 : 5;
}
struct SelectedSoundKitFile {
  std::uint32_t sound_kit_id{0};
  std::int32_t index{-1};
  std::string_view path;
};

inline constexpr std::uint32_t kDefaultPlaybackSoundType = 0u;

inline constexpr std::uint32_t kSelfUnitSoundPlaybackPriority = 110u;

inline constexpr std::uint32_t kErrorSpeechPlaybackSoundType = 7u;

inline constexpr std::uint32_t kSoundKitFlagExclusiveRepeat = 0x20u;
inline constexpr std::uint32_t kSoundKitFlagLoop = 0x200u;

inline constexpr std::uint32_t kSoundKitFlagRandomizeRate = 0x400u;
inline constexpr std::uint32_t kSoundKitFlagRandomizeVolume = 0x800u;

struct SoundKitPlaybackOptions {
  std::uint32_t sound_type{kDefaultPlaybackSoundType};
  std::optional<std::uint32_t> playback_priority{};
  std::string sound_model_override;
  float explicit_volume{-1.0f};
  float volume_scale{1.0f};
  SoundLoopMode loop_mode{SoundLoopMode::kUseSoundKit};
  SoundKitExclusivityMode exclusivity_mode{SoundKitExclusivityMode::kUseSoundKit};
  SoundKitVariationSelectionMode variation_mode{
      SoundKitVariationSelectionMode::kConsumeFrequenciesAcrossCalls};
  SoundKitMaxAudibleBehavior max_audible_behavior{
      SoundKitMaxAudibleBehavior::kStealLowest};
  float max_audible_mute_fade_speed{0.5f};

  float max_distance_override{-1.0f};

  float fade_in_seconds{0.0f};
  float fade_out_seconds{0.0f};
  std::optional<std::int32_t> forced_file_index{};
  std::optional<std::int32_t> preload_queue_hint{};
  bool allow_advanced_kit_properties{true};
  bool suppress_near_duplicate_advanced_instances{true};
  const float* cone_direction{nullptr};
  bool force_ambient_loop{false};
};

struct ManagedAdvancedSoundState {
  std::uint32_t sound_kit_id{0};
  std::uint32_t playback_mode{0};
  std::int32_t schedule_day_offset_ms{0};
  std::int32_t retrigger_countdown_ms{0};
  float time_of_day_scale{1.0f};
  SoundKitPlaybackOptions playback_options{};
};
struct SoundHandle {
  std::uint32_t handle_id{0};
  std::uint32_t sound_kit_id{0};
  std::uint32_t sound_type{0};
  std::optional<std::uint32_t> playback_priority{};
  SoundKitMaxAudibleBehavior max_audible_behavior{
      SoundKitMaxAudibleBehavior::kStealLowest};
  float max_audible_mute_fade_speed{0.5f};
  std::optional<std::uint64_t> bound_object_guid{};
  std::int32_t selected_file_index{-1};
  std::string sound_model_override;

  std::optional<ManagedAdvancedSoundState> managed_advanced{};
  bool has_active_sound{false};
  bool is_playing{false};
  bool loops{false};
  bool bypass_virtual_play_window{false};
  float min_distance{0.0f};
  float max_distance{0.0f};
  float position[3]{0.0f, 0.0f, 0.0f};
  bool has_position{false};
  bool tracks_instance_limit{false};
  bool tracks_exclusive_kit{false};
  std::optional<std::uint32_t> audio_engine_handle_id{};

  std::optional<float> engine_pushed_channel_volume{};
  std::string source_label;
  std::string selected_file_path;
  RelativeSoundVolumeState relative_volume{};

  float fade_in_seconds{0.0f};
  float fade_out_seconds{0.0f};
  SoundHandleStopState stop_state{};
  SoundHandleFadeInState fade_in_state{};
  SoundHandleVirtualPlayState virtual_play_state{};
};

struct SoundKitPlaybackVolume {
  float direct_volume{1.0f};
  float playback_scale{1.0f};

  [[nodiscard]] float effective_volume() const {
    return std::clamp(direct_volume, 0.0f, 1.0f) * playback_scale;
  }
};
struct ActiveAdvancedKitProperty {
  std::optional<std::uint32_t> owner_handle_id{};
  std::array<float, 3> current_attenuation{1.0f, 1.0f, 1.0f};
  float distance_to_listener{0.0f};
  bool active{false};
  std::uint32_t fade_out_time_ms{1000};
};

struct PlayMusicRuntimeState {
  std::int32_t requested_sound_kit_id{-1};
  std::int32_t latched_sound_kit_id{-1};
  std::array<std::int32_t, 4> playback_state_words{};
  std::array<std::int32_t, 3> marker_sound_kit_ids{{-1, -1, -1}};

  void Reset() {
    requested_sound_kit_id = -1;
    latched_sound_kit_id = -1;
    playback_state_words.fill(0);
    marker_sound_kit_ids.fill(-1);
  }

  void Arm(std::int32_t sound_kit_id) {
    Reset();
    requested_sound_kit_id = sound_kit_id;
    latched_sound_kit_id = sound_kit_id;
  }
};

struct ChaosModeFrameStats {
  std::uint32_t play_2d_attempts{0};
  std::uint32_t play_3d_attempts{0};
  std::uint32_t immediate_stops{0};
  std::uint32_t persistent_loop_starts{0};
  std::uint32_t script_file_attempts{0};
  bool restart_requested{false};
  bool restart_performed{false};
};
}
