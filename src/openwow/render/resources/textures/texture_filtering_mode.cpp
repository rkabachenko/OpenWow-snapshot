
#include "openwow/render/resources/textures/texture_filtering_mode.h"

#include "openwow/core/console.h"
#include "openwow/core/decimal_parse.h"
#include "openwow/render/backend/bgfx/retail_render_profile.h"
#include "openwow/ui/game/cvar_system.h"

#include <algorithm>
#include <string>

namespace openwow::render {

namespace {

constexpr char kTextureFilteringModeRangeError[] =
    "Texture filtering mode must be in range 0 to 6.";
constexpr std::size_t kTextureFilteringModeCount =
    sizeof(kFilterTierTable) / sizeof(kFilterTierTable[0]);

bool TextureFilteringModeValidationCallback(const std::string &, const std::string &,
                                            const std::string &new_value) {
  const std::int32_t parsed_value =
      static_cast<std::int32_t>(openwow::core::ParseSignedDecimalLikeSub76F0D0(new_value));
  if (parsed_value >= 0 && parsed_value < 6) {
    return true;
  }

  openwow::core::ida::ConsoleAddLine(kTextureFilteringModeRangeError,
                                     openwow::core::ida::COLOR_DEFAULT);
  return false;
}

}

TextureFilteringStartupState ResolveTextureFilteringStartupState(const uint32_t requested_mode,
                                                                 const TextureFilterCaps &caps) {
  TextureFilteringStartupState state;
  if (requested_mode >= kTextureFilteringModeCount) {
    return state;
  }

  state.requested_mode = requested_mode;
  state.requested_filter_tier = kFilterTierTable[requested_mode];
  state.requested_anisotropy = kAnisotropyTable[requested_mode];
  state.effective_filter_tier = state.requested_filter_tier;

  if (state.requested_filter_tier >= 4u && !caps.supports_tier4) {
    state.effective_filter_tier = 3u;
    state.effective_anisotropy = 1u;
    state.downgraded = true;
    return state;
  }

  if (state.requested_filter_tier >= 5u && !caps.supports_tier5) {
    state.effective_filter_tier = 4u;
    state.effective_anisotropy = 1u;
    state.downgraded = true;
    return state;
  }

  if (state.effective_filter_tier == 5u) {
    state.effective_anisotropy = std::min(state.requested_anisotropy, caps.max_anisotropy);
  }

  return state;
}

TextureFilterCaps QueryLiveTextureFilterCaps() {
  const WotlkRendererCapabilities device_caps =
      QueryWotlkRendererCapabilities();
  return TextureFilterCaps{
      .supports_tier4 = device_caps.supports_trilinear_filtering,
      .supports_tier5 = device_caps.supports_anisotropic_filtering,
      .max_anisotropy =
          device_caps.supports_anisotropic_filtering ? device_caps.max_anisotropy : 0u,
  };
}

void RegisterTextureFilteringModeCVarCallback(openwow::ui::game::CVarSystem &cvars) {
  cvars.SetValidationCallback("textureFilteringMode", TextureFilteringModeValidationCallback);
}

}
