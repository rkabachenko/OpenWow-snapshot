#pragma once

#include <vector>

namespace openwow::ui::lua {
struct NativeBindingCatalog;
}

namespace openwow::ui::game::detail {

[[nodiscard]] std::vector<openwow::ui::lua::NativeBindingCatalog>
FrameXmlCoreNativeBindingSources();

}
