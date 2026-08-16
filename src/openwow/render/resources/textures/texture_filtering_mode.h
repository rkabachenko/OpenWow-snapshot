#pragma once

#include <array>
#include <cstdint>

namespace openwow::ui::game {
class CVarSystem;
}

namespace openwow::render {

inline constexpr uint32_t kFilterTierTable[6] = {3, 4, 5, 5, 5, 5};

inline constexpr uint32_t kAnisotropyTable[6] = {1, 1, 2, 4, 8, 16};

struct TextureFilterCaps {
  bool supports_tier4 = true;
  bool supports_tier5 = true;
  uint32_t max_anisotropy = 16;
};

struct TextureFilteringStartupState {
  uint32_t requested_mode = 0;
  uint32_t requested_filter_tier = 3;
  uint32_t requested_anisotropy = 1;
  uint32_t effective_filter_tier = 3;
  uint32_t effective_anisotropy = 1;
  bool downgraded = false;
};

[[nodiscard]] TextureFilteringStartupState
ResolveTextureFilteringStartupState(uint32_t requested_mode, const TextureFilterCaps &caps);

[[nodiscard]] TextureFilterCaps QueryLiveTextureFilterCaps();

void RegisterTextureFilteringModeCVarCallback(openwow::ui::game::CVarSystem &cvars);

}
