#pragma once

namespace openwow::game {
class WorldSession;
}

namespace openwow::ui::game {

int GameUI_HandleTargetingEscape(
    openwow::game::WorldSession* session, const void* event_data);

}
