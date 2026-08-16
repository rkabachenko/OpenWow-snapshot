#pragma once

#include "openwow/ui/framexml/ui_frame.h"
#include "openwow/ui/framexml/xml_script_cache.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct lua_State;

namespace openwow::game {
class MinimapSystem;
}
namespace openwow::render {
class WorldFrame;
}
namespace openwow::ui {
class MinimapSystem;
}

namespace openwow::ui::game::runtime {

class FrameInputRouter;
class FrameStore;
class FrameTraversalIndex;
class RetainedLayout;

void PublishFrameAnchorsToLua(lua_State* lua, int frame_index,
                              const openwow::ui::framexml::UiFrame& frame);

class FrameMaterializer final {
 public:
  struct LoadCheckpoint {
    std::unordered_map<
        std::string, std::vector<openwow::ui::framexml::UiFrame>> templates;
    std::unordered_map<std::string, int> default_declarations;
  };
  struct Metrics {
    std::uint64_t tree_plans{0};
    std::uint64_t tree_source_nodes{0};
    std::uint64_t tree_inherited_nodes{0};
    std::uint64_t tree_plan_nodes{0};
    std::uint64_t tree_plan_node_peak{0};
    std::uint64_t finalize_dispatches{0};
    std::uint64_t finalize_stack_peak{0};
    std::uint64_t bindings_created{0};
    std::uint64_t texture_bindings_created{0};
    std::uint64_t font_string_bindings_created{0};
    std::uint64_t default_tree_plans{0};
    std::uint64_t default_tree_source_nodes{0};
    std::uint64_t default_tree_inherited_nodes{0};
    std::uint64_t default_tree_plan_nodes{0};
    std::uint64_t default_tree_plan_node_peak{0};
  };

  struct Dependencies {
    FrameStore& frames;
    RetainedLayout& layout;
    FrameTraversalIndex& traversal;
    FrameInputRouter& input;
    openwow::ui::MinimapSystem& minimap_state;
    openwow::game::MinimapSystem& minimap_content;
    openwow::render::WorldFrame& world_frame;
    std::function<void(const std::string&, std::uint8_t, std::int32_t)>
        register_on_update;
  };

  explicit FrameMaterializer(Dependencies dependencies);

  void BindLuaState(lua_State* state) noexcept;
  void ClearLuaState();
  void Reset();

  [[nodiscard]] int CreateFrame(lua_State* state);
  [[nodiscard]] int InstantiateFrameTree(
      openwow::ui::framexml::UiFrame root_frame,
      std::vector<openwow::ui::framexml::UiFrame> local_children = {},
      int explicit_parent_index = 0,
      bool suppress_named_parent_lookup = false,
      bool push_root_result = false,
      bool record_default_declarations = false);
  [[nodiscard]] int CreateTrackedFrameBinding(
      const openwow::ui::framexml::UiFrame& frame,
      int explicit_parent_index = 0,
      bool suppress_named_parent_lookup = false);
  [[nodiscard]] int AdoptRuntimeFrame(
      const openwow::ui::framexml::UiFrame& frame);

  void AddTemplates(
      std::unordered_map<std::string,
                         std::vector<openwow::ui::framexml::UiFrame>> templates);
  [[nodiscard]] std::size_t template_count() const noexcept;
  [[nodiscard]] std::size_t template_node_count() const noexcept;

  void BeginDefaultFrameXmlLoad();
  void EndDefaultFrameXmlLoad() noexcept;
  [[nodiscard]] LoadCheckpoint CaptureLoadCheckpoint() const;
  void RestoreLoadCheckpoint(LoadCheckpoint checkpoint);
  [[nodiscard]] bool loading_default_frame_xml() const noexcept;
  [[nodiscard]] bool HasDefaultDeclaration(std::string_view name) const;
  [[nodiscard]] bool IsDefaultDeclarationIdentity(std::string_view name,
                                                  int lua_ref) const;
  [[nodiscard]] bool HasCachedInlineHandler(
      lua_State* state,
      const openwow::ui::framexml::ScriptHandler& handler) const;

  [[nodiscard]] const Metrics& metrics() const noexcept { return metrics_; }
  void ResetMetrics() noexcept { metrics_ = {}; }

 private:
  using TemplateMap = std::unordered_map<
      std::string, std::vector<openwow::ui::framexml::UiFrame>>;

  void ApplyFullFrameSetup(const openwow::ui::framexml::UiFrame& frame);
  void WireScriptHandlers(const openwow::ui::framexml::UiFrame& frame,
                          int lua_ref);
  void SyncRuntimeMetadata(int frame_index,
                           openwow::ui::framexml::UiFrame& frame);

  lua_State* lua_{nullptr};
  Dependencies dependencies_;
  TemplateMap templates_;
  std::unordered_map<std::string, int> default_declarations_;
  openwow::ui::framexml::XmlScriptCache script_cache_;
  Metrics metrics_;
  bool loading_default_frame_xml_{false};
};

}
