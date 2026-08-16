#pragma once

#include "openwow/ui/surfaces/game/runtime/world_ui_lifecycle.h"

namespace openwow::game {
class WorldSession;
}

namespace openwow::ui::game {

class WorldUiSessionCommandAdapter final {
 public:
  void BindSession(openwow::game::WorldSession* session) noexcept {
    session_ = session;
  }

  [[nodiscard]] std::optional<openwow::game::ObjectGuid>
  CurrentWorldUiSelection() const;
  void SetWorldUiSelection(openwow::game::ObjectGuid selection);
  void EnableWorldUiVoice(WorldUiVoiceSettings settings);

 private:
  openwow::game::WorldSession* session_{nullptr};
};

}
