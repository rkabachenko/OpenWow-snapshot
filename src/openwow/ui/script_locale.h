#pragma once

#include "openwow/game/client_config.h"

#include <string>

namespace openwow::ui {

[[nodiscard]] inline std::string ScriptActiveLocaleName() {
  return openwow::game::ClientConfig::Get().GetLocale();
}

}
