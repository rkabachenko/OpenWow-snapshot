#pragma once

#include "openwow/ui/framexml/ui_frame.h"

#include <string_view>

namespace openwow::ui::framexml {

inline std::string_view EffectiveAnchorPoint(const UiAnchor& anchor) {
  return anchor.point.empty() ? std::string_view{"CENTER"}
                              : std::string_view{anchor.point};
}

inline std::string_view EffectiveAnchorRelativePoint(const UiAnchor& anchor) {
  return anchor.relative_point.empty() ? EffectiveAnchorPoint(anchor)
                                       : std::string_view{anchor.relative_point};
}

}
