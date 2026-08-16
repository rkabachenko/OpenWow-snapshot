#pragma once

#include "openwow/ui/game/addon_runtime_loader.h"

#include <functional>
#include <string>
#include <string_view>

namespace openwow::core {
struct MD5Context;
}
namespace openwow::game {
class CursorSurface;
class WorldSession;
}
namespace openwow::vfs {
class VirtualFileSystem;
}

namespace openwow::ui::game {
class GameUIManager;

namespace runtime {

class WorldUiRuntimeHost final {
 public:
  explicit WorldUiRuntimeHost(GameUIManager& owner) noexcept;

  [[nodiscard]] bool Initialize(const openwow::vfs::VirtualFileSystem* vfs,
                                openwow::game::WorldSession* session,
                                openwow::game::CursorSurface& cursor);
  [[nodiscard]] bool LoadDefaultUI(
      std::function<void(float)> progress_callback = {},
      UiLoadStatusSink* status_sink = nullptr);
  [[nodiscard]] bool LoadToc(const std::string& toc_path,
                             UiLoadStatusSink* status_sink = nullptr,
                             TocLoadProgress* progress = nullptr,
                             std::string_view addon_name = {},
                             openwow::core::MD5Context* digest = nullptr);
  void SaveSavedVariables();
  void Shutdown();

  [[nodiscard]] bool initialized() const noexcept;
  [[nodiscard]] bool loaded() const noexcept;
  [[nodiscard]] const AddonRuntimeIdentity& persistence_identity() const noexcept {
    return persistence_identity_;
  }

 private:
  GameUIManager& owner_;
  AddonRuntimeIdentity persistence_identity_;
};

}
}
