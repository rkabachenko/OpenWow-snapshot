#include "openwow/audio/adapters/lua/sound_voicechat_lua_registration.h"

#include "openwow/ui/game/api/game_lua_api_sound.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <array>

namespace openwow::audio {

namespace {

using namespace openwow::ui::game::detail;

constexpr std::array<openwow::ui::LuaGlobalBinding, 23> kBindings{{
    {"PlaySound", kPlaySound.handler},
    {"PlayMusic", kPlayMusic.handler},
    {"PlaySoundFile", kPlaySoundFile.handler},
    {"StopMusic", kStopMusic.handler},
    {"Sound_GameSystem_GetNumInputDrivers",
     kSound_GameSystem_GetNumInputDrivers.handler},
    {"Sound_GameSystem_GetInputDriverNameByIndex",
     kSound_GameSystem_GetInputDriverNameByIndex.handler},
    {"Sound_GameSystem_GetNumOutputDrivers",
     kSound_GameSystem_GetNumOutputDrivers.handler},
    {"Sound_GameSystem_GetOutputDriverNameByIndex",
     kSound_GameSystem_GetOutputDriverNameByIndex.handler},
    {"Sound_GameSystem_RestartSoundSystem",
     kSound_GameSystem_RestartSoundSystem.handler},
    {"Sound_ChatSystem_GetNumInputDrivers",
     kSound_ChatSystem_GetNumInputDrivers.handler},
    {"Sound_ChatSystem_GetInputDriverNameByIndex",
     kSound_ChatSystem_GetInputDriverNameByIndex.handler},
    {"Sound_ChatSystem_GetNumOutputDrivers",
     kSound_ChatSystem_GetNumOutputDrivers.handler},
    {"Sound_ChatSystem_GetOutputDriverNameByIndex",
     kSound_ChatSystem_GetOutputDriverNameByIndex.handler},
    {"VoiceChat_StartCapture", kVoiceChat_StartCapture.handler},
    {"VoiceChat_StopCapture", kVoiceChat_StopCapture.handler},
    {"VoiceChat_RecordLoopbackSound", kVoiceChat_RecordLoopbackSound.handler},
    {"VoiceChat_StopRecordingLoopbackSound",
     kVoiceChat_StopRecordingLoopbackSound.handler},
    {"VoiceChat_PlayLoopbackSound", kVoiceChat_PlayLoopbackSound.handler},
    {"VoiceChat_StopPlayingLoopbackSound",
     kVoiceChat_StopPlayingLoopbackSound.handler},
    {"VoiceChat_IsRecordingLoopbackSound",
     kVoiceChat_IsRecordingLoopbackSound.handler},
    {"VoiceChat_IsPlayingLoopbackSound",
     kVoiceChat_IsPlayingLoopbackSound.handler},
    {"VoiceChat_GetCurrentMicrophoneSignalLevel",
     kVoiceChat_GetCurrentMicrophoneSignalLevel.handler},
    {"VoiceChat_ActivatePrimaryCaptureCallback",
     kVoiceChat_ActivatePrimaryCaptureCallback.handler},
}};

constexpr auto BuildNames() {
  std::array<const char *, kBindings.size()> names{};
  for (std::size_t index = 0; index < kBindings.size(); ++index) {
    names[index] = kBindings[index].name;
  }
  return names;
}

constexpr auto kNames = BuildNames();

}

std::span<const char *const> GetSoundVoiceChatLuaGlobalNames() {
  return kNames;
}

std::span<const openwow::ui::LuaGlobalBinding> GetSoundVoiceChatLuaBindings() {
  return kBindings;
}

void RegisterSoundVoiceChatScriptFunctions(lua_State *state) {
  if (state == nullptr) {
    return;
  }
  openwow::ui::RegisterLuaGlobals(state, kBindings);
  diagnostics::Log(diagnostics::LogLevel::kDebug,
            "RegisterSoundVoiceChatScriptFunctions: registered 23 globals");
}

void UnregisterSoundVoiceChatScriptFunctions(lua_State *state) {
  if (state == nullptr) {
    return;
  }
  openwow::ui::UnregisterLuaGlobals(state, kBindings);
  diagnostics::Log(diagnostics::LogLevel::kDebug,
            "UnregisterSoundVoiceChatScriptFunctions: unregistered 23 globals");
}

}
