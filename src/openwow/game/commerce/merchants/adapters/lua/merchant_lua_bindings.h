#pragma once

#include "openwow/ui/runtime/lua/lua_composition.h"

#include <memory>

namespace openwow::ui::game {
class MerchantLuaAdapter;
[[nodiscard]] lua::NativeBindingCatalog MerchantNativeBindingCatalog(
    std::shared_ptr<MerchantLuaAdapter> adapter);
}
