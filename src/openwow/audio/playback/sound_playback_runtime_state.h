#pragma once
#include "openwow/audio/playback/sound_handle_table.h"
namespace openwow::audio {
class SoundPlaybackRuntimeState {
protected:
  SoundPlaybackRuntimeState() : liquid_query_result_buffer_(1) {}
  std::uint32_t non_positional_playback_block_depth_{0};
  std::uint32_t next_handle_{1};
  SoundHandleTable active_handles_;
  std::unordered_map<std::uint32_t, SoundHandleBinding *> active_handle_bindings_;
  std::array<std::uint32_t, 18> active_sound_type_counts_{};
  std::unordered_set<std::uint32_t> active_exclusive_sound_kits_;
  std::optional<std::uint32_t> script_music_handle_id_{};
  std::uint32_t error_speech_handle_id_{0};
  int ambient_idle_selection_counter_{0};
  std::uint32_t ambient_idle_handle_id_{0};
  bool ambient_idle_handle_valid_{false};
  std::uint64_t ambient_idle_last_guid_{0};
  bool script_music_playing_{false};
  struct BackgroundDisplayChannelState {
    std::uint32_t slot{0};
    bool has_selection{false};
  } background_sound_state_{};
  struct SuspendedVoiceSessionSelection {
    std::uint32_t session_type_code{4};
    std::string session_name;
  };
  std::optional<SuspendedVoiceSessionSelection> suspended_background_display_channel_{};
  std::uint32_t cinematic_sound_handle_{0};
  bool listener_at_character_{false};
  CvarSetCallback cvar_set_cb_;
  CvarGetBoolCallback cvar_get_bool_cb_;
  CvarGetIntCallback cvar_get_int_cb_;
  ChannelMuteCallback channel_mute_cb_;
  bool sound_engine_initialized_{false};
  int update_time_ms_{0};
  int last_update_time_ms_{0};
  std::uint32_t voice_chat_toggle_lookup_flags_{0};
  std::uint32_t voice_chat_on_kit_{0};
  std::uint32_t voice_chat_off_kit_{0};
  int time_of_day_index_{0};
  std::int32_t chunk_audio_cached_chunk_x_{-1};
  std::int32_t chunk_audio_cached_chunk_y_{-1};
  openwow::foundation::hashing::AdlerSeedState random_seed_{};
  ActivePlayerPositionCallback active_player_position_cb_;
  ObjectPositionCallback object_position_cb_;
  UnitLookupCallback unit_lookup_cb_;
  PlayerLookupCallback player_lookup_cb_;
  ActivePlayerCallback active_player_cb_;
  NormalizedTimeOfDayCallback normalized_time_of_day_cb_;
  LiquidQueryCallback liquid_query_cb_;
  double liquid_query_elapsed_ms_{0.0};
  float liquid_query_position_[3]{0.0f, 0.0f, 0.0f};
  bool liquid_query_position_initialized_{false};
  std::unordered_map<std::uint32_t, LiquidTypeSoundData> liquid_type_sound_data_;
  LiquidQueryResultBuffer liquid_query_result_buffer_;
  struct ActiveLiquidAmbienceState {
    std::uint32_t sound_kit_id{0};
    std::array<float, 3> position{0.0f, 0.0f, 0.0f};
    std::optional<std::uint32_t> handle_id{};
    bool stop_pending{false};
  } liquid_ambience_{};
  WorldReverbProperties world_reverb_{};
  WorldReverbRoomLfDspState world_reverb_room_lf_dsp_state_{};
  bool world_reverb_enabled_{false};
  std::uint32_t sound_enable_reverb_cvar_callback_handle_{0};
  std::uint32_t sound_enable_software_hrtf_cvar_callback_handle_{0};
  std::uint32_t sound_voice_input_driver_index_cvar_callback_handle_{0};
  std::uint32_t sound_voice_output_driver_index_cvar_callback_handle_{0};
  std::uint32_t sound_output_driver_index_cvar_callback_handle_{0};
  std::uint32_t outbound_chat_volume_cvar_callback_handle_{0};
  std::uint32_t chaos_mode_cvar_callback_handle_{0};
  struct ChaosRuntimeState {
    std::uint64_t interval_elapsed_ms{0};
    double burst_elapsed_seconds{0.0};
    double restart_elapsed_seconds{0.0};
    std::uint32_t last_tick_ms{0};
    bool tick_initialized{false};
    std::array<std::uint32_t, 80> persistent_loop_handles{};
    void Reset() {
      interval_elapsed_ms = 0;
      burst_elapsed_seconds = 0.0;
      restart_elapsed_seconds = 0.0;
      last_tick_ms = 0;
      tick_initialized = false;
      persistent_loop_handles.fill(0);
    }
  } chaos_runtime_{};
  ChaosModeFrameStats last_chaos_mode_frame_stats_{};
};
}
