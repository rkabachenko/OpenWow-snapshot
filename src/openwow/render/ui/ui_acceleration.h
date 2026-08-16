#pragma once

#include <cstdint>

#include "openwow/ui/game/cvar_system.h"

namespace openwow::render {

struct UiFasterDevicePathState {
  bool has_detected_hardware{false};
  bool is_intel_vendor{false};
  bool has_video_profile{false};
  bool video_profile_disables_texture_atlas{false};
};

struct UiFasterCallbackResult {
  bool accepted{false};
  std::uint32_t requested_mode{0};
  std::uint32_t effective_mode{0};
  bool simple_ui_fast_path_enabled{false};
  bool emitted_texture_atlas_disabled_message{false};
};

UiFasterCallbackResult ResolveUiFasterCallback(
    std::uint32_t requested_mode,
    const UiFasterDevicePathState& device_path);

UiFasterCallbackResult ApplyUiFasterMode(std::uint32_t requested_mode);
UiFasterCallbackResult ApplyCurrentUiFasterCVar(
    openwow::ui::game::CVarSystem& cvars);

void RegisterUiFasterCVarCallback(openwow::ui::game::CVarSystem& cvars);

}
