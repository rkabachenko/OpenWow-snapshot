#pragma once

#include "openwow/ui/runtime/lua/lua_composition.h"

#include <memory>

namespace openwow::ui::game {
class ItemLuaAdapter;
[[nodiscard]] lua::NativeBindingCatalog ItemNativeBindingCatalog(
    std::shared_ptr<ItemLuaAdapter> adapter);
}
