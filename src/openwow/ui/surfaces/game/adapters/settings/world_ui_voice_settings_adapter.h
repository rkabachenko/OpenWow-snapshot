#pragma once

#include "openwow/ui/surfaces/game/runtime/world_ui_lifecycle.h"

namespace openwow::ui::game {

class WorldUiVoiceSettingsAdapter final {
 public:
  [[nodiscard]] WorldUiVoiceSettings CurrentWorldUiVoiceSettings()
      const;
};

}
