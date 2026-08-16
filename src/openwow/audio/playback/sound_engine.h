#pragma once
#include "openwow/core/legacy_buffered_log_file.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdarg>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "openwow/foundation/compiler/printf_format.h"
namespace openwow::audio {
class AudioEngine;
class SoundEngine;
class VoiceChatLoopback;
constexpr float kSEDopplerScale   = 1.0f;
constexpr float kSEDistanceFactor = 1.0f;
constexpr float kSERolloffScale   = 4.0f;
void SoundEngine_InitMemoryConfig(void* engine);
void SoundCache_EvictToSize(void* cache, std::uint32_t target_size);
void TSExplicitList_CacheData_Destroy(void* list);
int CacheData_Destroy(void* cache_entry);
void TSHashTable_UCacheData_Clear(void* hash_table);
OPENWOW_PRINTF_FORMAT(3, 4)
void SoundEngine_LogError(SoundEngine& engine, int context, const char* format, ...);
int ComSatSoundIO_ReadChunked(void* io_obj, int offset, std::uint32_t total_size);
int SEStreamedSound_PcmSetPosCallback(void* sound_obj_ptr, int new_position);
double SoundEngine_Custom3DRolloff(float min_dist, float max_dist, float current_dist);
bool SoundEngine_GetOutputDeviceName(int device_index, char* name_buf, int buf_size, int use_recording);
bool SoundEngine_GetInputDeviceName(int device_index, char* name_buf, int buf_size, int use_recording);
int SoundEngine_GetSoundOpenState(void* sound_obj);
void* SoundEngine_GetSoundGroup(void* sound_obj);
int SoundEngine_SetSoundLoopPoints(void* sound_obj, int loop_start, int loop_end);
int SoundEngine_UpdateDSPChannel(void* sound_obj, float pan_level);
void SoundEngine_ProcessCaptureData(int context);
int SoundEngine_DSPReadCallback(int output, void* in_data, void* out_data, int sample_count, int unused1, int unused2);
int SoundEngine_CreateMonitorDSP();
void SoundEngine_ConnectMonitorDSP(void* handle);
bool SoundEngine_IsRecording();
void SoundEngine_SetChannelPlayState(void* channel, int action, float volume);
int SoundI_GetStreamState(void* state_ptr);
const char* SoundEngine_GetCurrentVoiceOutputDriverName();
const char* SoundEngine_GetCurrentInputDriverName();
struct SEChannelGroup {
  std::uint32_t name_hash{0};
  std::int32_t  parent_index{-1};
  float         volume{1.0f};
  float         effective_volume{1.0f};
  float         pitch{1.0f};
  float         effective_pitch{1.0f};
  bool          pause_state{false};
  bool          mute_state{false};
  bool          dirty{true};
};
struct ICaptureIOSink { virtual ~ICaptureIOSink() = default; virtual void OnCaptureData(const void* data, std::uint32_t sample_count) = 0; };
enum SoundFlags : std::uint32_t { kSoundFlag_Loop    = 0x02, kSoundFlag_2D      = 0x08, kSoundFlag_3D      = 0x10, kSoundFlag_HeadRel = 0x40, };
enum SoundKind : std::int32_t { kSoundKind_Normal   = 1, kSoundKind_Stream   = 2, kSoundKind_Capture  = 3, };
struct SoundObj {
  bool in_cache_list{false};
  std::uintptr_t cached_data_handle{0};
  std::uint32_t playback_handle_id{0};
  std::uint32_t audibility_token{0};
  float direct_volume{1.0f};
  float fade_volume{1.0f};
  float fade_in_duration{0.0f};
  float fade_out_duration{0.0f};
  bool fade_in_active{false};
  bool fade_out_active{false};
  SoundKind kind{kSoundKind_Normal};
  std::int32_t channel_group_index{-1};
  std::uint32_t creation_flags{0};
  std::int32_t cleanup_remaining_ms{500};
  std::uint32_t cache_ref_count{0};
  std::uint32_t cache_flags{0};
  std::uint32_t cache_size_bytes{0};
  std::int32_t  priority{0};
  float         pos_x{0.0f};
  float         pos_y{0.0f};
  float         pos_z{0.0f};
  float         min_distance{0.0f};
  float         max_distance{50.0f};
  std::uint32_t guid_lo{0};
  std::uint32_t guid_hi{0};
};
using PositionCallback = std::function<bool(std::uint32_t guid_lo, std::uint32_t guid_hi, float* pos_out)>;
using SoundAudibilityCallback = float(*)(std::uint32_t audibility_token);
using DeviceChangedCallback = std::function<void()>;
using InputLevelCallback = std::function<void(const float* samples, int sample_count)>;
using SoundEndCallback   = void(*)(std::uint32_t handle_ptr);
using SoundStartCallback = void(*)(std::uint32_t channel, std::uint32_t user);
struct SEDeviceInfo {
  char name[256]{};
};
struct SESoundEnumEntry {
  std::int32_t  type{0};
  float         pos_x{0}, pos_y{0}, pos_z{0};
  float         min_dist{0}, max_dist{0};
  std::uint32_t sound_handle{0};
  float         volume{0};
  float         audibility{0};
  float         distance_sq{0};
  float         frequency{0};
  std::uint32_t channel_group_handle{0};
  bool          is_3d{false};
  bool          is_fading_in{false};
  bool          is_fading_out{false};
  bool          is_virtual{false};
  std::int32_t  flags{0};
  char          pad[4]{};
};
#include "openwow/audio/playback/sound_engine_state.h"
class SoundEngine : private SoundEngineState {
 public:
  struct FeatureCapabilities {
    bool hardware_voice_selection{false};
    bool software_hrtf{true};
  };

  struct DspUnit;
  explicit SoundEngine(AudioEngine& audio_engine) : SoundEngineState(audio_engine) {}
  ~SoundEngine();
  void BindVoiceChatLoopback(VoiceChatLoopback& loopback) noexcept { voice_chat_loopback_ = &loopback; }
  void SetCaptureIOSink(ICaptureIOSink* sink) noexcept {
    capture_io_sink_ = sink;
  }
  void ClearCaptureIOSink(const ICaptureIOSink* sink) noexcept {
    if (capture_io_sink_ == sink) {
      capture_io_sink_ = nullptr;
    }
  }
  bool Init(int max_channels, PositionCallback pos_cb, int output_driver_index,
            int output_quality, int* error_out,
            int* actual_output_driver_index = nullptr);
  void Shutdown();
  void ShutdownGameSound(bool keep_channel_groups);
  void Update();
  bool ProcessUpdateTick();
  [[nodiscard]] bool IsInitialized() const { return initialized_; }
  [[nodiscard]] constexpr FeatureCapabilities Capabilities() const noexcept {
    return {.hardware_voice_selection = false, .software_hrtf = true};
  }
  int CreateDSPByType(std::uint32_t type_id, void** dsp_out);
  int DSPSetBypass(void* dsp, bool bypass);
  int DSPInsertAfter(void* dsp, void* target_dsp);
  void* CreateDSPByName(const char* type_name);
  DspUnit* CreateMonitorDSP();
  [[nodiscard]] InputLevelCallback GetInputLevelCallback() const { return input_level_callback_; }
  int DSPSetParameter(void* dsp, float value);
  float GetDspResolvedParameter(void* dsp);
  int RemoveDSP(void* dsp);
  void DestroyDSP(void* dsp);
  void ConnectMonitorDSP(SoundObj* obj);
  DspUnit* RegisterPlaybackDspHead(std::uint32_t handle_id);
  void UnregisterPlaybackDspHead(std::uint32_t handle_id);
  [[nodiscard]] DspUnit* FindPlaybackDspHead(std::uint32_t handle_id) const;
  bool SetSoundDistances(SoundObj* obj, float min_dist, float max_dist);
  void SetSound3DPosition(SoundObj* obj, const float* pos3);
  void SetMasterVolume(float volume);
  void SetMasterMuted(bool muted);
  void SetSoundAudibilityCallback(SoundAudibilityCallback callback);
  void ApplyChannelGroupVolume(SoundObj* obj);
  void PropagatePitch(std::size_t group_index);
  void PropagatePitchByName(const char* group_name);
  void UpdatePauseMuteState(SoundObj* obj, bool paused_by_group);
  void PropagatePauseState(std::size_t group_index, bool paused);
  [[nodiscard]] bool IsGroupPaused(std::size_t group_index) const;
  void PropagateMuteState(std::size_t group_index, bool muted);
  [[nodiscard]] bool IsGroupMuted(std::size_t group_index) const;
  void UpdateMuteVolume(SoundObj* obj, bool muted_by_group);
  void SetSoundVolume(SoundObj* obj, float volume);
  void StartCapture();
  void StopCapture();
  void DisableCapture();
  [[nodiscard]] bool IsRecording() const;
  void PlaySound(SoundObj* obj);
  void StopSound(SoundObj* obj, bool immediate, float fade_time);
  void StopAllSounds();
  void StartPlayback(SoundObj* obj);
  bool GetSoundIsPlaying(SoundObj* obj) const;
  static void StartFadeIn(SoundObj* obj);
  static void StartFadeOut(SoundObj* obj);
  static void SetFadeInDuration(SoundObj* obj, float duration);
  static void SetFadeOutDuration(SoundObj* obj, float duration);
  bool CreateSoundInstance(const char* filename, std::uint32_t flags, std::uint32_t handle_ptr, std::uint32_t fade_in_len, std::uint32_t fade_out_len, std::uint8_t  non_blocking, std::uint32_t priority, std::uint32_t head_relative, std::uint8_t  loop_sound, std::uint32_t file_offset, std::uint32_t use_hrtf, float cone_inside, float cone_outside, float cone_vol, const float*  orientation);
  int StopStream(int playback_handle, bool release);
  void CreateStream(const char* path, std::uint32_t flags, std::uint32_t sound_kind, std::uint32_t handle_ptr);
  void CreateCapturedStream(int stream_handle, std::uint32_t flags, std::uint32_t handle_ptr, bool use_voice_system);
  bool QueueVoicePlaybackPcm(std::uint32_t stream_index,
                             const std::int16_t *samples,
                             std::size_t sample_count,
                             int sample_rate,
                             int channels,
                             float volume,
                             double playback_rate);
  void SetVoicePlaybackVolume(std::uint32_t stream_index, float volume);
  void ResetVoicePlaybackStream(std::uint32_t stream_index);
  void ResetAllVoicePlaybackStreams();
  [[nodiscard]] std::size_t
  GetQueuedVoicePlaybackSampleCount(std::uint32_t stream_index) const;
  int EnumActiveSounds(SESoundEnumEntry* out, int max_count, bool include_playing, bool include_paused, bool include_stopped, bool include_no_channel, bool include_virtual, int* playing_out, int* paused_out, int* stopped_out, int* no_channel_out, int* virtual_out);
  void EnumerateDevices();
  void SetDeviceChangedCallback(DeviceChangedCallback callback);
  void SetInputLevelCallback(InputLevelCallback callback);
  [[nodiscard]] int GetMicrophoneSignalLevel() const { return microphone_signal_level_; }
  void SetMicrophoneSignalLevel(int level) { microphone_signal_level_ = level; }
  void LogOutputDeviceInfo(int driver_index, const char* usage, bool is_record);
  SEChannelGroup* FindOrCreateChannelGroup(const char* name, bool create_if_missing, bool attach_to_master);
  [[nodiscard]] float GetChannelGroupCompositeVolume(const char* name) const;
  void SetChannelGroupVolume(const char* name, float volume, bool attach_to_master = true);
  void SetChannelGroupMuted(const char* name, bool muted);
  void AssignChannelGroup(SoundObj* obj, const char* group_name, bool attach_to_master);
  void StopChannelGroupSounds(const char* group_name, float fade_time);
  void PurgeSoundCache(bool force_all);
  void DestroyCacheLock();
  void UpdatePositions();
  void CompleteNonBlockingLoad(SoundObj* obj, int result_code);
  void CleanupFinishedSounds();
  void CleanupExpiredSounds(bool force);
  void ReleaseSoundObj(SoundObj* obj);
  void CleanupSoundObj(SoundObj* obj);
  void ShutdownVoiceChat();
  bool InitVoiceChat(int* output_driver, const char* output_name, int* input_driver, const char* input_name, float mic_volume);
  void CaptureWork(std::uint32_t context_tick_ms);
  [[nodiscard]] bool IsCaptureEnabled() const { return capture_enabled_; }
  [[nodiscard]] bool IsVoiceChatEnabled() const { return voice_chat_enabled_; }
  [[nodiscard]] int  GetOutputDeviceCount() const { return static_cast<int>(output_devices_.size()); }
  [[nodiscard]] int  GetVoiceOutputDeviceCount() const { return static_cast<int>(voice_output_devices_.size()); }
  [[nodiscard]] int  GetInputDeviceCount()  const { return static_cast<int>(input_devices_.size()); }
  [[nodiscard]] int  GetRecordDeviceCount() const { return static_cast<int>(record_devices_.size()); }
  [[nodiscard]] const char* GetOutputDeviceName(int idx) const;
  [[nodiscard]] const char* GetVoiceOutputDeviceName(int idx) const;
  [[nodiscard]] const char* GetInputDeviceName(int idx) const;
  [[nodiscard]] const char* GetRecordDeviceName(int idx) const;
  [[nodiscard]] const std::string& GetCurrentOutputDeviceName() const { return current_output_device_name_; }
  [[nodiscard]] const std::string& GetCurrentVoiceOutputDeviceName() const { return current_voice_output_device_name_; }
  [[nodiscard]] const std::string& GetCurrentInputDeviceName() const { return current_input_device_name_; }
  [[nodiscard]] const std::string& GetEnumeratedDefaultOutputDeviceName() const;
  [[nodiscard]] const std::string& GetEnumeratedDefaultVoiceOutputDeviceName() const;
  [[nodiscard]] const std::string& GetEnumeratedDefaultInputDeviceName() const;
  void CommitEnumeratedDefaultOutputDeviceName();
  void SetCurrentVoiceOutputDeviceName(std::string_view device_name);
  void SetCurrentInputDeviceName(std::string_view device_name);
  [[nodiscard]] bool IsOutputDeviceReopenPending() const { return output_device_reopen_pending_; }
  void SetOutputDeviceReopenPending(const bool pending) { output_device_reopen_pending_ = pending; }
  [[nodiscard]] bool HasSoundGroupReachedInstanceLimit(const char* group_name, std::uint32_t limit) const;
  void IncrementSoundGroupInstanceCount(const char* group_name);
  void DecrementSoundGroupInstanceCount(const char* group_name);
 private:
  SoundEngine(const SoundEngine&) = delete;
  SoundEngine& operator=(const SoundEngine&) = delete;
  friend void LogCreateSoundError(int line, int error_code);
  friend void SoundEngine_LogError(SoundEngine& engine, int context, const char* format, ...);
  void OpenSoundLog();
  void CloseSoundLog();

  OPENWOW_PRINTF_FORMAT(5, 6)
  void LogLine(int line, const char* file, int error_code, const char* format, ...);
  OPENWOW_PRINTF_FORMAT(5, 0)
  void LogLineV(int line, const char* file, int error_code, const char* format, va_list args);
  struct OutputDriverChannelCounts {
    int hardware_2d{0};
    int hardware_3d{0};
    int total_hardware{0};
  };
  [[nodiscard]] OutputDriverChannelCounts QueryOutputDriverChannelCounts() const;
  float ComputeEffectiveVolume(SoundObj* obj) const;
  [[nodiscard]] float ComputeChannelGroupGain(const SoundObj& obj) const;
  void ApplyResolvedSoundVolume(SoundObj* obj) const;
  static std::uint32_t HashCI(const char* str);
public:
  struct DspConnection;
  struct DspUnit {
    std::string debug_name;
    float parameter_value{1.0f};
    float resolved_parameter_value{1.0f};
    float frequency{44100.0f};
    float frequency_min{0.0f};
    float frequency_max{192000.0f};
    bool bypassed{false};
    bool active{true};
    std::vector<DspConnection*> inputs;
    std::vector<DspConnection*> outputs;
    void* system_object{nullptr};
  };
  struct DspConnection {
    DspUnit* source{nullptr};
    DspUnit* destination{nullptr};
  };
  [[nodiscard]] DspUnit* ResolveDsp(void* dsp);
  [[nodiscard]] const DspUnit* ResolveDsp(const void* dsp) const;
  [[nodiscard]] int ConnectDspInput(DspUnit* destination, DspUnit* source, DspConnection** connection_out = nullptr);
  [[nodiscard]] int DisconnectDspConnection(DspConnection* connection);
  [[nodiscard]] int GetDspOutputAtIndex(DspUnit* dsp, std::size_t index, DspConnection** connection_out, DspUnit** output_out);
  [[nodiscard]] int GetDspInputAtIndex(DspUnit* dsp, std::size_t index, DspConnection** connection_out, DspUnit** input_out);
  [[nodiscard]] std::size_t GetNumDspInputs(const DspUnit* dsp) const;
  [[nodiscard]] int SpliceDspInputHead(DspUnit* head, DspUnit* node);
  void RefreshResolvedDspParameter(DspUnit* dsp);
  [[nodiscard]] int DisconnectDspGraphEdges(DspUnit* dsp, bool disconnect_inputs, bool disconnect_outputs);
  [[nodiscard]] int DisconnectDspInputsFrom(DspUnit* dsp, DspUnit* source_to_remove);
  void DestroyDspUnit(DspUnit* dsp);
  [[nodiscard]] int DeactivateDspNode(DspUnit* dsp);
  void ResetDspGraph();
  void CleanupActiveVoiceCaptureSession();
  [[nodiscard]] bool CanDestroyExpiredSound(const SoundObj& sound) const;
  void ReleaseExpiredSoundHandle(SoundObj* sound);
 private:
  std::vector<std::unique_ptr<DspUnit>> dsp_units_;
  DspUnit* monitor_dsp_{nullptr};
  std::vector<std::unique_ptr<DspConnection>> dsp_connections_;
  std::unordered_map<std::uint32_t, DspUnit*> playback_dsp_heads_;
};
}
