#pragma once
#include "openwow/audio/adapters/sdl/music_pcm_stream.h"
#include "openwow/audio/dsp/freeverb_engine.h"
#include "openwow/audio/resources/sound_data.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
namespace openwow::core {
class ThreadPoolSystem;
}
namespace openwow::audio {
class IAudioDecoder;
class IMovieAudioSource;
constexpr int kMaxSfxChannels = 32;
constexpr int kSfxChannelCapacity = 128;
constexpr int kMusicChannel    = -1;
constexpr int kAmbienceChannel = -2;
constexpr int kCreditsChannel  = -3;
constexpr float k3DDopplerScale    = 1.0f;
constexpr float k3DDistanceFactor  = 1.0f;
constexpr float k3DRolloffScale    = 4.0f;
constexpr int kMinSoftwareChannels = 12;
constexpr int kMaxSoftwareChannels = 128;
constexpr int kOutputQualityRates[] = {22050, 44100, 48000};
enum class PlaybackChannel : std::uint8_t {
    Music    = 0,
    Ambience = 1,
    SFX      = 2,
    Dialog   = 3,
    Master   = 4,
    Movie    = 5,
    Voice    = 6,
};
constexpr int kPlaybackChannelCount = 7;
struct MusicStreamStats {
  std::size_t buffered_frames{0};
  std::size_t capacity_frames{0};
  std::uint64_t underflow_count{0};
};
enum class AudioPlaybackState : std::uint8_t {
    Stopped   = 0,
    Playing   = 1,
    Paused    = 2,
    FadingIn  = 3,
    FadingOut = 4,
};
struct AudioClipInfo {
    std::uint32_t       clipId     = 0;
    std::string         path;
    PlaybackChannel     channel    = PlaybackChannel::SFX;
    float               volume     = 1.0f;
    bool                loop       = false;
    float               fadeInTime  = 0.0f;
    float               fadeOutTime = 0.0f;
    float               duration    = 0.0f;
    int                 priority    = -1;
    std::string         soundModelOverride;
};
struct AudioPlaybackHandle {
    std::uint32_t handleId = 0;
    bool operator==(const AudioPlaybackHandle& o) const { return handleId == o.handleId; }
    bool operator!=(const AudioPlaybackHandle& o) const { return handleId != o.handleId; }
};
struct AudioDirectPlaybackHandle {
  int channel{-1};
  std::uint64_t generation{0};
  [[nodiscard]] bool IsValid() const {
    return channel >= 0 && generation != 0;
  }
};
struct ActiveHandleInfo {
    std::uint32_t       handleId{0};
    AudioPlaybackState  state{AudioPlaybackState::Stopped};
    float               volume{0.0f};
    bool                is_3d{false};
    bool                fading_in{false};
    bool                fading_out{false};
    bool                is_virtual{false};
    float               pos_x{0}, pos_y{0}, pos_z{0};
    float               min_dist{0.0f};
    float               max_dist{50.0f};
    float               frequency{44100.0f};
    std::uint32_t       creation_flags{0};
    float               distance_sq{0.0f};
};
using VfsLoader = std::function<std::optional<std::vector<std::uint8_t>>(const std::string&)>;
#include "openwow/audio/playback/audio_engine_state.h"
class AudioEngine : private AudioEngineState {
 public:

  AudioEngine();
  ~AudioEngine();
  AudioEngine(const AudioEngine&) = delete;
  AudioEngine& operator=(const AudioEngine&) = delete;
  bool Initialize(int sample_rate = 44100, int channels = 2,
                  int buffer_size = 1024,
                  std::string_view output_device_name = {});
  void Shutdown();
  bool ReopenOutputDevicePreservingMovieAudio(
      std::string_view output_device_name);
  bool OpenVoiceOutputDevice(std::string_view output_device_name);
  void CloseVoiceOutputDevice();
  void SetMaxSfxChannels(int max_channels);
  [[nodiscard]] bool IsInitialized() const;
  [[nodiscard]] int GetOutputRate() const { return output_rate_; }
  [[nodiscard]] int GetOutputChannels() const { return output_channels_; }
  bool PlayMusicFromBytes(const std::vector<std::uint8_t>& raw_wav,
                          bool loop = true,
                          float fade_in_ms = 0.0f,
                          float volume = 1.0f);
  bool PlayMusic(const std::string& path, bool loop = true,
                 float fade_in_ms = 0.0f, float volume = 1.0f);
  void StopMusic(float fade_out_ms = 0.0f);
  void SetMusicVolume(float volume);
  [[nodiscard]] bool IsMusicPlaying() const;
  [[nodiscard]] std::optional<MusicStreamStats> GetMusicStreamStats() const;
  int PlaySoundFromBytes(const std::vector<std::uint8_t>& raw_wav,
                         float volume = 1.0f, bool loop = false,
                         std::optional<PlaybackChannel> playback_channel = std::nullopt);
  AudioDirectPlaybackHandle PlaySoundFromBytesTracked(
      const std::vector<std::uint8_t>& raw_wav, float volume = 1.0f,
      bool loop = false,
      std::optional<PlaybackChannel> playback_channel = std::nullopt);
  int PlaySound(const std::string& path, float volume = 1.0f, bool loop = false,
                int priority = -1,
                std::optional<PlaybackChannel> playback_channel = std::nullopt);
  int PlaySound3DFromBytes(const std::vector<std::uint8_t>& raw_wav,
                           float x, float y, float z,
                           float volume = 1.0f, float max_distance = 50.0f);
  int PlaySound3D(const std::string& path,
                  float x, float y, float z,
                  float volume = 1.0f, float max_distance = 50.0f);
  void StopSound(int channel, float fade_out_ms = 0.0f);
  void StopSound(AudioDirectPlaybackHandle handle, float fade_out_ms = 0.0f);
  [[nodiscard]] bool IsSoundChannelActive(int channel) const;
  [[nodiscard]] bool IsSoundChannelActive(AudioDirectPlaybackHandle handle) const;
  void StopAllSounds(float fade_out_ms = 0.0f);
  void StopNonVoiceSounds(float fade_out_ms = 0.0f);
  void SetSoundVolume(float volume);
  bool PlayAmbienceFromBytes(const std::vector<std::uint8_t>& raw_wav,
                             float volume = 1.0f, float fade_in_ms = 0.0f);
  bool PlayAmbience(const std::string& path, float volume = 1.0f, float fade_in_ms = 0.0f);
  void StopAmbience(float fade_out_ms = 0.0f);
  void SetAmbienceVolume(float volume);
  [[nodiscard]] bool IsAmbiencePlaying() const;
  bool PlayCreditsFromBytes(const std::vector<std::uint8_t>& raw_wav,
                            bool loop = false, float fade_in_ms = 0.0f,
                            float volume = 1.0f);
  bool PlayCredits(const std::string& path, bool loop = false,
                   float fade_in_ms = 0.0f, float volume = 1.0f);
  void StopCredits(float fade_out_ms = 0.0f);
  [[nodiscard]] bool IsCreditsPlaying() const;
  void SetMasterVolume(float volume);
  void SetListenerPosition(float x, float y, float z);
  void SetListenerOrientation(float fx, float fy, float fz);
  void SetListener3DAttributes(const float* position, const float* velocity,
                               const float* forward, const float* up);
  void Update(float delta_ms = 16.0f);
  void SetMuted(bool muted);
  [[nodiscard]] bool IsMuted() const;
  void SetWorldReverbEnabled(bool enabled);
  void SetSoftwareHrtfEnabled(bool enabled) {
    software_hrtf_enabled_.store(enabled, std::memory_order_relaxed);
  }
  void ConfigureWorldReverb(float room_size, float damping, float wet, float dry, float width);
  void PlayMovieAudioStream(std::shared_ptr<IMovieAudioSource> source,
                            float volume);
  void StopMovieAudio();
  [[nodiscard]] bool IsMovieAudioPlaying() const;
  [[nodiscard]] double MovieAudioTimeSeconds() const;
  bool QueueVoicePcm(std::uint32_t stream_index,
                     const std::int16_t *samples,
                     std::size_t sample_count,
                     int sample_rate,
                     int channels,
                     float volume,
                     double playback_rate = 1.0);
  void SetVoicePcmStreamVolume(std::uint32_t stream_index, float volume);
  void ResetVoicePcmStream(std::uint32_t stream_index);
  void ResetAllVoicePcmStreams();
  [[nodiscard]] std::size_t
  GetQueuedVoicePcmSampleCount(std::uint32_t stream_index) const;
  struct MovieAudioStateSnapshot {
    std::shared_ptr<IMovieAudioSource> stream;
    std::size_t sample_pos{0};
    int sample_rate{44100};
    int channels{2};
    float volume{1.0f};
    bool playing{false};
  };
  [[nodiscard]] std::optional<MovieAudioStateSnapshot> CaptureMovieAudioState() const;
  void RestoreMovieAudioState(const MovieAudioStateSnapshot& state);
  void SetVfsLoader(VfsLoader loader);
  void Precache(const std::string& path);
  void ClearCache();
  AudioPlaybackHandle Play(const AudioClipInfo& clip);
  void Stop(AudioPlaybackHandle handle);
  void Pause(AudioPlaybackHandle handle);
  void Resume(AudioPlaybackHandle handle);
  void FadeIn(AudioPlaybackHandle handle, float seconds);
  void FadeOut(AudioPlaybackHandle handle, float seconds);
  void  SetHandleVolume(AudioPlaybackHandle handle, float volume);
  [[nodiscard]] float GetHandleVolume(AudioPlaybackHandle handle) const;
  [[nodiscard]] AudioPlaybackState GetState(AudioPlaybackHandle handle) const;
  [[nodiscard]] bool IsHandlePlaying(AudioPlaybackHandle handle) const;
  [[nodiscard]] bool IsHandleMaterialized(AudioPlaybackHandle handle) const;
  [[nodiscard]] bool IsHandleChannelActive(AudioPlaybackHandle handle) const;

  void QueryHandleChannelsActive(const std::vector<AudioPlaybackHandle>& handles,
                                 std::vector<bool>& active_out) const;
  void DestroyHandle(AudioPlaybackHandle handle);
  void  SetPlaybackChannelVolume(PlaybackChannel ch, float volume);
  [[nodiscard]] float GetPlaybackChannelVolume(PlaybackChannel ch) const;
  void SetPlaybackChannelMuted(PlaybackChannel ch, bool muted);
  [[nodiscard]] bool IsPlaybackChannelMuted(PlaybackChannel ch) const;
  [[nodiscard]] float GetMasterVolume() const;
  [[nodiscard]] std::size_t GetActivePlaybackCount() const;
  [[nodiscard]] std::size_t GetActiveNonVoicePlaybackCount() const;
  [[nodiscard]] std::vector<ActiveHandleInfo> GetActiveHandlesInfo() const;
  void StopAllHandles();
  void StopHandleChannel(PlaybackChannel ch, float fade_seconds = 0.0f);
  void SetSound3DPosition(AudioPlaybackHandle handle, float x, float y, float z);
  void SetHandleDistanceRange(AudioPlaybackHandle handle, float min_dist, float max_dist);
  [[nodiscard]] float GetPlaybackPosition(AudioPlaybackHandle handle) const;
  static constexpr std::size_t kMaxConcurrentSounds = 64;
 private:
  static void AudioCallback(void* userdata, std::uint8_t* stream, int len);
  static void VoiceAudioCallback(void* userdata, std::uint8_t* stream, int len);
  void MixOutput(std::int16_t* output, int frames);
  void MixVoiceOutput(std::int16_t* output, int frames);
  int AllocateChannel(int incoming_priority = -1);
  AudioDirectPlaybackHandle ClaimChannelInstance(int channel);
  AudioDirectPlaybackHandle StartStreamingSound(
      std::unique_ptr<IAudioDecoder> decoder, float volume, bool loop,
      std::optional<PlaybackChannel> playback_channel = std::nullopt,
      int priority = -1);
  [[nodiscard]] bool HandleOwnsChannelLocked(const HandleEntry& entry) const;
  void ApplyHandleRuntimeVolumeLocked(const HandleEntry& entry);

  void ReleaseChannelOwnershipIndex(const HandleEntry& entry) noexcept;
  std::shared_ptr<const SoundData> LoadAndDecode(const std::string& path);

  void LoadAndDecodeAsync(std::string path,
                          std::function<void(std::shared_ptr<const SoundData>)> on_ready);
  void EnsureDecodeWorkers();

  void RestartHandleFadeInOnChannelReadyLocked(int channel, std::uint64_t generation);

  void ClearCacheForOutputFormat();
  bool StartStreamingMusic(std::unique_ptr<IAudioDecoder> decoder,
                           bool loop, float fade_in_ms, float volume);
  bool StartStreamingCredits(std::unique_ptr<IAudioDecoder> decoder,
                             bool loop, float fade_in_ms, float volume);
  void StartDecodedMusic(std::shared_ptr<const SoundData> sound, bool loop, float fade_in_ms, float volume);
  void MixChannel(AudioChannel& ch, std::int32_t* mix_buf, int frames, float vol_scale);
  void UpdateSpatializationLocked(AudioChannel& channel);
  void ApplyWorldReverb(std::int32_t* mix_buf, int frames);
  void EnsureWorldReverbInitialized();
  void ResetPlaybackStateAfterDeviceClose(bool destroy_handles);
};
}
