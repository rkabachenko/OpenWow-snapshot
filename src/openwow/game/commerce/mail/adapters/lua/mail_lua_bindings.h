#pragma once

#include "openwow/ui/runtime/lua/lua_composition.h"

#include <memory>

namespace openwow::ui::game {
class MailLuaAdapter;
[[nodiscard]] lua::NativeBindingCatalog MailNativeBindingCatalog(
    std::shared_ptr<MailLuaAdapter> adapter);
}
