
#pragma once

#include "openwow/game/world_session_fwd.h"

struct lua_State;

namespace openwow::ui::game {

void InitializeSlashCommandHandlers(lua_State* L,
                                    openwow::game::WorldSession* session);

}
