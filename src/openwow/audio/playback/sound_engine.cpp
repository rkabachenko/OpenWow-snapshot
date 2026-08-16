#include "openwow/audio/playback/sound_engine.h"
#include "openwow/audio/playback/audio_engine.h"
#include "openwow/audio/voice/voice_chat_loopback.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <span>
#include <string>

namespace {
std::uint32_t GetTickCountMs() {
  using namespace std::chrono;
  return static_cast<std::uint32_t>(
      duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

void AppendEnumeratedDevices(std::vector<openwow::audio::SEDeviceInfo>* devices,
                             const int is_capture) {
  if (!devices) {
    return;
  }

  devices->clear();

  openwow::audio::SEDeviceInfo system_default{};
  std::strncpy(system_default.name, "System Default",
               sizeof(system_default.name) - 1);
  devices->push_back(system_default);

  if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO)) {
    return;
  }

  const int count = SDL_GetNumAudioDevices(is_capture);
  for (int index = 0; index < count; ++index) {
    const char* name = SDL_GetAudioDeviceName(index, is_capture);
    if (!name || !*name) {
      continue;
    }

    const auto duplicate = std::find_if(
        devices->begin() + 1, devices->end(),
        [name](const openwow::audio::SEDeviceInfo& entry) {
          return std::strncmp(entry.name, name, sizeof(entry.name)) == 0;
        });
    if (duplicate != devices->end()) {
      continue;
    }

    openwow::audio::SEDeviceInfo info{};
    std::strncpy(info.name, name, sizeof(info.name) - 1);
    devices->push_back(info);
  }
}

std::string EnumeratedDefaultDeviceName(const std::vector<openwow::audio::SEDeviceInfo>& devices) {
  if (devices.size() <= 1) {
    return {};
  }
  return devices[1].name;
}
}

namespace openwow::audio {

SoundEngine::~SoundEngine() = default;

void SoundEngine_LogError(SoundEngine& engine, const int context, const char* format, ...) {
  va_list args;
  va_start(args, format);
  engine.LogLineV(0, ".\\SoundEngine.cpp", context, format, args);
  va_end(args);
}

double SoundEngine_Custom3DRolloff(float min_dist,
                                   float max_dist,
                                   float current_dist) {
  if (max_dist <= min_dist) {
    max_dist = min_dist + 1.0f;
  }
  if (current_dist <= min_dist) {
    return 1.0;
  }
  if (current_dist >= max_dist) {
    return 0.0;
  }

  const double base =
      min_dist /
      ((current_dist - min_dist) * kSERolloffScale + min_dist);
  const double soft_fade_start = max_dist * 0.9;
  if (current_dist <= soft_fade_start) {
    return base;
  }

  const double soft_fade =
      (current_dist - soft_fade_start) / (max_dist - soft_fade_start);
  return base * (1.0 - soft_fade);
}

void SoundEngine::OpenSoundLog() {
  std::lock_guard<std::mutex> lock(sound_log_mutex_);
  if (sound_log_file_.IsOpen()) {
    return;
  }

  const auto open_mode = sound_log_append_on_next_open_
                             ? openwow::core::LegacyBufferedLogOpenMode::kAppend
                             : openwow::core::LegacyBufferedLogOpenMode::kTruncate;
  if (sound_log_file_.Open("Logs\\SESound.log", open_mode)) {
    sound_log_append_on_next_open_ = true;
  }
}

void SoundEngine::CloseSoundLog() {
  std::lock_guard<std::mutex> lock(sound_log_mutex_);
  sound_log_file_.Close();
}

void SoundEngine::LogLine(const int line,
                          const char* file,
                          const int error_code,
                          const char* format,
                          ...) {
  va_list args;
  va_start(args, format);
  LogLineV(line, file, error_code, format, args);
  va_end(args);
}

void SoundEngine::LogLineV(const int line,
                           const char* file,
                           const int error_code,
                           const char* format,
                           va_list args) {
  std::lock_guard<std::mutex> lock(sound_log_mutex_);
  if (!sound_log_file_.IsOpen()) {
    return;
  }

  char formatted[512]{};
  if (format != nullptr) {
    va_list args_copy;
    va_copy(args_copy, args);
    std::vsnprintf(formatted, sizeof(formatted), format, args_copy);
    va_end(args_copy);
  }

  if (error_code == 0) {
    sound_log_file_.AppendLine(formatted);
    sound_log_file_.FlushPending();
    return;
  }

  if (sound_log_error_count_ < 200 && error_code != 23) {
    char error_line[512]{};
    std::snprintf(error_line, sizeof(error_line),
                  " -######## AUDIO ERROR! (err %d)", error_code);
    sound_log_file_.AppendLine(error_line);
    if (formatted[0] != '\0') {
      sound_log_file_.AppendLine(formatted);
    }

    char source_line[512]{};
    std::snprintf(source_line, sizeof(source_line), "%s(%d)",
                  file != nullptr ? file : "", line);
    sound_log_file_.AppendLine(source_line);
    sound_log_file_.FlushPending();
    ++sound_log_error_count_;
  }

  if (sound_log_error_count_ == 200) {
    sound_log_file_.AppendLine(
        " -######## TOO MANY ERRORS. NO FURTHER ERRORS WILL BE LOGGED.");
    sound_log_file_.FlushPending();
    ++sound_log_error_count_;
  }
}

void SoundEngine::LogOutputDeviceInfo(int driver_index,
                                       const char* usage,
                                       bool is_record) {
  const char* name = "Unknown";
  if (!is_record && driver_index >= 0 &&
      driver_index < static_cast<int>(output_devices_.size())) {
    name = output_devices_[driver_index].name;
  } else if (is_record && driver_index >= 0 &&
             driver_index < static_cast<int>(input_devices_.size())) {
    name = input_devices_[driver_index].name;
  }
  LogLine(2267, ".\\SoundEngine.cpp", 0,
          " - Using [%d] %s for %s", driver_index, name, usage);
}

int SoundEngine::StopStream(int playback_handle, bool release) {
  if (playback_handle == 0) return 0;

  const AudioPlaybackHandle handle{
      static_cast<std::uint32_t>(playback_handle)};
  auto& audio = audio_engine_;
  audio.Stop(handle);
  if (release) {
    UnregisterPlaybackDspHead(handle.handleId);
    audio.DestroyHandle(handle);
  }
  return 0;
}

void SoundEngine::CreateStream(const char* path,
                                std::uint32_t flags,
                                std::uint32_t ,
                                std::uint32_t handle_ptr) {

  if (!initialized_ || !path || !*path) return;

  LogLine(0, ".\\SoundEngine.cpp", 0, "CreateStream: %s", path);

  AudioClipInfo clip;
  clip.path = path;
  clip.channel = PlaybackChannel::SFX;
  clip.volume = 1.0f;
  clip.loop = (flags & kSoundFlag_Loop) != 0;

  AudioPlaybackHandle handle = audio_engine_.Play(clip);
  if (handle.handleId == 0) {
    LogLine(0, ".\\SoundEngine.cpp", 0,
            "CreateStream: Play returned 0 for %s", path);
    return;
  }

  if (handle_ptr != 0) {
    *reinterpret_cast<std::uint32_t*>(
        static_cast<std::uintptr_t>(handle_ptr)) = handle.handleId;
  }

  RegisterPlaybackDspHead(handle.handleId);
}
void SoundEngine::EnumerateDevices() {
  AppendEnumeratedDevices(&output_devices_, 0);
  voice_output_devices_ = output_devices_;
  AppendEnumeratedDevices(&input_devices_, 1);
  record_devices_ = input_devices_;
  enumerated_default_output_device_name_ = EnumeratedDefaultDeviceName(output_devices_);
  enumerated_default_voice_output_device_name_ = EnumeratedDefaultDeviceName(voice_output_devices_);
  enumerated_default_input_device_name_ = EnumeratedDefaultDeviceName(input_devices_);
}

void SoundEngine::SetDeviceChangedCallback(DeviceChangedCallback callback) {
  device_changed_callback_ = callback;
}

void SoundEngine::SetInputLevelCallback(InputLevelCallback callback) {
  input_level_callback_ = callback;
}
const char* SoundEngine::GetOutputDeviceName(int idx) const {
  if (idx < 0 || idx >= static_cast<int>(output_devices_.size()))
    return "Unknown";
  return output_devices_[idx].name;
}

const char* SoundEngine::GetVoiceOutputDeviceName(int idx) const {
  if (idx < 0 || idx >= static_cast<int>(voice_output_devices_.size()))
    return "Unknown";
  return voice_output_devices_[idx].name;
}

const char* SoundEngine::GetInputDeviceName(int idx) const {
  if (idx < 0 || idx >= static_cast<int>(input_devices_.size()))
    return "Unknown";
  return input_devices_[idx].name;
}

const char* SoundEngine::GetRecordDeviceName(int idx) const {
  if (idx < 0 || idx >= static_cast<int>(record_devices_.size()))
    return "Unknown";
  return record_devices_[idx].name;
}

const std::string& SoundEngine::GetEnumeratedDefaultOutputDeviceName() const {
  return enumerated_default_output_device_name_;
}

const std::string& SoundEngine::GetEnumeratedDefaultVoiceOutputDeviceName() const {
  return enumerated_default_voice_output_device_name_;
}

const std::string& SoundEngine::GetEnumeratedDefaultInputDeviceName() const {
  return enumerated_default_input_device_name_;
}

void SoundEngine::CommitEnumeratedDefaultOutputDeviceName() {
  current_output_device_name_ = enumerated_default_output_device_name_;
}

void SoundEngine::SetCurrentVoiceOutputDeviceName(std::string_view device_name) {
  current_voice_output_device_name_.assign(device_name.begin(), device_name.end());
  if (voice_chat_enabled_) {
    (void)audio_engine_.OpenVoiceOutputDevice(current_voice_output_device_name_);
  }
}

void SoundEngine::SetCurrentInputDeviceName(std::string_view device_name) {
  current_input_device_name_.assign(device_name.begin(), device_name.end());
}

SoundEngine::OutputDriverChannelCounts SoundEngine::QueryOutputDriverChannelCounts() const {

  return {};
}

bool SoundEngine::Init(int max_channels,
                        PositionCallback pos_cb,
                        int output_driver_index,
                        int output_quality,
                        int* error_out,
                        int* actual_output_driver_index) {
  if (error_out) *error_out = 0;
  if (initialized_) return true;

  OpenSoundLog();
  std::error_code ec;
  std::filesystem::remove(std::filesystem::path("Logs") / "Sound.log", ec);

  LogLine(0, ".\\SoundEngine.cpp", 0, " ");
  LogLine(0, ".\\SoundEngine.cpp", 0, "=> Initializing Game Sound");
  LogLine(0, ".\\SoundEngine.cpp", 0, "Version 3.3.5 / Build 12340");

  position_callback_ = pos_cb;

  const int sw_channels = std::clamp(max_channels, kMinSoftwareChannels,
                                     kMaxSoftwareChannels);
  audio_engine_.SetMaxSfxChannels(sw_channels);

  int sample_rate = 44100;
  if (output_quality == 0)      sample_rate = 22050;
  else if (output_quality == 2) sample_rate = 48000;

  EnumerateDevices();
  if (output_driver_index < 0 ||
      output_driver_index >= static_cast<int>(output_devices_.size())) {
    output_driver_index = 0;
  }
  const std::string_view requested_device_name =
      output_driver_index == 0 ? std::string_view{}
                               : output_devices_[output_driver_index].name;

  constexpr int kRetailDspBlockSamples = 0x400;
  bool output_opened = audio_engine_.Initialize(
      sample_rate, 2, kRetailDspBlockSamples, requested_device_name);
  if (!output_opened && output_driver_index != 0) {
    output_driver_index = 0;
    output_opened = audio_engine_.Initialize(
        sample_rate, 2, kRetailDspBlockSamples);
  }
  if (!output_opened) {
    LogLine(0, ".\\SoundEngine.cpp", 33, "AudioEngine init failed");
    if (error_out) *error_out = 33;
    return false;
  }
  if (actual_output_driver_index != nullptr) {
    *actual_output_driver_index = output_driver_index;
  }

  channel_groups_.clear();
  SEChannelGroup master;
  master.name_hash = HashCI("<master>");
  master.parent_index = -1;
  master.volume = 1.0f;
  master.effective_volume = 1.0f;
  master.pitch = 1.0f;
  master.effective_pitch = 1.0f;
  master.dirty = true;
  channel_groups_.push_back(master);

  current_output_device_name_ =
      output_driver_index == 0
          ? enumerated_default_output_device_name_
          : output_devices_[output_driver_index].name;

  const OutputDriverChannelCounts output_channel_counts = QueryOutputDriverChannelCounts();
  LogLine(2557, ".\\SoundEngine.cpp", 0,
          " - Sound Driver Reports: %d Hardware Channels Available.",
          output_channel_counts.hardware_3d);

  last_tick_ms_ = GetTickCountMs();
  initialized_ = true;

  LogLine(0, ".\\SoundEngine.cpp", 0,
          "=> Done Initializing Game Sound (%d sw channels, %d Hz)",
          sw_channels, sample_rate);
  return true;
}

void SoundEngine::Shutdown() {
  if (!initialized_) {
    ResetDspGraph();
    return;
  }

  ShutdownVoiceChat();
  StopAllSounds();
  ShutdownGameSound(false);

  audio_engine_.Shutdown();
  initialized_ = false;
  CleanupActiveVoiceCaptureSession();
  capture_enabled_ = false;
  voice_chat_enabled_ = false;
  current_output_device_name_.clear();
  current_voice_output_device_name_.clear();
  current_input_device_name_.clear();
  enumerated_default_output_device_name_.clear();
  enumerated_default_voice_output_device_name_.clear();
  enumerated_default_input_device_name_.clear();
  output_device_reopen_pending_ = false;
  CloseSoundLog();
}

void SoundEngine::ShutdownGameSound(bool keep_channel_groups) {
  LogLine(3009, ".\\SoundEngine.cpp", 0, " ");
  LogLine(3010, ".\\SoundEngine.cpp", 0, "=> Shutting Down Game Sound");

  if (!keep_channel_groups) {
    channel_groups_.clear();
  }

  PurgeSoundCache(true);
  ResetDspGraph();
  initialized_ = false;

  LogLine(3046, ".\\SoundEngine.cpp", 0, "=> Done Shutting Down Game Sound");
}

bool SoundEngine::ProcessUpdateTick() {
  if (!initialized_) {
    return true;
  }

  PurgeSoundCache(false);
  Update();
  UpdatePositions();
  CleanupFinishedSounds();
  CleanupExpiredSounds(false);

  return true;
}

void SoundEngine::Update() {
  if (!initialized_) return;

  std::uint32_t now = GetTickCountMs();
  float delta_ms = static_cast<float>(now - last_tick_ms_);
  if (static_cast<int>(now - last_tick_ms_) < 0) {
    last_tick_ms_ = now;
    return;
  }

  bool changed = true;
  while (changed) {
    changed = false;
    for (std::size_t i = 0; i < channel_groups_.size(); ++i) {
      auto& g = channel_groups_[i];
      if (g.dirty) continue;
      if (g.parent_index >= 0 &&
          g.parent_index < static_cast<int>(channel_groups_.size()) &&
          channel_groups_[g.parent_index].dirty) {
        g.dirty = true;
        changed = true;
      }
    }
  }

  for (auto& g : channel_groups_) g.dirty = false;

  audio_engine_.Update(delta_ms);

  last_tick_ms_ = now;
}

}
