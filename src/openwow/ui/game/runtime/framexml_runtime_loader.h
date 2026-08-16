#pragma once

#include "openwow/ui/framexml/framexml_parser.h"
#include "openwow/ui/framexml_font_registry.h"
#include "openwow/ui/game/addon_runtime_loader.h"
#include "openwow/ui/game/framescript/xml/frame_xml_loader.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::core {
struct MD5Context;
}
namespace openwow::vfs {
class VirtualFileSystem;
}

namespace openwow::ui::game {
class GameUIManager;

namespace runtime {

class FrameXmlRuntimeLoader final {
 public:
  explicit FrameXmlRuntimeLoader(GameUIManager& owner) noexcept;

  void BeginLifetime(const openwow::vfs::VirtualFileSystem* vfs);
  [[nodiscard]] bool LoadDefaultUI(
      std::function<void(float)> progress_callback = {},
      UiLoadStatusSink* status_sink = nullptr);
  [[nodiscard]] bool LoadToc(const std::string& toc_path,
                             UiLoadStatusSink* status_sink = nullptr,
                             TocLoadProgress* progress = nullptr,
                             std::string_view addon_name = {},
                             openwow::core::MD5Context* digest = nullptr);
  void Reset();

  [[nodiscard]] AddonRuntimeLoader* addon_runtime_loader() noexcept {
    return addon_runtime_loader_.get();
  }

  [[nodiscard]] bool post_bootstrap_complete() const noexcept {
    return post_bootstrap_complete_;
  }
  [[nodiscard]] bool default_ui_loaded() const noexcept {
    return default_ui_loaded_;
  }

 private:
  void ProcessXmlFrameGroup(const std::string& path,
                            const openwow::ui::framexml::ParseResult& result,
                            std::size_t group_index);
  void RegisterXmlFontDefinition(
      const std::string& path,
      const openwow::ui::FontDefinition& definition);
  void RegisterParsedFrameGroup(
      const std::string& path,
      const std::vector<openwow::ui::framexml::UiFrame>& frames);
  [[nodiscard]] bool CompletePostFrameXmlBootstrap(
      UiLoadStatusSink* status_sink);
  GameUIManager& owner_;
  FrameXmlLoader loader_;
  std::unique_ptr<AddonRuntimeLoader> addon_runtime_loader_;
  openwow::ui::FontDefinitionRegistry font_registry_;
  TocLoadProgress* active_progress_{nullptr};
  std::size_t groups_since_progress_pulse_{0};
  float last_progress_pulse_{0.0F};
  bool post_bootstrap_complete_{false};
  bool default_ui_loaded_{false};
  std::uint32_t post_bootstrap_count_{0};
};

}
}
