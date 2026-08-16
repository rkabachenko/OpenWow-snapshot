
#pragma once

namespace openwow::game {

class CGObject_C;
class CGPlayer_C;

[[nodiscard]] bool CanInteractWithTarget(const CGPlayer_C& active_player,
                                         const CGObject_C& target);

}
