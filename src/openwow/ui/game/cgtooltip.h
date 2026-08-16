#pragma once

#include "openwow/ui/game/tooltip_builders.h"
#include "openwow/ui/game/tooltip_formatter.h"
#include "openwow/ui/game/tooltip_lua_adapter.h"
#include "openwow/ui/game/tooltip_runtime.h"
#include "openwow/ui/game/tooltip_types.h"

namespace openwow::ui::game {

int SpellAuraArray_GetElement(void* array, int index);
std::uint32_t FrameStackInfoArray_Add(std::uint32_t count,
                                      const FrameStackInfo* source);
int ItemTooltip_GetSubClassFlag(void* item_data);
int CGTooltip_ShowItemPending(void* tooltip);
std::uint8_t CGTooltip_GetAuraField13(void* unit_data, int aura_index);
std::uint8_t CGTooltip_GetAuraField14(void* unit_data, int aura_index);

}
