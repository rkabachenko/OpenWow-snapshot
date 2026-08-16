#include "openwow/ui/game/runtime/world_ui_runtime_context.h"

#include "openwow/ui/game/runtime/frame_store.h"
#include "openwow/ui/game/runtime/framexml_runtime_loader.h"
#include "openwow/ui/game/runtime/retained_layout.h"
#include "openwow/ui/game/runtime/world_lua_runtime.h"
#include "openwow/ui/game/runtime/world_ui_runtime_host.h"
#include "openwow/ui/frame_script_events.h"

#include <lua.hpp>

#include <utility>

namespace openwow::ui::game::runtime {

WorldUiRuntimeContext::WorldUiRuntimeContext(Ports ports) noexcept
    : ports_(std::move(ports)) {}

AddonRuntimeLoader* WorldUiRuntimeContext::addon_runtime_loader() const noexcept {
  return ports_.frame_xml_loader.addon_runtime_loader();
}

const AddonRuntimeIdentity& WorldUiRuntimeContext::addon_runtime_identity() const noexcept {
  return ports_.runtime_host.persistence_identity();
}

lua_State* WorldUiRuntimeContext::lua_state() const noexcept {
  return ports_.lua.state();
}

bool WorldUiRuntimeContext::is_loaded() const noexcept {
  return ports_.runtime_host.loaded();
}

bool WorldUiRuntimeContext::is_initialized() const noexcept {
  return ports_.runtime_host.initialized();
}

float WorldUiRuntimeContext::screen_width() const noexcept {
  return ports_.layout.viewport_width();
}

float WorldUiRuntimeContext::screen_height() const noexcept {
  return ports_.layout.viewport_height();
}

float WorldUiRuntimeContext::root_scale() const noexcept {
  return ports_.layout.root_scale();
}

void WorldUiRuntimeContext::SetRootScale(const float scale,
                                         const bool force) const {
  ports_.set_root_scale(scale, force);
}

void WorldUiRuntimeContext::RequestWorldUiReload() const {
  ports_.request_world_ui_reload();
}

void WorldUiRuntimeContext::NotifyFrameInputCategoryMutation(
    const std::string& frame_name, const bool reindex_only) const {
  ports_.notify_frame_mutation(frame_name, reindex_only);
}

bool WorldUiRuntimeContext::BuildFrameStackSnapshot(
    const bool show_hidden, TooltipFrameStackSnapshot* snapshot) const {
  return ports_.frame_stack_snapshot(show_hidden, snapshot);
}

void WorldUiRuntimeContext::DestroyNamedFrame(
    const std::string& frame_name) const {
  ports_.frames.DestroySubtree(frame_name);
}

WorldUiRuntimeContext* WorldUiRuntimeContext::FromLua(lua_State* state) noexcept {
  if (state == nullptr) return nullptr;
  lua_getfield(state, LUA_REGISTRYINDEX, kWorldUiRuntimeContextRegistryKey);
  auto* context =
      static_cast<WorldUiRuntimeContext*>(lua_touserdata(state, -1));
  lua_pop(state, 1);
  return context;
}

WorldUiRuntimeContext* WorldUiRuntimeContext::FromActiveLua() noexcept {
  return FromLua(
      openwow::ui::frame_script_events::FrameScript_GetLuaStateTyped());
}

}
