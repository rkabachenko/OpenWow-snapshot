#include "openwow/render/world/environment/sky_settings.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <limits>
#include <string>

namespace openwow::render {

namespace {

bool ParseSscanfStyleInt(std::string_view value, int& out) {
  std::string owned(value);
  char* end = nullptr;
  errno = 0;
  const long parsed = std::strtol(owned.c_str(), &end, 10);
  if (end == owned.c_str()) {
    out = 0;
    return false;
  }

  if (parsed < static_cast<long>(std::numeric_limits<int>::min())) {
    out = std::numeric_limits<int>::min();
  } else if (parsed > static_cast<long>(std::numeric_limits<int>::max())) {
    out = std::numeric_limits<int>::max();
  } else {
    out = static_cast<int>(parsed);
  }
  return true;
}

}

int ClampSkyCloudLod(const int value) {
  return std::clamp(value, 0, 3);
}

int ParseSkyCloudLodValue(const std::string_view value) {
  int parsed = 0;
  (void)ParseSscanfStyleInt(value, parsed);
  return ClampSkyCloudLod(parsed);
}

bool ParseSkySunGlareEnabled(const std::string_view value) {
  int parsed = 0;
  return ParseSscanfStyleInt(value, parsed) && parsed != 0;
}

}
