#pragma once

#include "openwow/ui/runtime/lua/lua_composition.h"

#include <vector>

namespace openwow::ui::lua {

struct RetailNativeBindingPlan final {
  std::vector<NativeBindingCatalog> before_frame_script_types;
  std::vector<NativeBindingCatalog> after_frame_script_types;
};

[[nodiscard]] RetailNativeBindingPlan ComposeRetailWorldNativeBindings(
    std::vector<NativeBindingCatalog> sources);
[[nodiscard]] RetailNativeBindingPlan ComposeRetailGlueNativeBindings(
    std::vector<NativeBindingCatalog> sources);

}
