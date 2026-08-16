#include "debug_ui_control_adapter.h"

#include "openwow/ui/game/debug/ui_debug_snapshot.h"
#include "openwow/ui/game/game_ui_manager.h"

#include <boost/json.hpp>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

namespace openwow::client {
namespace {

using openwow::debug::control::CapabilityResult;
using openwow::debug::control::DebugControlError;
using openwow::debug::control::SerializedJson;

boost::json::object EncodeAncestor(
    const openwow::ui::game::debug::UiDebugAncestor& ancestor) {
  return {{"key", ancestor.key},
          {"parent", ancestor.parent},
          {"locallyVisible", ancestor.locally_visible},
          {"drawLayerEnabled", ancestor.draw_layer_enabled}};
}

boost::json::object EncodeNode(
    const openwow::ui::game::debug::UiDebugNode& node) {
  boost::json::object encoded{
      {"key", node.key},
      {"luaName", node.lua_name},
      {"kind", node.kind},
      {"parent", node.parent},
      {"drawLayer", node.draw_layer},
      {"locallyVisible", node.locally_visible},
      {"drawLayerEnabled", node.draw_layer_enabled},
      {"effectivelyVisible", node.effectively_visible},
      {"renderRank", node.render_rank},
      {"inputRank", node.input_rank},
      {"submittedLastFrame", node.submitted_last_frame},
  };
  if (node.rect.has_value()) {
    encoded["rect"] = {
        {"x", node.rect->x}, {"y", node.rect->y},
        {"width", node.rect->width}, {"height", node.rect->height}};
  } else {
    encoded["rect"] = nullptr;
  }
  if (node.lua.has_value()) {
    encoded["lua"] = {
        {"name", node.lua->name},
        {"kind", node.lua->kind},
        {"parentKey", node.lua->parent_key},
        {"texture", node.lua->texture},
        {"locallyVisible", node.lua->locally_visible},
        {"effectivelyVisible", node.lua->effectively_visible}};
  }
  boost::json::array ancestors;
  ancestors.reserve(node.ancestors.size());
  for (const auto& ancestor : node.ancestors) {
    ancestors.push_back(EncodeAncestor(ancestor));
  }
  encoded["ancestors"] = std::move(ancestors);
  return encoded;
}

SerializedJson EncodeSnapshot(
    const openwow::ui::game::debug::UiDebugSnapshot& snapshot) {
  boost::json::array nodes;
  nodes.reserve(snapshot.nodes.size());
  for (const auto& node : snapshot.nodes) nodes.push_back(EncodeNode(node));
  boost::json::object root{
      {"initialized", snapshot.initialized},
      {"loaded", snapshot.loaded},
      {"traversalGeneration", snapshot.traversal_generation},
      {"compositorGeneration", snapshot.compositor_generation},
      {"retainedFrames", snapshot.retained_frames},
      {"renderCandidates", snapshot.render_candidates},
      {"inputCandidates", snapshot.input_candidates},
      {"matchedFrames", snapshot.matched_frames},
      {"truncated", snapshot.truncated},
      {"nodes", std::move(nodes)},
  };
  if (snapshot.point.has_value()) {
    boost::json::array stack;
    stack.reserve(snapshot.point->frame_stack.size());
    for (const auto& entry : snapshot.point->frame_stack) {
      stack.push_back({
          {"key", entry.key},
          {"strata", entry.strata},
          {"level", entry.level},
          {"mouseEnabled", entry.mouse_enabled},
          {"locallyVisible", entry.locally_visible},
          {"effectivelyVisible", entry.effectively_visible},
      });
    }
    root["point"] = {
        {"xPixels", snapshot.point->x_pixels},
        {"yPixels", snapshot.point->y_pixels},
        {"hitTarget", snapshot.point->hit_target},
        {"stackTruncated", snapshot.point->stack_truncated},
        {"frameStack", std::move(stack)},
    };
  } else {
    root["point"] = nullptr;
  }
  return {boost::json::serialize(root)};
}

}

struct DebugUiControlAdapter::Impl {
  struct Pending {
    openwow::debug::control::InspectUiRequest request;
    std::mutex mutex;
    std::condition_variable ready;
    std::optional<CapabilityResult<SerializedJson>> result;
    bool cancelled{false};
  };

  std::mutex queue_mutex;
  std::deque<std::shared_ptr<Pending>> queue;
  bool stopped{false};
};

DebugUiControlAdapter::DebugUiControlAdapter()
    : impl_(std::make_unique<Impl>()) {}

DebugUiControlAdapter::~DebugUiControlAdapter() { Stop(); }

CapabilityResult<SerializedJson> DebugUiControlAdapter::Inspect(
    const openwow::debug::control::RequestContext& context,
    const openwow::debug::control::InspectUiRequest& request) {
  auto pending = std::make_shared<Impl::Pending>();
  pending->request = request;
  {
    std::lock_guard lock(impl_->queue_mutex);
    if (impl_->stopped) {
      return DebugControlError{"unavailable", "UI inspector is stopped", true};
    }
    if (impl_->queue.size() >= 8U) {
      return DebugControlError{"busy", "UI inspector request queue is full", true};
    }
    impl_->queue.push_back(pending);
  }

  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(2);
  std::unique_lock lock(pending->mutex);
  while (!pending->result.has_value()) {
    if (context.is_cancellation_requested &&
        context.is_cancellation_requested()) {
      pending->cancelled = true;
      return DebugControlError{"cancelled", "UI inspection was cancelled", false};
    }
    if (pending->ready.wait_until(lock, deadline) == std::cv_status::timeout) {
      pending->cancelled = true;
      return DebugControlError{"timeout", "client thread did not service UI inspection", true};
    }
  }
  return std::move(*pending->result);
}

void DebugUiControlAdapter::Pump(openwow::ui::game::GameUIManager& game_ui) {
  std::deque<std::shared_ptr<Impl::Pending>> pending;
  {
    std::lock_guard lock(impl_->queue_mutex);
    pending.swap(impl_->queue);
  }
  for (const auto& request : pending) {
    {
      std::lock_guard lock(request->mutex);
      if (request->cancelled) continue;
    }
    const auto& source = request->request;
    const auto snapshot = game_ui.BuildDebugSnapshot({
        .selector = source.selector,
        .max_results = source.max_results,
        .include_lua = source.include_lua,
        .include_ancestors = source.include_ancestors,
        .x_pixels = source.x_pixels,
        .y_pixels = source.y_pixels,
    });
    auto encoded = CapabilityResult<SerializedJson>{EncodeSnapshot(snapshot)};
    {
      std::lock_guard lock(request->mutex);
      if (request->cancelled) continue;
      request->result = std::move(encoded);
    }
    request->ready.notify_one();
  }
}

void DebugUiControlAdapter::Stop() {
  std::deque<std::shared_ptr<Impl::Pending>> pending;
  {
    std::lock_guard lock(impl_->queue_mutex);
    if (impl_->stopped) return;
    impl_->stopped = true;
    pending.swap(impl_->queue);
  }
  for (const auto& request : pending) {
    {
      std::lock_guard lock(request->mutex);
      request->result = DebugControlError{
          "unavailable", "UI inspector stopped before servicing request", true};
    }
    request->ready.notify_one();
  }
}

}
