#include "glue_host/presentation_settings.h"

#include "openwow/ui/game/cvar_system.h"

#include <algorithm>
#include <cstdint>

namespace openwow::client {

openwow::render::api::RendererCreateInfo::PresentationConfig
BuildPresentationConfig(const openwow::ui::game::CVarSystem& cvars) {

  return {
      .vsync = cvars.GetCVarBool("gxVSync"),
      .multisample = static_cast<std::uint8_t>(
          std::clamp(cvars.GetCVarInt("gxMultisample"), 1, 16)),
      .maximum_frame_latency = static_cast<std::uint8_t>(
          cvars.GetCVarBool("gxTripleBuffer") ? 3 : 2),
      .flush_after_render = false,
  };
}

}
