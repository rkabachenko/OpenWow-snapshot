#pragma once

#include "openwow/game/object_guid.h"

struct lua_State;

namespace openwow::ui::game::frame_api {

void CancelPendingDressUpItemTemplates(lua_State* lua);
void RefreshBoundUnitModelState(lua_State* lua, int frame_index,
                                openwow::game::ObjectGuid guid);

}
