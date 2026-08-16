
#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace openwow::audio {

using DriverNameResolver = std::function<bool(uint32_t index, char* name_out,
                                              uint32_t max_len, bool is_input)>;

using CvarSetter = std::function<void(const std::string& cvar_name,
                                       const std::string& value)>;

using CategoryVolumeSetter = std::function<void(const std::string& category,
                                                 float volume)>;

using VolumeSetter = std::function<void(float volume)>;

using StringToFloat = std::function<float(const std::string& str)>;

using StringToUint = std::function<uint32_t(const std::string& str)>;

inline bool OnVoiceChatInputDriverIndexChanged(
    const std::string& new_value,
    StringToUint parse_uint,
    DriverNameResolver resolve,
    CvarSetter set_cvar) {
  uint32_t index = parse_uint(new_value);
  char name_buf[256] = {};

  if (resolve(index, name_buf, 256, true)) {
    set_cvar("Sound_VoiceChatInputDriverName", name_buf);
  }
  return true;
}

inline bool OnVoiceChatOutputDriverIndexChanged(
    const std::string& new_value,
    StringToUint parse_uint,
    DriverNameResolver resolve,
    CvarSetter set_cvar) {
  uint32_t index = parse_uint(new_value);
  char name_buf[256] = {};

  if (resolve(index, name_buf, 256, false)) {
    set_cvar("Sound_VoiceChatOutputDriverName", name_buf);
  }
  return true;
}

inline bool OnSoundOutputDriverIndexChanged(
    const std::string& new_value,
    StringToUint parse_uint,
    DriverNameResolver resolve,
    CvarSetter set_cvar) {
  uint32_t index = parse_uint(new_value);
  char name_buf[256] = {};

  if (resolve(index, name_buf, 256, false)) {
    set_cvar("Sound_OutputDriverName", name_buf);
  }
  return true;
}

inline bool OnSoundMasterVolumeChanged(
    const std::string& new_value,
    StringToFloat parse_float,
    VolumeSetter set_volume) {
  set_volume(parse_float(new_value));
  return true;
}

inline bool OnSoundSfxVolumeChanged(
    const std::string& new_value,
    StringToFloat parse_float,
    CategoryVolumeSetter set_volume) {
  set_volume("SFX", parse_float(new_value));
  return true;
}

inline bool OnSoundMusicVolumeChanged(
    const std::string& new_value,
    StringToFloat parse_float,
    CategoryVolumeSetter set_volume) {
  float vol = parse_float(new_value);
  set_volume("MUSIC", vol);
  set_volume("SCRIPTMUSIC", vol);
  return true;
}

inline bool OnSoundAmbienceVolumeChanged(
    const std::string& new_value,
    StringToFloat parse_float,
    CategoryVolumeSetter set_volume) {
  float vol = parse_float(new_value);
  set_volume("AMBIENCE", vol);
  return true;
}

}
