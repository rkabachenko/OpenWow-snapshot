#pragma once
struct AudioChannel {

  std::shared_ptr<const SoundData> data;
  std::shared_ptr<MusicPcmStream> pcm_stream;
  std::size_t position{0};
  float volume{1.0f};
  bool loop{false};
  bool active{false};
  bool paused{false};
  bool uses_playback_channel{false};
  PlaybackChannel playback_channel{PlaybackChannel::SFX};
  bool is_3d{false};
  float x{0}, y{0}, z{0};
  float min_distance{1.0f};
  float max_distance{50.0f};
  float computed_volume{1.0f};
  float computed_pan{0.0f};
  static constexpr std::size_t kHrtfDelayCapacity = 64u;
  std::array<float, kHrtfDelayCapacity> hrtf_left_delay{};
  std::array<float, kHrtfDelayCapacity> hrtf_right_delay{};
  std::size_t hrtf_delay_cursor{0u};
  float hrtf_left_filter{0.0f};
  float hrtf_right_filter{0.0f};
};
struct ExternalVoiceStream {
  static constexpr std::size_t kRingSampleCapacity = 65536u;

  std::vector<std::int16_t> samples;
  std::size_t read_cursor{0};
  std::size_t write_cursor{0};
  std::size_t sample_count{0};
  float volume{1.0f};
};
struct MusicFade {
  bool active{false};
  bool fading_in{false};
  float duration_ms{0};
  float elapsed_ms{0};
  float start_volume{0};
  float end_volume{0};
};
class AudioEngineState {
protected:
  ~AudioEngineState();
  std::uint32_t device_{0};
  std::uint32_t voice_output_device_{0};
  int output_rate_{44100};
  int output_channels_{2};
  int output_buffer_size_{1024};
  std::string output_device_name_;
  bool initialized_{false};
  std::vector<std::int32_t> callback_mix_buffer_;
  std::vector<std::int32_t> voice_callback_mix_buffer_;
  int voice_output_rate_{44100};
  int voice_output_channels_{2};
  AudioChannel music_;
  std::unique_ptr<MusicPcmStream> music_stream_;
  AudioChannel ambience_;
  AudioChannel credits_;
  std::unique_ptr<MusicPcmStream> credits_stream_;
  std::array<AudioChannel, kSfxChannelCapacity> sfx_{};
  int max_sfx_channels_{kMaxSfxChannels};
  std::atomic<float> master_volume_{1.0f};
  std::atomic<float> music_volume_{0.5f};
  std::atomic<float> sfx_volume_{1.0f};
  std::atomic<float> ambience_volume_{0.5f};
  std::atomic<bool>  muted_{false};
  std::atomic<bool> software_hrtf_enabled_{false};
  MusicFade music_fade_;
  MusicFade ambience_fade_;
  MusicFade credits_fade_;
  std::array<MusicFade, kSfxChannelCapacity> sfx_fades_{};
  float listener_x_{0}, listener_y_{0}, listener_z_{0};
  float listener_fx_{0}, listener_fy_{0}, listener_fz_{1.0f};
  float listener_vx_{0}, listener_vy_{0}, listener_vz_{0};
  float listener_ux_{0}, listener_uy_{0}, listener_uz_{1.0f};
  VfsLoader vfs_loader_;
  std::mutex cache_mutex_;

  std::unordered_map<std::string, std::shared_ptr<const SoundData>> cache_;
  int cached_output_rate_{0};
  int cached_output_channels_{0};

  std::unique_ptr<openwow::core::ThreadPoolSystem> decode_workers_;
  std::shared_ptr<IMovieAudioSource> movie_audio_stream_;
  std::size_t movie_sample_pos_{0};
  int movie_sample_rate_{44100};
  int movie_channels_{2};
  float movie_volume_{1.0f};
  bool movie_audio_playing_{false};
  FreeverbEngine world_reverb_engine_{};
  bool world_reverb_initialized_{false};
  std::atomic<bool> world_reverb_enabled_{false};
  float world_reverb_room_size_{0.5f};
  float world_reverb_damping_{0.5f};
  float world_reverb_wet_{0.0f};
  float world_reverb_dry_{0.5f};
  float world_reverb_width_{1.0f};
  std::vector<float> world_reverb_input_;
  std::vector<float> world_reverb_output_;
  struct HandleEntry {
      std::uint32_t       handleId{0};
      AudioClipInfo       clip;
      AudioPlaybackState  state{AudioPlaybackState::Stopped};
      float               currentVolume{1.0f};
      float               position{0.0f};
      float               fadeStart{0.0f};
      float               fadeTarget{0.0f};
      float               fadeDuration{0.0f};
      float               fadeElapsed{0.0f};
      int                 sdlChannel{-1};
      std::uint64_t       sdlGeneration{0};
      int                 priority{0};
  };
  std::uint32_t nextHandleId_{1};
  std::uint64_t nextDirectPlaybackGeneration_{1};
  std::array<std::uint64_t, kSfxChannelCapacity> directPlaybackGenerations_{};
  std::unordered_map<std::uint32_t, HandleEntry> handles_;

  static constexpr std::uint32_t kNoOwningHandleId = 0u;
  std::array<std::uint32_t, kSfxChannelCapacity> handleIdByChannel_{};
  std::array<float, kPlaybackChannelCount> playbackChannelVolumes_{
      {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f}};
  std::array<bool, kPlaybackChannelCount> playbackChannelMuted_{};
  static constexpr std::size_t kExternalVoiceStreamCount = 16u;
  std::array<ExternalVoiceStream, kExternalVoiceStreamCount> external_voice_streams_{};
};
