#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
namespace openwow::audio {
struct WorldAudioBindingValue {
  std::int32_t sound_ambience_id{-1};
  std::int32_t zone_music_id{-1};
  std::int32_t zone_intro_music_id{-1};
  std::int32_t sound_provider_preferences_id{-1};
};
struct SoundAmbienceTableEntryData {
  std::uint32_t id{0};
  std::array<std::int32_t, 2> sound_kit_ids{{-1, -1}};
};
struct ZoneMusicTableEntryData {
  std::uint32_t id{0};
  std::array<std::int32_t, 2> sound_kit_ids{{-1, -1}};
  std::array<std::uint32_t, 2> repeat_delay_min_ms{};
  std::array<std::uint32_t, 2> repeat_delay_max_ms{};
};
struct ZoneIntroMusicTableEntryData {
  std::uint32_t id{0};
  std::int32_t sound_kit_id{-1};
  std::uint32_t priority{0};
  std::uint32_t min_delay_ms{0};
};
struct LiquidQueryResultEntry {
  float distance_squared{-1.0f};
  std::array<float, 3> position{0.0f, 0.0f, 0.0f};
  std::uint32_t sound_kit_id{0};
  [[nodiscard]] bool IsEmpty() const {
    return distance_squared < 0.0f;
  }
};
class LiquidQueryResultBuffer {
public:
  explicit LiquidQueryResultBuffer(std::size_t capacity);
  void Reset();
  void ShiftDownFrom(std::size_t insert_index);
  [[nodiscard]] bool InsertSorted(const LiquidQueryResultEntry &entry);
  [[nodiscard]] bool InsertSorted(float distance_squared, float x, float y, float z,
                                  std::uint32_t sound_kit_id);
  [[nodiscard]] std::size_t size() const {
    return entries_.size();
  }
  [[nodiscard]] const LiquidQueryResultEntry &operator[](const std::size_t index) const {
    return entries_[index];
  }
private:
  std::vector<LiquidQueryResultEntry> entries_;
};
struct LiquidTypeSoundData {
  std::uint32_t liquid_type_id{0};
  std::uint32_t flags{0};
  std::uint32_t sound_kit_id{0};
};
struct LiquidQueryWorldEntry {
  std::uint32_t liquid_type_id{0};
  std::array<float, 3> relative_offset{0.0f, 0.0f, 0.0f};
};
struct LiquidQueryWorldSnapshot {
  std::vector<LiquidQueryWorldEntry> entries;
  bool suppress_flagged_types{false};
};
struct WorldReverbProperties {
  std::int32_t environment{0};
  std::int32_t room_flags{-1};
  float environment_size{7.5f};
  float environment_diffusion{1.0f};
  std::int32_t room{-10000};
  std::int32_t room_hf{-10000};
  std::int32_t room_lf{0};
  float decay_time{1.0f};
  float decay_hf_ratio{1.0f};
  float decay_lf_ratio{1.0f};
  std::int32_t reflections{-2602};
  float reflections_delay{0.0070000002f};
  std::array<float, 3> reflections_pan{0.0f, 0.0f, 0.0f};
  std::int32_t reverb{200};
  float reverb_delay{0.011f};
  std::array<float, 3> reverb_pan{0.0f, 0.0f, 0.0f};
  float echo_time{0.25f};
  float echo_depth{0.0f};
  float modulation_time{0.25f};
  float modulation_depth{0.0f};
  float air_absorption_hf{-5.0f};
  float hf_reference{5000.0f};
  float lf_reference{250.0f};
  float room_rolloff_factor{0.0f};
  float engine_scalar_28{0.0f};
  float engine_scalar_29{0.0f};
  std::uint32_t flags{831};
};
struct BiquadFilterCoefficients {
  float b0{1.0f};
  float b1{0.0f};
  float b2{0.0f};
  float a1{0.0f};
  float a2{0.0f};
};
struct WorldReverbRoomLfDspState {
  std::int32_t clamped_room_lf{0};
  float clamped_lf_reference{250.0f};
  float room_lf_gain_db{0.0f};
  BiquadFilterCoefficients low_shelf{};
};
struct ChunkAudioBindingKey {
  std::uint32_t map_id{0};
  std::uint32_t tile_y{0};
  std::uint32_t tile_x{0};
  std::uint32_t chunk_y{0};
  std::uint32_t chunk_x{0};
  [[nodiscard]] constexpr bool operator==(const ChunkAudioBindingKey &other) const {
    return map_id == other.map_id && tile_y == other.tile_y && tile_x == other.tile_x &&
           chunk_y == other.chunk_y && chunk_x == other.chunk_x;
  }
};
using ChunkAudioBindingValue = WorldAudioBindingValue;
struct ChunkAudioBindingEntry {
  ChunkAudioBindingKey key{};
  ChunkAudioBindingValue value{};
};
[[nodiscard]] std::uint32_t ComputeChunkAudioBindingLookupHash(const ChunkAudioBindingKey &key);
struct WorldStateZoneSoundEntryData {
  std::uint32_t world_state_id{0};
  std::uint32_t world_state_value{0};
  std::uint32_t area_id{0};
  std::uint32_t wmo_area_id{0};
  WorldAudioBindingValue value{};
};
struct SoundProviderPreferenceData {
  std::uint32_t id{0};
  std::string description{};
  std::uint32_t flags{0};
  std::int32_t room_flags{-1};
  float decay_time{1.0f};
  float environment_size{7.5f};
  float environment_diffusion{1.0f};
  std::int32_t room{-10000};
  std::int32_t room_hf{-10000};
  float decay_hf_ratio{1.0f};
  std::int32_t reflections{-2602};
  float reflections_delay{0.0070000002f};
  std::int32_t reverb{200};
  float reverb_delay{0.011f};
  float room_rolloff_factor{0.0f};
  float air_absorption_hf{-5.0f};
  std::int32_t room_lf{0};
  float decay_lf_ratio{1.0f};
  float echo_time{0.25f};
  float echo_depth{0.0f};
  float modulation_time{0.25f};
  float modulation_depth{0.0f};
  float hf_reference{5000.0f};
  float lf_reference{250.0f};
};
}
