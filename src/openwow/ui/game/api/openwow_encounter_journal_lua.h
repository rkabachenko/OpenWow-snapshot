#pragma once

#include "openwow/ui/runtime/lua/lua_composition.h"

namespace openwow::ui::game {

// Additive OpenWoW API used by the modern encounter-journal AddOn. This is
// deliberately separate from the build 12340 retail global namespace.
lua::NativeBindingCatalog EncounterJournalNativeBindingCatalog();

}
