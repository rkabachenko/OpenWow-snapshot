#pragma once

#include "openwow/ui/runtime/lua/lua_composition.h"

#include <memory>

namespace openwow::ui::game {
class AuctionLuaAdapter;
[[nodiscard]] lua::NativeBindingCatalog AuctionNativeBindingCatalog(
    std::shared_ptr<AuctionLuaAdapter> adapter);
}
