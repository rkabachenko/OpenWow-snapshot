#pragma once

#include "openwow/ui/runtime/lua/lua_composition.h"

#include <memory>

namespace openwow::ui::game {
class LootLuaAdapter;
[[nodiscard]] lua::NativeBindingCatalog LootNativeBindingCatalog(
    std::shared_ptr<LootLuaAdapter> adapter);
}
