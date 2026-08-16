#pragma once

#include <vector>

namespace openwow::ui::lua {
struct NativeBindingCatalog;
}

namespace openwow::ui::lua::modules {
[[nodiscard]] std::vector<NativeBindingCatalog>
SharedFrameScriptTypeModules();
[[nodiscard]] std::vector<NativeBindingCatalog>
WorldWidgetSubtypeModules();

}
