#include "openwow/render/world/environment/sky_cvar_callbacks.h"

#include "openwow/debug/diagnostics/debug_console.h"
#include "openwow/world/environment/day_night.h"
#include "openwow/render/world/environment/sky_settings.h"
#include "openwow/ui/game/cvar_system.h"

namespace openwow::render {

namespace {

constexpr char kSkySunGlareConsoleHelp[] = "0, 1";
constexpr int kSkyConsoleCommandCategory = 1;

}

void RegisterSkyCVarDefaults(openwow::ui::game::CVarSystem& cvars) {
  using openwow::ui::game::CVarFlags;

  cvars.RegisterCVar("SkyCloudLOD", "0", CVarFlags::Archive, "Sky cloud texture LOD", 0.0f,
                     3.0f);
  cvars.SetValidationCallback("SkyCloudLOD", CVar_SkyCloudLOD_Callback);

  openwow::debug::DebugConsole::Get().RegisterRawCommand(
      "SkySunGlare", kSkySunGlareConsoleHelp,
      [](const std::string_view raw_args) -> std::string {
        (void)openwow::game::DayNight_OnSunGlareToggle(
            0, std::string(raw_args).c_str());
        return {};
      },
      kSkySunGlareConsoleHelp, kSkyConsoleCommandCategory);

  ApplyCurrentSkyCVarState(cvars);
}

void ApplyCurrentSkyCVarState(openwow::ui::game::CVarSystem& cvars) {
  (void)openwow::game::DayNight_ApplySkyCloudLod(
      ParseSkyCloudLodValue(cvars.GetCVar("SkyCloudLOD")), false);
}

bool CVar_SkyCloudLOD_Callback(const std::string&, const std::string&,
                               const std::string& newValue) {
  const int lod = ParseSkyCloudLodValue(newValue);
  (void)openwow::game::DayNight_ApplySkyCloudLod(lod, true);
  return true;
}

}
