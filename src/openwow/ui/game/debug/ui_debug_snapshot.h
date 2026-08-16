#pragma once

#include "openwow/ui/framexml/layout_resolver.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::ui::game::debug {

struct UiDebugQuery {
  std::string selector;
  std::size_t max_results{128U};
  bool include_lua{true};
  bool include_ancestors{true};
  std::optional<std::int32_t> x_pixels;
  std::optional<std::int32_t> y_pixels;
};

struct UiDebugStackEntry {
  std::string key;
  std::int32_t strata{0};
  std::int32_t level{0};
  bool mouse_enabled{false};
  bool locally_visible{false};
  bool effectively_visible{false};
};

struct UiDebugPointResult {
  std::int32_t x_pixels{0};
  std::int32_t y_pixels{0};
  std::string hit_target;
  bool stack_truncated{false};
  std::vector<UiDebugStackEntry> frame_stack;
};

struct UiDebugAncestor {
  std::string key;
  std::string parent;
  bool locally_visible{false};
  bool draw_layer_enabled{false};
};

struct UiDebugLuaState {
  std::string name;
  std::string kind;
  std::string parent_key;
  std::string texture;
  bool locally_visible{false};
  bool effectively_visible{false};
};

struct UiDebugNode {
  std::string key;
  std::string lua_name;
  std::string kind;
  std::string parent;
  std::string draw_layer;
  std::optional<openwow::ui::framexml::FrameRect> rect;
  std::optional<UiDebugLuaState> lua;
  std::vector<UiDebugAncestor> ancestors;
  std::int64_t render_rank{-1};
  std::int64_t input_rank{-1};
  bool locally_visible{false};
  bool draw_layer_enabled{false};
  bool effectively_visible{false};
  bool submitted_last_frame{false};
};

struct UiDebugSnapshot {
  std::uint64_t traversal_generation{0U};
  std::uint64_t compositor_generation{0U};
  std::size_t retained_frames{0U};
  std::size_t render_candidates{0U};
  std::size_t input_candidates{0U};
  std::size_t matched_frames{0U};
  bool initialized{false};
  bool loaded{false};
  bool truncated{false};
  std::optional<UiDebugPointResult> point;
  std::vector<UiDebugNode> nodes;
};

}
