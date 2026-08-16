#include "openwow/ui/surfaces/game/adapters/settings/world_ui_voice_settings_adapter.h"

#include "openwow/ui/game/cvar_system.h"

namespace openwow::ui::game {
namespace {

constexpr char kVoiceChatEnabledCVar[] = "EnableVoiceChat";
constexpr char kMicrophoneEnabledCVar[] = "EnableMicrophone";

bool ReadEnabledSetting(const char* name) {
  const auto& cvars = CVarSystem::Instance();
  return cvars.Exists(name) && cvars.GetCVarBool(name);
}

}

WorldUiVoiceSettings
WorldUiVoiceSettingsAdapter::CurrentWorldUiVoiceSettings() const {
  return {
      .voice_enabled = ReadEnabledSetting(kVoiceChatEnabledCVar),
      .microphone_enabled = ReadEnabledSetting(kMicrophoneEnabledCVar),
  };
}

}
