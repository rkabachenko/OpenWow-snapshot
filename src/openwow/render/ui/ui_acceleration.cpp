#include "openwow/render/ui/ui_acceleration.h"

#include "openwow/core/decimal_parse.h"
#include "openwow/core/console.h"
#include "openwow/core/gxcvar.h"

namespace openwow::render {

namespace {

UiFasterDevicePathState BuildRuntimeUiFasterDevicePathState() {
  const auto* hardware_info =
      openwow::core::ida::GetDetectedHardwareInfoIfReady();
  if (hardware_info == nullptr) {
    return {};
  }

  UiFasterDevicePathState device_path;
  device_path.has_detected_hardware = true;
  device_path.is_intel_vendor = hardware_info->vendor_id == 0x8086u;
  device_path.has_video_profile = hardware_info->has_video_profile;
  device_path.video_profile_disables_texture_atlas =
      hardware_info->has_video_profile
      && hardware_info->video_profile_disables_texture_atlas;
  return device_path;
}

void ReportUiFasterResult(const UiFasterCallbackResult& result) {
  if (result.emitted_texture_atlas_disabled_message) {
    openwow::core::ida::ConsoleAddLine(
        "Texture atlas disabled.",
        openwow::core::ida::COLOR_DEFAULT);
  }
}

bool UiFasterValidationCallback(const std::string&,
                                const std::string&,
                                const std::string& new_value) {
  const auto result = ApplyUiFasterMode(
      openwow::core::ParseSignedDecimalLikeSub76F0D0(new_value));
  return result.accepted;
}

}

UiFasterCallbackResult ResolveUiFasterCallback(
    std::uint32_t requested_mode,
    const UiFasterDevicePathState& device_path) {
  UiFasterCallbackResult result;
  result.requested_mode = requested_mode;
  if (requested_mode > 3u) {
    return result;
  }

  std::uint32_t effective_mode = requested_mode;
  if ((effective_mode & 1u) != 0u && device_path.has_detected_hardware) {
    if (!device_path.has_video_profile && device_path.is_intel_vendor) {
      effective_mode &= ~1u;
      result.emitted_texture_atlas_disabled_message = true;
    } else if (device_path.has_video_profile
               && device_path.video_profile_disables_texture_atlas) {
      effective_mode &= ~1u;
      result.emitted_texture_atlas_disabled_message = true;
    }
  }

  result.accepted = true;
  result.effective_mode = effective_mode;
  result.simple_ui_fast_path_enabled = ((effective_mode >> 1u) & 1u) != 0u;
  return result;
}

UiFasterCallbackResult ApplyUiFasterMode(std::uint32_t requested_mode) {
  const auto result = ResolveUiFasterCallback(
      requested_mode, BuildRuntimeUiFasterDevicePathState());
  ReportUiFasterResult(result);
  return result;
}

UiFasterCallbackResult ApplyCurrentUiFasterCVar(
    openwow::ui::game::CVarSystem& cvars) {
  if (!cvars.Exists("UIFaster")) {
    return {};
  }

  return ApplyUiFasterMode(
      openwow::core::ParseSignedDecimalLikeSub76F0D0(cvars.GetCVar("UIFaster")));
}

void RegisterUiFasterCVarCallback(openwow::ui::game::CVarSystem& cvars) {
  cvars.SetValidationCallback("UIFaster", UiFasterValidationCallback);
}

}
