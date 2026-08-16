#pragma once

#include <optional>
#include <vector>

namespace openwow::ui::display::platform {

struct DisplayMode final {
  int width{0};
  int height{0};
  int bits_per_pixel{0};
  int refresh_rate{0};
};

struct ScreenResolution final {
  int width{0};
  int height{0};
};

namespace detail {

[[nodiscard]] std::vector<DisplayMode>
NormalizeI386GllDisplayModes(std::vector<DisplayMode> modes);

}

[[nodiscard]] std::vector<DisplayMode> AvailableDisplayModes();
[[nodiscard]] std::optional<DisplayMode> CurrentDisplayMode();
[[nodiscard]] ScreenResolution DefaultScreenResolution();
[[nodiscard]] std::vector<ScreenResolution>
BuildFullscreenResolutionCatalog(bool widescreen_enabled,
                                 bool include_640x480_fallback);

}
