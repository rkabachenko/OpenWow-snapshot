#include "openwow/ui/game/runtime/frame_materializer.h"

#include "openwow/foundation/text/ascii.h"
#include "openwow/game/localization.h"
#include "openwow/ui/animation/animation_lua.h"
#include "openwow/ui/framexml/framexml_name_utils.h"
#include "openwow/ui/framexml/framexml_value_utils.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/framescript/core/frame_alpha.h"
#include "openwow/ui/game/framescript/core/frame_region_factory.h"
#include "openwow/ui/game/framescript/core/frame_runtime_identity.h"
#include "openwow/ui/game/framescript/core/lua_script_object_access.h"
#include "openwow/ui/game/framescript/widgets/edit_box_methods.h"
#include "openwow/ui/game/framescript/widgets/edit_box_state.h"
#include "openwow/ui/game/runtime/frame_input_router.h"
#include "openwow/ui/game/runtime/frame_store.h"
#include "openwow/ui/game/runtime/frame_template_expander.h"
#include "openwow/ui/game/runtime/frame_traversal_index.h"
#include "openwow/ui/game/runtime/retained_layout.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/runtime/lua/lua_binding.h"
#include "openwow/ui/widgets/script_object.h"
#include "openwow/ui/widgets/simple_frame.h"
#include "openwow/ui/widgets/simple_game_tooltip.h"
#include "openwow/ui/widgets/simple_minimap.h"
#include "openwow/ui/widgets/simple_world_frame.h"

extern "C" {
#include <lua.hpp>
}

#include <algorithm>
#include <iterator>
#include <memory>
#include <optional>
#include <utility>

namespace openwow::ui::game::runtime {
namespace {

using UiFrame = openwow::ui::framexml::UiFrame;

std::optional<std::string> StringField(lua_State* state, int index,
                                       const char* field) {
  index = lua_absindex(state, index);
  lua_getfield(state, index, field);
  std::optional<std::string> result;
  if (const char* value = lua_tostring(state, -1); value != nullptr) {
    result = value;
  }
  lua_pop(state, 1);
  return result;
}

std::optional<std::string> FrameKey(lua_State* state, int index) {
  index = lua_absindex(state, index);
  if (lua_isstring(state, index) != 0) {
    const char* value = lua_tostring(state, index);
    return value != nullptr && value[0] != '\0'
               ? std::optional<std::string>(value)
               : std::nullopt;
  }
  if (lua_istable(state, index) == 0) {
    return std::nullopt;
  }
  auto key = StringField(state, index, frame_api::kLuaFrameRuntimeKeyField);
  if (key.has_value() && !key->empty()) {
    return key;
  }
  key = StringField(state, index, "__ow_name");
  return key.has_value() && !key->empty() ? key : std::nullopt;
}

std::string NearestStoredLuaName(const FrameStore& frames,
                                 std::string runtime_key) {
  for (int depth = 0; depth < 64 && !runtime_key.empty(); ++depth) {
    const auto* frame = frames.FindFrame(runtime_key);
    if (frame == nullptr) {
      return runtime_key;
    }
    if (!frame->LuaName().empty()) {
      return std::string(frame->LuaName());
    }
    runtime_key = frame->parent;
  }
  return {};
}

void InitializeRuntimeMetadata(UiFrame& frame) {

  frame.runtime_kind = openwow::ui::framexml::DeriveUiFrameRuntimeKind(frame.kind);
  frame.runtime_draw_layer_enabled = true;
  frame.runtime_uses_mouse =
      frame.enable_mouse ||
      openwow::ui::framexml::StockConstructorEnablesMouse(frame.kind);

  if (frame.runtime_kind == openwow::ui::framexml::UiFrame::RuntimeKind::EditBox) {
    frame.runtime_uses_mouse = true;
  }
  frame.runtime_uses_mouse_wheel = false;
  frame.runtime_uses_keyboard = frame.enable_keyboard;
  for (const auto& handler : frame.script_handlers) {

    frame.runtime_uses_mouse_wheel |=
        openwow::text::EqualsIgnoreCaseAscii(handler.event, "OnMouseWheel");
  }
  frame.runtime_state_initialized = true;
}

void LinkToParent(lua_State* state, int frame, int parent) {
  frame = lua_absindex(state, frame);
  parent = lua_absindex(state, parent);
  lua_pushvalue(state, parent);
  lua_setfield(state, frame, "__ow_parent");
  lua_getfield(state, parent, "__ow_children");
  if (lua_istable(state, -1) == 0) {
    lua_pop(state, 1);
    lua_newtable(state);
    lua_pushvalue(state, -1);
    lua_setfield(state, parent, "__ow_children");
  }
  const int children = lua_absindex(state, -1);
  lua_pushvalue(state, frame);
  lua_seti(state, children, luaL_len(state, children) + 1);
  lua_pop(state, 1);
}

void ResolveImplicitAnchors(lua_State* state, int frame_index, int parent_index,
                            const UiFrame& frame) {
  if (frame.anchors.empty()) return;
  frame_index = lua_absindex(state, frame_index);
  parent_index = lua_absindex(state, parent_index);
  lua_getfield(state, frame_index, "__ow_anchors");
  if (lua_istable(state, -1) == 0) {
    lua_pop(state, 1);
    return;
  }
  const int anchors = lua_absindex(state, -1);
  for (std::size_t i = 0; i < frame.anchors.size(); ++i) {
    if (frame.anchors[i].relative_to_explicit ||
        !frame.anchors[i].relative_to.empty())
      continue;
    lua_rawgeti(state, anchors, static_cast<lua_Integer>(i + 1));
    if (lua_istable(state, -1) != 0) {
      lua_pushvalue(state, parent_index);
      lua_setfield(state, -2, "relativeTo");
    }
    lua_pop(state, 1);
  }
  lua_pop(state, 1);
}

}

FrameMaterializer::FrameMaterializer(Dependencies dependencies)
    : dependencies_(std::move(dependencies)) {}

void FrameMaterializer::BindLuaState(lua_State* state) noexcept {
  lua_ = state;
}

void FrameMaterializer::ClearLuaState() {
  if (lua_ != nullptr) script_cache_.ClearState(lua_);
  lua_ = nullptr;
}

void FrameMaterializer::Reset() {
  templates_.clear();
  default_declarations_.clear();
  loading_default_frame_xml_ = false;
}

void FrameMaterializer::AddTemplates(TemplateMap templates) {
  for (auto& [name, frames] : templates) {
    auto& stored = templates_[name];
    stored.insert(stored.end(), std::make_move_iterator(frames.begin()),
                  std::make_move_iterator(frames.end()));
  }
}

std::size_t FrameMaterializer::template_count() const noexcept {
  return templates_.size();
}

std::size_t FrameMaterializer::template_node_count() const noexcept {
  std::size_t count = 0;
  for (const auto& [name, frames] : templates_) {
    (void)name;
    count += frames.size();
  }
  return count;
}

void FrameMaterializer::BeginDefaultFrameXmlLoad() {
  default_declarations_.clear();
  loading_default_frame_xml_ = true;
}
void FrameMaterializer::EndDefaultFrameXmlLoad() noexcept {
  loading_default_frame_xml_ = false;
}
FrameMaterializer::LoadCheckpoint FrameMaterializer::CaptureLoadCheckpoint()
    const {
  return {templates_, default_declarations_};
}
void FrameMaterializer::RestoreLoadCheckpoint(LoadCheckpoint checkpoint) {
  if (lua_ != nullptr) script_cache_.ClearState(lua_);
  templates_ = std::move(checkpoint.templates);
  default_declarations_ = std::move(checkpoint.default_declarations);
  loading_default_frame_xml_ = false;
}
bool FrameMaterializer::loading_default_frame_xml() const noexcept {
  return loading_default_frame_xml_;
}
bool FrameMaterializer::HasDefaultDeclaration(std::string_view name) const {
  return default_declarations_.contains(std::string(name));
}
bool FrameMaterializer::IsDefaultDeclarationIdentity(std::string_view name,
                                                     const int lua_ref) const {
  const auto declaration = default_declarations_.find(std::string(name));
  return declaration != default_declarations_.end() &&
         declaration->second == lua_ref;
}
bool FrameMaterializer::HasCachedInlineHandler(
    lua_State* state, const framexml::ScriptHandler& handler) const {
  return script_cache_.HasCachedInlineHandler(state, handler);
}

void FrameMaterializer::SyncRuntimeMetadata(int frame_index, UiFrame& frame) {
  frame_index = lua_absindex(lua_, frame_index);
  if (auto type = StringField(lua_, frame_index, "__ow_type");
      type.has_value() && !type->empty()) {
    frame.kind = *type;
  }
  InitializeRuntimeMetadata(frame);
  frame.visible = detail::GetLuaWidgetShownState(lua_, frame_index);
  lua_getfield(lua_, frame_index, "__ow_alpha");
  if (lua_isnumber(lua_, -1)) {
    frame.color_a = static_cast<float>(NormalizeFrameAlphaByte(
        QuantizeFrameAlphaByteTruncated(lua_tonumber(lua_, -1))));
  }
  lua_pop(lua_, 1);
  lua_getfield(lua_, frame_index, "__ow_draw_layer_enabled");
  frame.runtime_draw_layer_enabled =
      !lua_isboolean(lua_, -1) || lua_toboolean(lua_, -1);
  lua_pop(lua_, 1);
  const auto optional_boolean = [&](const char* field) -> std::optional<bool> {
    lua_getfield(lua_, frame_index, field);
    std::optional<bool> result;
    if (lua_isboolean(lua_, -1)) result = lua_toboolean(lua_, -1) != 0;
    lua_pop(lua_, 1);
    return result;
  };
  const auto has_handler = [&](const char* handler) {
    std::string field = "__ow_script_";
    field += handler;
    lua_getfield(lua_, frame_index, field.c_str());
    bool found = lua_isfunction(lua_, -1);
    lua_pop(lua_, 1);
    if (!found) {
      lua_getfield(lua_, frame_index, handler);
      found = lua_isfunction(lua_, -1);
      lua_pop(lua_, 1);
    }
    return found;
  };
  auto mouse = optional_boolean("__ow_mouse_enabled");
  if (!mouse.has_value()) mouse = optional_boolean("__ow_enableMouse");

  frame.runtime_uses_mouse =
      mouse.value_or(
          openwow::ui::framexml::StockConstructorEnablesMouse(frame.kind));
  frame.runtime_uses_mouse_wheel = optional_boolean("__ow_mousewheel_enabled")
                                       .value_or(has_handler("OnMouseWheel"));
  auto keyboard = optional_boolean("__ow_keyboard_enabled");
  if (!keyboard.has_value()) keyboard = optional_boolean("__ow_enableKeyboard");
  frame.runtime_uses_keyboard = keyboard.value_or(false);
  frame.runtime_state_initialized = true;
}

int FrameMaterializer::CreateTrackedFrameBinding(
    const UiFrame& frame, int explicit_parent_index,
    bool suppress_named_parent_lookup) {

  if (lua_ == nullptr || lua_checkstack(lua_, LUA_MINSTACK) == 0) {
    return LUA_NOREF;
  }
  openwow::ui::lua::LuaStackRestore restore_stack(lua_);
  const std::string lua_name(frame.LuaName());
  const bool named = !lua_name.empty();
  detail::ScopedNeutralLuaTaint neutral_taint(lua_);
  UiFrame tracked = frame;
  const std::string key = dependencies_.frames.AllocateUniqueKey(frame.name);
  tracked.name = key;
  tracked.lua_name = lua_name;
  tracked.publish_to_lua = frame.publish_to_lua && named;
  if (explicit_parent_index != 0 && lua_istable(lua_, explicit_parent_index)) {
    if (auto parent = dependencies_.frames.FindKeyForLuaTable(
            lua_, explicit_parent_index);
        parent.has_value())
      tracked.parent = *parent;
  }

  int parent_index =
      explicit_parent_index != 0 && lua_istable(lua_, explicit_parent_index)
          ? lua_absindex(lua_, explicit_parent_index)
          : 0;
  bool pushed_parent = false;
  if (parent_index == 0 && !suppress_named_parent_lookup &&
      !tracked.parent.empty()) {
    if (auto ref = dependencies_.frames.FindLuaRef(tracked.parent);
        ref.has_value()) {
      lua_rawgeti(lua_, LUA_REGISTRYINDEX, *ref);
      if (lua_istable(lua_, -1)) {
        parent_index = lua_absindex(lua_, -1);
        pushed_parent = true;
      } else
        lua_pop(lua_, 1);
    }
  }

  const auto type = widgets::ScriptObjectTypeFromName(frame.kind);
  const bool texture = type == widgets::ScriptObjectType::Texture;
  const bool font_string = type == widgets::ScriptObjectType::FontString;
  const bool region = texture || font_string;
  ++metrics_.bindings_created;
  metrics_.texture_bindings_created += texture;
  metrics_.font_string_bindings_created += font_string;
  if (!region && parent_index != 0) {
    if (tracked.frame_strata.empty()) {
      lua_getfield(lua_, parent_index, frame_api::kLuaFrameStrataField);
      const char* strata = lua_tostring(lua_, -1);
      tracked.frame_strata =
          strata != nullptr && strata[0] != '\0' ? strata : "MEDIUM";
      lua_pop(lua_, 1);
    }
    if (!tracked.has_frame_level) {
      lua_getfield(lua_, parent_index, frame_api::kLuaFrameLevelField);
      tracked.frame_level = static_cast<int>(lua_tointeger(lua_, -1)) + 1;
      lua_pop(lua_, 1);
      tracked.has_frame_level = true;
    }
  }
  InitializeRuntimeMetadata(tracked);

  std::unique_ptr<widgets::CScriptObject> native;
  if (!region) {
    native = widgets::CreateFrameWidget(frame.kind);
    if (!native) return LUA_NOREF;
  }
  dependencies_.frames.InsertFrame(key, tracked);
  auto* native_pointer = native.get();
  if (native) {
    dependencies_.frames.AttachNativeObject(key, std::move(native));
    if (auto* minimap = dependencies_.frames.FindMinimap(key);
        minimap != nullptr)
      minimap->BindPresentation(dependencies_.minimap_state,
                                dependencies_.minimap_content);
    if (auto* world = dependencies_.frames.FindWorldFrame(key);
        world != nullptr)
      world->BindWorldFrame(dependencies_.world_frame);
  }
  dependencies_.layout.Invalidate();

  dependencies_.traversal.InvalidateHierarchy();

  if (texture)
    frame_api::CreateTextureTable(lua_, parent_index);
  else if (font_string)
    frame_api::CreateFontStringTable(lua_, parent_index);
  else
    lua_newtable(lua_);
  const int frame_index = lua_absindex(lua_, -1);
  if (!region) {
    const char* canonical = type != widgets::ScriptObjectType::COUNT_
                                ? widgets::ScriptObjectTypeName(type)
                                : frame.kind.c_str();
    lua_pushstring(lua_, canonical);
    lua_setfield(lua_, frame_index, "__ow_type");
    lua_adapter::AttachScriptObjectIdentity(lua_, frame_index, native_pointer);
  }
  lua_pushstring(lua_, key.c_str());
  lua_setfield(lua_, frame_index, frame_api::kLuaFrameRuntimeKeyField);
  if (named) {
    lua_pushlstring(lua_, lua_name.data(), lua_name.size());
    lua_setfield(lua_, frame_index, "__ow_name");
  }
  lua_pushboolean(lua_, frame.visible);
  lua_setfield(lua_, frame_index, "__ow_visible");
  lua_pushvalue(lua_, frame_index);
  const int ref = luaL_ref(lua_, LUA_REGISTRYINDEX);
  lua_pushinteger(lua_, ref);
  lua_setfield(lua_, frame_index, "__ow_ref");
  ApplyFullFrameSetup(tracked);
  if (parent_index != 0)
    ResolveImplicitAnchors(lua_, frame_index, parent_index, tracked);

  if (type == widgets::ScriptObjectType::EditBox) {
    lua_getfield(lua_, frame_index, "__ow_eb_fontstr");
    if (lua_istable(lua_, -1)) {
      const int text_index = lua_absindex(lua_, -1);
      const std::string text_key = key + ".__EditBoxText";
      lua_pushlstring(lua_, text_key.data(), text_key.size());
      lua_setfield(lua_, text_index, frame_api::kLuaFrameRuntimeKeyField);
      lua_pushvalue(lua_, text_index);
      const int text_ref = luaL_ref(lua_, LUA_REGISTRYINDEX);
      lua_pushinteger(lua_, text_ref);
      lua_setfield(lua_, text_index, "__ow_ref");
      UiFrame text;
      text.kind = "FontString";
      text.name = text_key;
      text.publish_to_lua = false;
      text.region_role = UiFrame::RegionRole::EditBoxText;
      text.parent = key;
      text.draw_layer = "OVERLAY";
      text.justify_h = "LEFT";
      text.visible = true;
      if (frame.has_text_insets) {
        text.anchors = {{.point = "TOPLEFT",
                         .relative_to = key,
                         .relative_point = "TOPLEFT",
                         .x = static_cast<float>(frame.text_inset_left),
                         .y = static_cast<float>(-frame.text_inset_top)},
                        {.point = "BOTTOMRIGHT",
                         .relative_to = key,
                         .relative_point = "BOTTOMRIGHT",
                         .x = static_cast<float>(-frame.text_inset_right),
                         .y = static_cast<float>(frame.text_inset_bottom)}};
      } else
        text.set_all_points = true;
      InitializeRuntimeMetadata(text);
      dependencies_.frames.InsertFrame(text_key, std::move(text));
      dependencies_.frames.AttachLuaBinding(text_key, text_ref);
      dependencies_.layout.PublishFrame(text_key);

      CreateEditBoxCaretRegions(key, dependencies_.frames,
                                dependencies_.layout);
    }
    lua_pop(lua_, 1);
  }

  if (!tracked.parent.empty() && parent_index == 0) {
    lua_pushstring(lua_, tracked.parent.c_str());
    lua_setfield(lua_, frame_index, "__ow_parent_name");
  }
  if (parent_index != 0) {
    if (!region) LinkToParent(lua_, frame_index, parent_index);
    anim::ApplyFrameXmlLoadBehavior(lua_, frame_index, parent_index, tracked,
                                    &script_cache_);
    if (font_string && frame.region_role == UiFrame::RegionRole::ButtonText &&
        detail::LuaScriptObjectIsKindOfCanonicalType(
            lua_, parent_index, widgets::ScriptObjectType::Button))
      frame_api::BindButtonFontStringRegion(lua_, parent_index, frame_index);
    if (texture)
      (void)frame_api::BindNativeTextureRegion(lua_, parent_index, frame_index,
                                               tracked);
    lua_pushnil(lua_);
    lua_setfield(lua_, frame_index, "__ow_parent_name");
  } else {
    anim::ApplyFrameXmlLoadBehavior(lua_, frame_index, 0, tracked,
                                    &script_cache_);
  }
  neutral_taint.Restore();
  const bool owns_lua_global =
      named && openwow::ui::PublishLuaGlobalValueIfNil(lua_, lua_name.c_str(),
                                                       frame_index);
  if (type == widgets::ScriptObjectType::GameTooltip) {
    static_cast<widgets::CSimpleGameTooltip*>(native_pointer)
        ->SetOwnsDefaultTooltipState(owns_lua_global &&
                                     lua_name == "GameTooltip");
  }
  lua_pop(lua_, 1);
  if (pushed_parent) lua_pop(lua_, 1);
  dependencies_.frames.AttachLuaBinding(key, ref);
  dependencies_.layout.PublishFrame(key);
  return ref;
}

int FrameMaterializer::InstantiateFrameTree(UiFrame root,
                                            std::vector<UiFrame> local_children,
                                            int explicit_parent_index,
                                            bool suppress_named_parent_lookup,
                                            bool push_root_result,
                                            bool record_default_declarations) {
  if (lua_ == nullptr) return LUA_NOREF;
  dependencies_.layout.EnterFrameTreeInstantiation();
  struct Guard {
    RetainedLayout& layout;
    ~Guard() { layout.LeaveFrameTreeInstantiation(); }
  } guard{dependencies_.layout};
  std::string parent_scope;
  if (explicit_parent_index != 0 && lua_istable(lua_, explicit_parent_index)) {
    if (auto key = dependencies_.frames.FindKeyForLuaTable(
            lua_, explicit_parent_index);
        key.has_value()) {
      parent_scope = NearestStoredLuaName(dependencies_.frames, *key);
    }
  } else {
    std::string key = root.parent;
    for (int depth = 0; depth < 64 && !key.empty(); ++depth) {
      const auto* parent = dependencies_.frames.FindFrame(key);
      if (parent == nullptr) {
        parent_scope = key;
        break;
      }
      if (!parent->LuaName().empty()) {
        parent_scope = parent->LuaName();
        break;
      }
      key = parent->parent;
    }
  }
  auto plan = BuildExpandedFramePlan(std::move(root), std::move(local_children),
                                     templates_, parent_scope);
  if (plan.frames.empty()) return LUA_NOREF;
  ++metrics_.tree_plans;
  metrics_.tree_source_nodes += plan.source_nodes;
  metrics_.tree_inherited_nodes += plan.inherited_nodes;
  metrics_.tree_plan_nodes += plan.frames.size();
  metrics_.tree_plan_node_peak =
      std::max(metrics_.tree_plan_node_peak,
               static_cast<std::uint64_t>(plan.frames.size()));
  if (loading_default_frame_xml_) {
    ++metrics_.default_tree_plans;
    metrics_.default_tree_source_nodes += plan.source_nodes;
    metrics_.default_tree_inherited_nodes += plan.inherited_nodes;
    metrics_.default_tree_plan_nodes += plan.frames.size();
    metrics_.default_tree_plan_node_peak =
        std::max(metrics_.default_tree_plan_node_peak,
                 static_cast<std::uint64_t>(plan.frames.size()));
  }

  {
    std::string external_parent_key;
    if (explicit_parent_index != 0 && lua_istable(lua_, explicit_parent_index)) {
      if (auto key = dependencies_.frames.FindKeyForLuaTable(
              lua_, explicit_parent_index);
          key.has_value())
        external_parent_key = *key;
    } else {
      external_parent_key = plan.frames[0].parent;
    }
    if (!external_parent_key.empty()) {
      if (const auto* external_parent =
              dependencies_.frames.FindFrame(external_parent_key);
          external_parent != nullptr &&
          external_parent->scroll_child_membership_count != 0u) {
        for (auto& planned_frame : plan.frames) {
          planned_frame.scroll_child_membership_count +=
              external_parent->scroll_child_membership_count;
          planned_frame.scroll_child_content = true;
        }
      }
    }
  }
  detail::BumpLuaLayoutProtectionGeneration(lua_);
  std::vector<int> refs(plan.frames.size(), LUA_NOREF);
  refs[0] = CreateTrackedFrameBinding(plan.frames[0], explicit_parent_index,
                                      suppress_named_parent_lookup);
  if (refs[0] == LUA_NOREF) return LUA_NOREF;
  for (std::size_t i = 1; i < plan.frames.size(); ++i) {
    if (plan.frames[i].region_role == UiFrame::RegionRole::EditBoxText) {
      const auto parent = plan.parents[i];
      if (parent < refs.size() && refs[parent] != LUA_NOREF) {
        const int top = lua_gettop(lua_);
        lua_rawgeti(lua_, LUA_REGISTRYINDEX, refs[parent]);
        lua_getfield(lua_, -1, "__ow_eb_fontstr");
        if (lua_istable(lua_, -1)) {
          frame_api::ApplyFrameXmlRegionDefinition(lua_, -1, plan.frames[i]);
          auto key = StringField(lua_, -1, frame_api::kLuaFrameRuntimeKeyField);
          if (key.has_value() && !key->empty()) {
            auto configured = plan.frames[i];
            configured.name = *key;
            configured.publish_to_lua = false;
            configured.region_role = UiFrame::RegionRole::EditBoxText;
            if (const auto* existing = dependencies_.frames.FindFrame(*key);
                existing != nullptr) {
              configured.parent = existing->parent;
              MergeInheritedFrameDefinition(configured, *existing);
            }
            InitializeRuntimeMetadata(configured);
            SyncRuntimeMetadata(-1, configured);
            dependencies_.frames.ReplaceFrame(*key, std::move(configured));
            dependencies_.layout.ReindexDependencies(*key);
          }
          frame_api::SyncEditBoxInternalDisplayText(lua_, -2);
        }
        lua_settop(lua_, top);
        continue;
      }
    }
    if (plan.frames[i].region_role ==
        UiFrame::RegionRole::MessageFontDefinition) {
      const auto parent = plan.parents[i];
      if (parent < refs.size() && refs[parent] != LUA_NOREF) {
        const int top = lua_gettop(lua_);
        lua_rawgeti(lua_, LUA_REGISTRYINDEX, refs[parent]);
        frame_api::ApplyFrameXmlMessageFontDefinition(lua_, -1, plan.frames[i]);
        lua_settop(lua_, top);
      }
      continue;
    }
    const auto parent = plan.parents[i];
    int parent_index = 0;
    bool parent_pushed = false;
    const bool has_structural_parent =
        parent < i && refs[parent] != LUA_NOREF &&
        (plan.frames[i].parent.empty() ||
         plan.frames[parent].name == plan.frames[i].parent);
    if (has_structural_parent) {
      lua_rawgeti(lua_, LUA_REGISTRYINDEX, refs[parent]);
      parent_pushed = true;
      if (lua_istable(lua_, -1)) parent_index = lua_absindex(lua_, -1);
    }
    refs[i] = CreateTrackedFrameBinding(plan.frames[i], parent_index,
                                        parent_index != 0);
    if (parent_pushed) lua_pop(lua_, 1);
  }
  if (record_default_declarations) {
    for (std::size_t i = 0; i < plan.frames.size(); ++i)
      if (refs[i] != LUA_NOREF && !plan.frames[i].name.empty())
        default_declarations_.insert_or_assign(plan.frames[i].name, refs[i]);
  }

  struct Cursor {
    std::size_t frame;
    std::size_t next_child;
  };
  std::vector<std::uint8_t> finalized(plan.frames.size());
  std::vector<Cursor> stack;
  stack.reserve(plan.frames.size());
  for (std::size_t root_index = 0; root_index < plan.frames.size();
       ++root_index) {
    if (finalized[root_index]) continue;
    stack.push_back({root_index, 0});
    finalized[root_index] = 1;
    metrics_.finalize_stack_peak = std::max(
        metrics_.finalize_stack_peak, static_cast<std::uint64_t>(stack.size()));
    while (!stack.empty()) {
      auto& cursor = stack.back();
      const auto& children = plan.children[cursor.frame];
      if (cursor.next_child < children.size()) {
        const auto child = children[cursor.next_child++];
        if (child < finalized.size() && !finalized[child]) {
          finalized[child] = 1;
          stack.push_back({child, 0});
          metrics_.finalize_stack_peak =
              std::max(metrics_.finalize_stack_peak,
                       static_cast<std::uint64_t>(stack.size()));
        }
        continue;
      }
      const auto frame = cursor.frame;
      stack.pop_back();
      if (refs[frame] == LUA_NOREF) continue;
      const auto type =
          widgets::ScriptObjectTypeFromName(plan.frames[frame].kind);
      if ((type == widgets::ScriptObjectType::Button ||
           type == widgets::ScriptObjectType::CheckButton) &&
          !plan.frames[frame].text.empty()) {
        const int top = lua_gettop(lua_);
        lua_rawgeti(lua_, LUA_REGISTRYINDEX, refs[frame]);
        if (lua_istable(lua_, -1)) {
          const std::string text = openwow::game::ResolveLocalizedGlobalString(
              lua_, plan.frames[frame].text);
          lua_getfield(lua_, -1, "SetText");
          if (lua_isfunction(lua_, -1)) {
            lua_pushvalue(lua_, -2);
            const std::string& value =
                text.empty() ? plan.frames[frame].text : text;
            lua_pushlstring(lua_, value.data(), value.size());
            (void)lua_pcall(lua_, 2, 0, 0);
          }
        }
        lua_settop(lua_, top);
      }
      WireScriptHandlers(plan.frames[frame], refs[frame]);
      ++metrics_.finalize_dispatches;
    }
  }
  if (push_root_result) lua_rawgeti(lua_, LUA_REGISTRYINDEX, refs[0]);
  return refs[0];
}

int FrameMaterializer::AdoptRuntimeFrame(const UiFrame& frame) {
  if (lua_ == nullptr || lua_istable(lua_, -1) == 0 ||
      lua_checkstack(lua_, LUA_MINSTACK) == 0) {
    return LUA_NOREF;
  }
  const int index = lua_absindex(lua_, -1);
  detail::BumpLuaLayoutProtectionGeneration(lua_);
  lua_pushvalue(lua_, index);
  const int ref = luaL_ref(lua_, LUA_REGISTRYINDEX);
  lua_pushinteger(lua_, ref);
  lua_setfield(lua_, index, "__ow_ref");
  UiFrame tracked = frame;
  const std::string lua_name(frame.LuaName());
  const std::string key = dependencies_.frames.AllocateUniqueKey(frame.name);
  tracked.name = key;
  tracked.lua_name = lua_name;
  tracked.publish_to_lua = frame.publish_to_lua && !lua_name.empty();
  if (tracked.parent.empty()) {
    lua_getfield(lua_, index, "__ow_parent");
    if (auto parent = FrameKey(lua_, -1); parent.has_value())
      tracked.parent = *parent;
    lua_pop(lua_, 1);
  }
  lua_pushstring(lua_, key.c_str());
  lua_setfield(lua_, index, frame_api::kLuaFrameRuntimeKeyField);

  if (const auto* owner_frame = dependencies_.frames.FindFrame(tracked.parent);
      owner_frame != nullptr &&
      owner_frame->scroll_child_membership_count != 0u) {
    tracked.scroll_child_membership_count +=
        owner_frame->scroll_child_membership_count;
    tracked.scroll_child_content = true;
  }
  InitializeRuntimeMetadata(tracked);
  SyncRuntimeMetadata(index, tracked);
  dependencies_.frames.InsertFrame(key, tracked);
  dependencies_.frames.AttachLuaBinding(key, ref);
  dependencies_.layout.PublishFrame(key);

  dependencies_.traversal.InvalidateHierarchy();
  if (!lua_name.empty())
    (void)openwow::ui::PublishLuaGlobalValueIfNil(lua_, lua_name.c_str(),
                                                  index);
  return ref;
}

int FrameMaterializer::CreateFrame(lua_State* state) {
  if (state == nullptr || state != lua_) {
    if (state) lua_pushnil(state);
    return 1;
  }
  if (!lua_isstring(state, 1) ||
      (!lua_isnone(state, 3) && !lua_isnil(state, 3) && !lua_istable(state, 3)))
    return luaL_error(state,
                      "Usage: CreateFrame(\"frameType\" [, \"name\"] [, "
                      "parent] [, \"template\"] [, id])");
  const char* type = lua_tostring(state, 1);
  const char* name = lua_tostring(state, 2);

  const char* inherits = lua_tostring(state, 4);
  int parent_index = 0;
  std::optional<std::string> parent_name;
  if (lua_istable(state, 3)) {
    parent_index = lua_absindex(state, 3);
    if (!lua_adapter::HasScriptObjectIdentity(state, parent_index))
      return luaL_error(state,
                        "CreateFrame: Couldn't find 'this' in parent object");
    const auto object_type =
        detail::GetLuaCanonicalScriptObjectType(state, parent_index);
    if (!detail::IsFrameLikeLookupObjectType(object_type))
      return luaL_error(
          state, "CreateFrame: Wrong parent object type, expected frame");
    parent_name = dependencies_.frames.FindKeyForLuaTable(state, parent_index);
  }
  if (inherits != nullptr && inherits[0] != '\0') {
    for (const auto& template_name : framexml::SplitTemplateList(
             inherits, framexml::TemplateListSyntax::kCreateFrame)) {
      switch (ValidateFrameTemplateChain(templates_, template_name)) {
        case FrameTemplateValidation::Missing:
          return luaL_error(
              state, "CreateFrame(): Couldn't find inherited node \"%s\"",
              template_name.c_str());
        case FrameTemplateValidation::Recursive:
          return luaL_error(state,
                            "CreateFrame(): Recursively inherited node \"%s\"",
                            template_name.c_str());
        case FrameTemplateValidation::Found:
          break;
      }
    }
  }
  const char* canonical = widgets::ResolveRegisteredCreateFrameTypeName(
      type != nullptr ? type : "");
  if (canonical == nullptr)
    return luaL_error(state, "CreateFrame: Unknown frame type '%s'",
                      type != nullptr ? type : "");
  UiFrame root;
  root.kind = canonical;
  root.visible = true;
  if (name != nullptr && name[0] != '\0')
    root.name = frame_api::ExpandLuaParentNameToken(
        parent_name.has_value()
            ? NearestStoredLuaName(dependencies_.frames, *parent_name)
            : std::string(),
        name);
  if (inherits != nullptr && inherits[0] != '\0') root.inherits = inherits;
  auto record_id = [&](const char* raw, const char* value_type) {
    if (raw != nullptr && raw[0] != '\0')
      root.initial_attributes.push_back(
          {.name = "id", .type = value_type, .value = raw});
  };

  if (lua_isstring(state, 5)) {
    const char* raw = lua_tostring(state, 5);
    record_id(raw, "string");
    if (raw != nullptr)
      if (auto parsed = framexml::ParseIntegerAttributeValue(raw);
          parsed.has_value()) {
        root.id = *parsed;
        root.has_id = true;
      }
  } else if (lua_isnumber(state, 5)) {
    record_id(lua_tostring(state, 5), "number");
    root.id = static_cast<int>(lua_tointeger(state, 5));
    root.has_id = true;
  }
  if (parent_index != 0) {
    if (parent_name.has_value()) root.parent = *parent_name;
  } else if (inherits != nullptr && inherits[0] != '\0') {
    if (auto parent = ResolveInheritedParentName(templates_, inherits);
        parent.has_value())
      root.parent = *parent;
  }
  const int ref = InstantiateFrameTree(std::move(root), {}, parent_index,
                                       parent_index != 0, true);

  if (ref == LUA_NOREF || !lua_istable(state, -1)) {
    return luaL_error(state, "CreateFrame: failed to create frame '%s'",
                      name != nullptr ? name : "");
  }
  return 1;
}

}
