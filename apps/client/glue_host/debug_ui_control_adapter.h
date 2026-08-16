#pragma once

#include "openwow/debug/control/debug_control_server.h"

#include <memory>

namespace openwow::ui::game {
class GameUIManager;
}

namespace openwow::client {

class DebugUiControlAdapter final {
 public:
  DebugUiControlAdapter();
  ~DebugUiControlAdapter();
  DebugUiControlAdapter(const DebugUiControlAdapter&) = delete;
  DebugUiControlAdapter& operator=(const DebugUiControlAdapter&) = delete;

  [[nodiscard]] openwow::debug::control::CapabilityResult<
      openwow::debug::control::SerializedJson>
  Inspect(const openwow::debug::control::RequestContext& context,
          const openwow::debug::control::InspectUiRequest& request);

  void Pump(openwow::ui::game::GameUIManager& game_ui);
  void Stop();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}
