#include "glue_host/settings_capability_policy.h"

#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/glue/glue_widget_runtime.h"

#include <string>
#include <string_view>
#include <vector>

namespace openwow::client {
namespace {

std::vector<std::string_view> UnavailableControls(
    const ClientSettingsCapabilities capabilities) {
  std::vector<std::string_view> unavailable;
  if (!capabilities.terrain_texture_detail) {
    unavailable.emplace_back("VideoOptionsEffectsPanelTerrainDetail");
  }
  if (!capabilities.ground_clutter) {
    unavailable.emplace_back("VideoOptionsEffectsPanelClutterDensity");
    unavailable.emplace_back("VideoOptionsEffectsPanelClutterRadius");
  }
  if (!capabilities.texture_resolution) {
    unavailable.emplace_back("VideoOptionsEffectsPanelTextureResolution");
  }
  if (!capabilities.texture_filtering_tiers) {
    unavailable.emplace_back("VideoOptionsEffectsPanelTextureFiltering");
  }
  if (!capabilities.hardware_audio_voices) {
    unavailable.emplace_back("AudioOptionsSoundPanelUseHardware");
  }
  if (!capabilities.software_hrtf) {
    unavailable.emplace_back("AudioOptionsSoundPanelHRTF");
  }

  return unavailable;
}

}

void ApplySettingsCapabilityPolicy(
    openwow::ui::glue::GlueWidgetRuntime& widgets,
    openwow::ui::game::CVarSystem& cvars,
    const ClientSettingsCapabilities capabilities) {
  if (!capabilities.hardware_audio_voices) {
    (void)cvars.SetCVar("Sound_EnableHardware", "0", true);
  }
  if (!capabilities.software_hrtf) {
    (void)cvars.SetCVar("Sound_EnableSoftwareHRTF", "0", true);
  }

  for (const auto control : UnavailableControls(capabilities)) {
    widgets.SetCapabilityAvailable(std::string(control), false);
  }
}

}
