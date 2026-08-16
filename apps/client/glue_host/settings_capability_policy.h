#pragma once

namespace openwow::ui::game {
class CVarSystem;
}

namespace openwow::ui::glue {
class GlueWidgetRuntime;
}

namespace openwow::client {

struct ClientSettingsCapabilities {
  bool terrain_texture_detail{false};
  bool ground_clutter{false};
  bool texture_resolution{false};
  bool texture_filtering_tiers{false};
  bool hardware_audio_voices{false};
  bool software_hrtf{false};
};

void ApplySettingsCapabilityPolicy(
    openwow::ui::glue::GlueWidgetRuntime& widgets,
    openwow::ui::game::CVarSystem& cvars,
    ClientSettingsCapabilities capabilities);

}
