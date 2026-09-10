#include "glue_host/presentation_settings.h"

#include "openwow/ui/game/cvar_system.h"

#include <algorithm>
#include <cstdint>

namespace openwow::client {

openwow::render::api::RendererCreateInfo::PresentationConfig
BuildPresentationConfig(const openwow::ui::game::CVarSystem& cvars) {

#if defined(OPENWOW_PLATFORM_IOS)
  constexpr std::uint8_t maximum_frame_latency = 2u;
#else
  const std::uint8_t maximum_frame_latency = static_cast<std::uint8_t>(
      cvars.GetCVarBool("gxTripleBuffer") ? 3 : 2);
#endif
  return {
      .vsync = cvars.GetCVarBool("gxVSync"),
      .multisample = static_cast<std::uint8_t>(
          std::clamp(cvars.GetCVarInt("gxMultisample"), 1, 16)),
      .maximum_frame_latency = maximum_frame_latency,
      .flush_after_render = false,
  };
}

}
