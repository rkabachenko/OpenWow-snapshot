#pragma once

#include "openwow/ui/game/cvar_system.h"

#include <array>
#include <thread>

namespace openwow::audio {

struct SoundCVarDefaultRegistration {
  const char *name;
  const char *description;
  const char *default_value;
};

inline void RegisterSoundInterfaceCVarDefaults(openwow::ui::game::CVarSystem &cvars) {
  using openwow::ui::game::CVarFlags;

  static constexpr std::array<SoundCVarDefaultRegistration, 24> kSoundInterfaceCVarDefaults = {{
      {"StartTalkingDelay", "", "0.0"},
      {"StartTalkingTime", "", "1.0"},
      {"StopTalkingDelay", "", "0.0"},
      {"StopTalkingTime", "", "2.0"},
      {"OutboundChatVolume", "The software amplification factor (0.0 - 2.0)", "1.0"},
      {"InboundChatVolume", "The volume of all other chat you hear (0.0 - 1.0)", "1.0"},
      {"VoiceChatMode", "Push to talk(0) or voice activation(1)", "0"},
      {"VoiceActivationSensitivity", "Sensitivity of the microphone (0.0 - 1.0)", "0.4"},
      {"EnableMicrophone", "Enables the microphone so you can speak.", "1"},
      {"EnableVoiceChat", "Enables the voice chat feature.", "0"},
      {"VoiceChatSelfMute", "Turn off your ability to talk.", "0"},
      {"PushToTalkButton", "String representation of the Push-To-Talk button.", "`"},
      {"Sound_OutputQuality", "sound quality, default 1 (medium)", "1"},
      {"Sound_NumChannels", "number of sound channels", "32"},
      {"Sound_EnableReverb", "", "0"},
      {"Sound_EnableSoftwareHRTF", "", "0"},
      {"Sound_VoiceChatInputDriverIndex", "", "0"},
      {"Sound_VoiceChatInputDriverName", "", "Primary Sound Capture Driver"},
      {"Sound_VoiceChatOutputDriverIndex", "", "0"},
      {"Sound_VoiceChatOutputDriverName", "", "Primary Sound Driver"},
      {"Sound_OutputDriverIndex", "", "0"},
      {"Sound_OutputDriverName", "", "Primary Sound Driver"},
      {"Sound_DSPBufferSize", "sound buffer size, default 0", "0"},
      {"Sound_EnableHardware", "Enables Hardware", "0"},

  }};

  for (const auto &cvar : kSoundInterfaceCVarDefaults) {
    cvars.RegisterCVar(cvar.name, cvar.default_value, CVarFlags::Archive, cvar.description);
  }

  cvars.RegisterCVar("Sound_EnableMode2", "0", CVarFlags::Archive, "test");
  cvars.RegisterCVar("Sound_EnableMixMode2", "0", CVarFlags::Archive, "test");
}

inline void RegisterSoundPlaybackCVarDefaults(openwow::ui::game::CVarSystem &cvars) {
  using openwow::ui::game::CVarFlags;

  static constexpr std::array<SoundCVarDefaultRegistration, 21> kPlaybackDefaults = {{
      {"ChatMusicVolume", "music volume (0.0 to 1.0)", "0.3"},
      {"ChatSoundVolume", "sound volume (0.0 to 1.0)", "0.4"},
      {"ChatAmbienceVolume", "Ambience Volume (0.0 to 1.0)", "0.3"},
      {"Sound_EnableSFX", "", "1"},
      {"Sound_EnableAmbience", "Enable Ambience", "1"},
      {"Sound_EnableErrorSpeech", "error speech", "1"},
      {"Sound_EnableMusic", "Enables music", "1"},
      {"Sound_EnableAllSound", "", "1"},
      {"Sound_MasterVolume", "master volume (0.0 to 1.0)", "1.0"},
      {"Sound_SFXVolume", "sound volume (0.0 to 1.0)", "1.0"},
      {"Sound_MusicVolume", "music volume (0.0 to 1.0)", "0.4"},
      {"Sound_AmbienceVolume", "Ambience Volume (0.0 to 1.0)", "0.6"},
      {"Sound_ListenerAtCharacter", "lock listener at character", "1"},
      {"Sound_EnableEmoteSounds", "", "1"},
      {"Sound_ZoneMusicNoDelay", "", "0"},
      {"Sound_EnableArmorFoleySoundForSelf", "", "1"},
      {"Sound_EnableArmorFoleySoundForOthers", "", "1"},
      {"Sound_EnablePetSounds", "Enables pet sounds", "1"},
      {"Sound_EnableSoundWhenGameIsInBG", "Enable Sound When Game Is In Background", "1"},
      {"Sound_MaxCacheSizeInBytes", "Max cache size in bytes", "16777216"},
      {"Sound_MaxCacheableSizeInBytes",
       "Max sound size that will be cached, larger files will be streamed instead", "1048576"},
  }};

  for (const auto &cvar : kPlaybackDefaults) {
    cvars.RegisterCVar(cvar.name, cvar.default_value, CVarFlags::Archive, cvar.description);
  }

  const char *const dsp_default = std::thread::hardware_concurrency() > 1 ? "1" : "0";
  cvars.RegisterCVar("Sound_EnableDSPEffects", dsp_default, CVarFlags::Archive, "");
}

}
