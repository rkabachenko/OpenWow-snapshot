#pragma once

#include "os_platform.h"

#include <cstdint>
#include <string_view>

namespace openwow::platform::detail {

struct LegacyWindowsVersionProbe {
  std::uint32_t platform_id = 0;
  std::uint32_t major_version = 0;
  std::uint32_t minor_version = 0;

  char windows9x_csd_marker = '\0';
};

[[nodiscard]] int ClassifyLegacyWindowsVersion(const LegacyWindowsVersionProbe &probe);
[[nodiscard]] std::string_view LookupLegacyOsVersionString(int version_id);
[[nodiscard]] MessageBoxButtons DecodeLegacyMessageBoxButtons(int mode);
[[nodiscard]] int MapLegacyMessageBoxResult(int win32_result);

}
