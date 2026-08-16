#pragma once

namespace openwow::ui::game {

class WorldUiLifecycleCommandPort {
 public:
  virtual ~WorldUiLifecycleCommandPort() = default;
  virtual void RequestWorldUiReload() = 0;
};

}
