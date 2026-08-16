#pragma once

#include <cstdint>

namespace openwow::game {

enum class NativeWorldUiSurface : std::uint8_t {
  Chat,
  Loot,
  Tooltip,
  Minimap,
};

[[nodiscard]] constexpr bool ShouldRunNativeWorldUiSurface(
    const bool stock_frame_xml_loaded,
    const NativeWorldUiSurface surface) noexcept {
  if (!stock_frame_xml_loaded) {
    return true;
  }

  return surface == NativeWorldUiSurface::Tooltip ||
         surface == NativeWorldUiSurface::Minimap;
}

}
