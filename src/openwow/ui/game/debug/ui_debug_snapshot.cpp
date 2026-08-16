#include "openwow/ui/game/game_ui_manager.h"

#include "openwow/ui/game/debug/lua_ui_debug_adapter.h"
#include "openwow/ui/game/runtime/render/ui_compositor.h"

#include <algorithm>
#include <unordered_map>

namespace openwow::ui::game {

debug::UiDebugSnapshot GameUIManager::BuildDebugSnapshot(
    const debug::UiDebugQuery& query) {
  debug::UiDebugSnapshot snapshot;
  snapshot.initialized = is_initialized();
  snapshot.loaded = is_loaded();
  snapshot.retained_frames = frame_store_.size();
  if (!snapshot.initialized) return snapshot;

  retained_layout_.SolveIfDirty();
  if (frame_traversal_index_.order_dirty()) {
    frame_traversal_index_.Rebuild(root_scale(), screen_height());
  }

  const auto render = frame_traversal_index_.render_snapshot();
  const auto input = frame_traversal_index_.input_snapshot();
  snapshot.traversal_generation = frame_traversal_index_.metrics().generation;
  snapshot.compositor_generation =
      compositor_ != nullptr ? compositor_->last_generation() : 0U;
  snapshot.render_candidates = render.size();
  snapshot.input_candidates = input.size();
  const std::size_t limit =
      std::clamp<std::size_t>(query.max_results, 1U, 4096U);

  if (query.x_pixels.has_value() && query.y_pixels.has_value()) {
    debug::UiDebugPointResult point;
    point.x_pixels = *query.x_pixels;
    point.y_pixels = *query.y_pixels;
    const float x = static_cast<float>(point.x_pixels);
    const float y = static_cast<float>(point.y_pixels);
    point.hit_target =
        frame_traversal_index_.HitTarget(x, y, screen_height());
    for (const auto& entry : frame_traversal_index_.FrameStackAt(
             x, y, true, screen_height())) {
      if (point.frame_stack.size() >= limit) {
        point.stack_truncated = true;
        break;
      }
      point.frame_stack.push_back({
          .key = entry.key,
          .strata = entry.strata,
          .level = entry.level,
          .mouse_enabled = entry.mouse_enabled,
          .locally_visible = entry.visible,
          .effectively_visible =
              frame_traversal_index_.IsEffectivelyVisible(entry.key),
      });
    }
    snapshot.point = std::move(point);
  }

  std::unordered_map<std::string, std::int64_t> render_ranks;
  std::unordered_map<std::string, std::int64_t> input_ranks;
  render_ranks.reserve(render.size());
  input_ranks.reserve(input.size());
  for (std::size_t rank = 0; rank < render.size(); ++rank) {
    render_ranks.emplace(render[rank].key, static_cast<std::int64_t>(rank));
  }
  for (std::size_t rank = 0; rank < input.size(); ++rank) {
    input_ranks.emplace(input[rank].key, static_cast<std::int64_t>(rank));
  }

  const auto keys = frame_store_.registration_order();
  const auto matches = [&](const std::string& key,
                           const openwow::ui::framexml::UiFrame& frame,
                           const bool exact_only) {
    if (query.selector.empty()) return !exact_only;
    if (exact_only) {
      return key == query.selector || frame.LuaName() == query.selector;
    }
    return key.find(query.selector) != std::string::npos ||
           frame.LuaName().find(query.selector) != std::string_view::npos;
  };

  bool exact_match_found = false;
  for (int pass = 0; pass < 2; ++pass) {
    const bool exact_only = pass == 0;
    if (query.selector.empty() && exact_only) continue;
    if (pass == 1 && exact_match_found) break;

    for (const auto& key : keys) {
      const auto* frame = frame_store_.FindFrame(key);
      if (frame == nullptr || !matches(key, *frame, exact_only)) continue;
      exact_match_found = exact_match_found || exact_only;
      ++snapshot.matched_frames;
      if (snapshot.nodes.size() >= limit) {
        snapshot.truncated = true;
        continue;
      }

      debug::UiDebugNode node;
      node.key = key;
      node.lua_name = std::string(frame->LuaName());
      node.kind = frame->kind;
      node.parent = frame->parent;
      node.draw_layer = frame->draw_layer;
      node.locally_visible = frame->visible;
      node.draw_layer_enabled = frame->runtime_draw_layer_enabled;
      node.effectively_visible =
          frame_traversal_index_.IsEffectivelyVisible(key);
      if (const auto found = retained_layout_.rects().find(key);
          found != retained_layout_.rects().end()) {
        node.rect = found->second;
      }
      if (const auto found = render_ranks.find(key);
          found != render_ranks.end()) {
        node.render_rank = found->second;
      }
      if (const auto found = input_ranks.find(key);
          found != input_ranks.end()) {
        node.input_rank = found->second;
      }
      node.submitted_last_frame =
          compositor_ != nullptr && compositor_->WasSubmittedLastFrame(key);

      if (query.include_ancestors) {
        std::string ancestor_key = frame->parent;
        for (std::size_t depth = 0; depth < 64U && !ancestor_key.empty(); ++depth) {
          const auto* ancestor = frame_store_.FindFrame(ancestor_key);
          if (ancestor == nullptr) break;
          node.ancestors.push_back({ancestor_key, ancestor->parent,
                                    ancestor->visible,
                                    ancestor->runtime_draw_layer_enabled});
          ancestor_key = ancestor->parent;
        }
      }

      if (query.include_lua) {
        const auto ref = frame_store_.FindLuaRef(key);
        if (ref.has_value()) {
          node.lua = debug::ReadLuaUiDebugState(lua_state(), *ref);
        }
      }
      snapshot.nodes.push_back(std::move(node));
    }
  }
  return snapshot;
}

void GameUIManager::SetDebugSubmissionReceiptsEnabled(const bool enabled) {
  if (compositor_ != nullptr) {
    compositor_->SetDebugSubmissionReceiptsEnabled(enabled);
  }
}

}
