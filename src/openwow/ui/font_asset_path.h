#pragma once

#include "openwow/foundation/text/ascii.h"

#include <string>
#include <string_view>

namespace openwow::ui {

[[nodiscard]] inline std::string ResolveBuiltInFontAssetPath(
    const std::string_view name) {
  if (openwow::text::EqualsIgnoreCaseAscii(name, "FRIZQT__")) {
    return "Fonts/FRIZQT__.TTF";
  }
  if (openwow::text::EqualsIgnoreCaseAscii(name, "ARIALN")) {
    return "Fonts/ARIALN.TTF";
  }
  if (openwow::text::EqualsIgnoreCaseAscii(name, "MORPHEUS")) {
    return "Fonts/MORPHEUS.TTF";
  }
  if (openwow::text::EqualsIgnoreCaseAscii(name, "SKURRI")) {
    return "Fonts/SKURRI.TTF";
  }
  return {};
}

}
