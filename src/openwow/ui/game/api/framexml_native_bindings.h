#pragma once

#include "openwow/ui/runtime/lua/lua_composition.h"

namespace openwow::ui::game::detail {

[[nodiscard]] openwow::ui::lua::NativeBindingCatalog
FrameXmlClientNativeBindings();
[[nodiscard]] openwow::ui::lua::NativeBindingCatalog
FrameXmlGameplayNativeBindings();

}
