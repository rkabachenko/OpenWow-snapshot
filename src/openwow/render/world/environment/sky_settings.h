#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

namespace openwow::render {

struct SkyRenderSettings {
  int cloud_lod{0};
  bool sun_glare_enabled{true};
};

using SkySettingsProvider = std::function<SkyRenderSettings()>;

[[nodiscard]] int ClampSkyCloudLod(int value);
[[nodiscard]] int ParseSkyCloudLodValue(std::string_view value);

[[nodiscard]] bool ParseSkySunGlareEnabled(std::string_view value);

}
