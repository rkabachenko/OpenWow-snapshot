
#pragma once

#include "openwow/ui/widgets/script_object.h"

namespace openwow::ui {

[[nodiscard]] void* Script_FindNamedObjectByTypeTag(
    const char* name, widgets::ScriptObjectType requiredType) noexcept;

}
