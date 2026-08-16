#pragma once

#include "openwow/ui/runtime/lua/lua_composition.h"

#include <memory>

namespace openwow::ui::game {
class TradeLuaAdapter;
[[nodiscard]] lua::NativeBindingCatalog TradeNativeBindingCatalog(
    std::shared_ptr<TradeLuaAdapter> adapter);
}
