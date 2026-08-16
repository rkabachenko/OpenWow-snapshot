#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/glue/glue_lua_api_internal.h"
#include "openwow/game/localization.h"
#include "openwow/foundation/math/vec3_normalize_if_length_squared_exceeds_client_epsilon.h"
#include "openwow/render/resources/textures/texture_manager.h"
#include "openwow/ui/animation/animation_lua.h"
#include "openwow/ui/ui_aspect_scales.h"
#include "openwow/ui/font_layout.h"
#include "openwow/ui/font_string_layout.h"
#include "openwow/ui/frame_script_type_info.h"
#include "openwow/ui/framexml/framexml_name_utils.h"
#include "openwow/ui/framexml/framexml_value_utils.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/game/framescript/core/frame_font_runtime.h"
#include "openwow/ui/game/framescript/core/frame_runtime_identity.h"
#include "openwow/ui/glue/editbox_text_layout.h"
#include "openwow/ui/glue/glue_font_metrics.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/lua_result_capacity.h"
#include "openwow/ui/lua_post_hook_closure.h"
#include "openwow/ui/ui_enum_helpers.h"
#include "openwow/ui/widgets/simple_frame.h"
#include "openwow/foundation/text/utf8.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "openwow/foundation/diagnostics/logging.h"
#include <cstring>
#include <limits>
#include <optional>
#include <string_view>
#include <unordered_set>

#include "openwow/ui/glue/widget_lua_adapter_support.h"
#include "openwow/ui/glue/widget_lua_bindings.h"

namespace openwow::ui::glue::detail {

int LuaGetNumFrames(lua_State* state) {
  auto* runtime = GetWidgetRuntime(state);
  if (runtime == nullptr) {
    lua_pushnumber(state, 0.0);
    return 1;
  }

  lua_pushnumber(state,
                 static_cast<lua_Number>(CollectGlueFrameNamesInCreationOrder(*runtime).size()));
  return 1;
}

int LuaEnumerateFrames(lua_State* state) {
  auto* runtime = GetWidgetRuntime(state);
  if (runtime == nullptr) {
    lua_pushnil(state);
    return 1;
  }

  const auto frame_names = CollectGlueFrameNamesInCreationOrder(*runtime);
  if (lua_type(state, 1) == LUA_TTABLE) {

    const auto current_name = ReadStoredWidgetRuntimeKey(state, 1);
    if (current_name.empty()) {
      return luaL_error(state, "EnumerateFrames: Couldn't find 'this' in current object");
    }

    const auto current = runtime->GetWidget(current_name);
    if (!current.has_value() || !IsGlueFrameLikeForEnumeration(*current)) {
      return luaL_error(state, "EnumerateFrames: Wrong current object type, expected frame");
    }

    const auto it = std::find(frame_names.begin(), frame_names.end(), current_name);
    if (it != frame_names.end() && it != frame_names.begin()) {
      if (PushGlueWidgetGlobalTable(state, *(it - 1))) {
        return 1;
      }
    }

    lua_pushnil(state);
    return 1;
  }

  for (auto it = frame_names.rbegin(); it != frame_names.rend(); ++it) {
    if (PushGlueWidgetGlobalTable(state, *it)) {
      return 1;
    }
  }

  lua_pushnil(state);
  return 1;
}

int LuaGetFramesRegisteredForEvent(lua_State* state) {
  if (lua_isstring(state, 1) == 0) {
    return luaL_error(state, "Usage: GetFramesRegisteredForEvent(\"event\")");
  }

  const char* event_name_arg = lua_tostring(state, 1);
  const std::string event_name = event_name_arg != nullptr ? event_name_arg : "";
  const auto* glue_runtime = GetGlueRuntime(state);
  if (glue_runtime == nullptr) {
    return 0;
  }

  const auto registered_widgets = glue_runtime->RegisteredWidgetsForEvent(event_name);

  if (registered_widgets.size() >
          static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
      lua_checkstack(state, static_cast<int>(registered_widgets.size())) == 0) {
    return luaL_error(state, "GetFramesRegisteredForEvent(%s): Stack overflow",
                      event_name_arg);
  }

  int pushed = 0;
  for (const auto& widget_name : registered_widgets) {
    if (!PushGlueWidgetGlobalTable(state, widget_name)) {
      continue;
    }
    ++pushed;
  }

  return pushed;
}

int LuaGetCurrentKeyBoardFocus(lua_State* state) {
  const auto* runtime = GetWidgetRuntime(state);
  if (runtime == nullptr || runtime->focused_widget().empty() ||
      !PushGlueWidgetGlobalTable(state, runtime->focused_widget())) {
    lua_pushnil(state);
  }
  return 1;
}

void PublishWidgetGlobal(lua_State* state, const std::string& name) {
  EnsureWidgetMethodTable(state);

  const auto* runtime = GetWidgetRuntime(state);
  const auto widget = runtime != nullptr ? runtime->GetWidget(name)
                                         : std::optional<GlueWidgetState>{};
  const std::string public_name =
      widget.has_value() ? std::string(widget->LuaName()) : name;
  if (!PushStoredWidgetTableByRuntimeKey(state, name)) {
    lua_newtable(state);
  }

  lua_pushlstring(state, public_name.data(), public_name.size());
  lua_setfield(state, -2, kGlueLuaFrameNameField);
  lua_pushstring(state, name.c_str());
  lua_setfield(state, -2, kGlueLuaFrameRuntimeKeyField);
  lua_pushlstring(state, public_name.data(), public_name.size());
  lua_setfield(state, -2, "__ow_public_name");

  lua_getfield(state, LUA_REGISTRYINDEX, kWidgetMethodsRegistryKey);
  lua_setmetatable(state, -2);

  std::string widget_kind = "Frame";
  if (IsUiParentName(name)) {
    widget_kind = "Frame";
  } else if (widget.has_value() && !widget->kind.empty()) {
    widget_kind = widget->kind;
    lua_pushboolean(state, widget->clamped_to_screen ? 1 : 0);
    lua_setfield(state, -2, "__ow_clamped");
    lua_pushboolean(state, widget->movable ? 1 : 0);
    lua_setfield(state, -2, kGlueMovableField);
    lua_pushboolean(state, widget->resizable ? 1 : 0);
    lua_setfield(state, -2, kGlueResizableField);
    lua_pushboolean(state, widget->toplevel ? 1 : 0);
    lua_setfield(state, -2, kGlueToplevelField);
    lua_pushboolean(state, widget->user_placed ? 1 : 0);
    lua_setfield(state, -2, kGlueUserPlacedField);
  }
  lua_pushstring(state, widget_kind.c_str());
  lua_setfield(state, -2, "__ow_type");
  BindWidgetObjectTypeMethods(state, widget_kind);
  StoreWidgetTableByRuntimeKey(state, name, -1);

  if (!public_name.empty()) {
    (void)openwow::ui::PublishLuaGlobalValueIfNil(
        state, public_name.c_str(), -1);
  }
  lua_pop(state, 1);
}

int LuaCreateFrame(lua_State* state) {
  if (lua_isstring(state, 1) == 0 ||
      (!lua_isnone(state, 3) && lua_isnil(state, 3) == 0 &&
       lua_istable(state, 3) == 0)) {
    return luaL_error(
        state, "Usage: CreateFrame(\"frameType\" [, \"name\"] [, parent] [, \"template\"] [, id])");
  }

  const char* frame_type_raw = lua_tostring(state, 1);
  const std::string frame_type = frame_type_raw != nullptr ? std::string(frame_type_raw) : std::string();

  const char* frame_name_raw = lua_tostring(state, 2);
  const char* inherits_raw = lua_tostring(state, 4);
  auto* runtime = GetWidgetRuntime(state);

  std::string parent_key;
  if (lua_istable(state, 3) != 0) {
    parent_key = ReadStoredWidgetRuntimeKey(state, 3);
    if (parent_key.empty()) {
      return luaL_error(state, "CreateFrame: Couldn't find 'this' in parent object");
    }
    if (auto* parent_runtime = GetWidgetRuntime(state);
        parent_runtime != nullptr && !IsUiParentName(parent_key)) {
      const auto parent_widget = parent_runtime->GetWidget(parent_key);
      if (!parent_widget.has_value()) {
        return luaL_error(state,
                          "CreateFrame: Couldn't find 'this' in parent object");
      }
      if (!GlueWidgetMatchesFrameType(*parent_widget)) {
        return luaL_error(
            state, "CreateFrame: Wrong parent object type, expected frame");
      }
    } else if (parent_runtime == nullptr &&
               !IsGlueFrameLikeType(
                   ReadGlueTableStringField(state, 3, "__ow_type"))) {
      return luaL_error(
          state, "CreateFrame: Wrong parent object type, expected frame");
    }
  }

  const std::string inherits = inherits_raw != nullptr ? std::string(inherits_raw) : std::string();
  if (runtime != nullptr && lua_type(state, 4) == LUA_TSTRING) {
    for (const auto& template_name : openwow::ui::framexml::SplitTemplateList(
             inherits, openwow::ui::framexml::TemplateListSyntax::kCreateFrame)) {
      switch (runtime->ValidateTemplateChain(template_name)) {
        case GlueTemplateValidation::kFound:
          break;
        case GlueTemplateValidation::kMissing:
          return luaL_error(state, "CreateFrame(): Couldn't find inherited node \"%s\"",
                            template_name.c_str());
        case GlueTemplateValidation::kRecursive:
          return luaL_error(state, "CreateFrame(): Recursively inherited node \"%s\"",
                            template_name.c_str());
      }
    }
  }

  const char* canonical_frame_type =
      openwow::ui::widgets::ResolveRegisteredCreateFrameTypeName(frame_type);
  if (canonical_frame_type == nullptr) {
    return luaL_error(state, "CreateFrame: Unknown frame type '%s'", frame_type.c_str());
  }

  const std::string frame_name =
      openwow::ui::game::frame_api::ExpandLuaParentNameToken(
          runtime != nullptr ? runtime->NearestLuaName(parent_key)
                             : std::string(),
          frame_name_raw);

  const int frame_id = ReadCreateFrameNumericId(state, 5);
  if (frame_name.empty()) {
    std::string runtime_key;
    std::vector<std::string> created;
    if (runtime != nullptr) {
      runtime_key = runtime->RegisterAnonymousWidget({
          .kind = canonical_frame_type,
          .inherits = inherits,
          .id = frame_id,
          .frame_strata = "MEDIUM",
          .frame_level = 0,
          .x = 0,
          .y = 0,
          .width = 0,
          .height = 0,
          .alpha = 1.0F,
          .visible = true,
      });

      runtime->SetParent(runtime_key, parent_key);
      PushAnonymousGlueFrame(state, canonical_frame_type, runtime_key, frame_id);
      if (!inherits.empty()) {
        created = runtime->InstantiateTemplate(runtime_key, inherits);
      }
      for (const auto& child : created) {
        if (!child.empty()) {
          PublishWidgetGlobal(state, child);
        }
      }
      FinalizePublishedWidgetGlobal(state, runtime_key);
      for (const auto& child : created) {
        if (!child.empty()) {
          FinalizePublishedWidgetGlobal(state, child);
        }
      }
      if (auto* glue_runtime = GetGlueRuntime(state); glue_runtime != nullptr) {
        if (const auto result = glue_runtime->RunWidgetEvent(
                runtime_key, "OnLoad", runtime_key + ".OnLoad", {});
            !result.ok) {
          openwow::diagnostics::Log(
              openwow::diagnostics::LogLevel::kWarn,
              "Glue CreateFrame OnLoad failed: widget=" + runtime_key +
                  " err=" + result.error);
        }
        for (const auto& child : created) {
          if (child.empty()) {
            continue;
          }
          if (const auto result = glue_runtime->RunWidgetEvent(
                  child, "OnLoad", child + ".OnLoad", {});
              !result.ok) {
            openwow::diagnostics::Log(
                openwow::diagnostics::LogLevel::kWarn,
                "Glue CreateFrame OnLoad failed: widget=" + child +
                    " err=" + result.error);
          }
        }
      }
      return 1;
    }
    PushAnonymousGlueFrame(state, canonical_frame_type, runtime_key, frame_id);
    return 1;
  }

  std::string runtime_key = frame_name;
  if (runtime != nullptr) {
    std::vector<std::string> created;
    runtime_key = runtime->AllocateUniqueWidgetKey(frame_name);
    runtime->RegisterWidget({
        .name = runtime_key,
        .lua_name = frame_name,
        .kind = canonical_frame_type,
        .inherits = inherits,
        .id = frame_id,
        .frame_strata = "MEDIUM",
        .frame_level = 0,
        .x = 0,
        .y = 0,
        .width = 0,
        .height = 0,
        .alpha = 1.0F,
        .visible = true,
    });
    runtime->SetParent(runtime_key, parent_key);
    if (!inherits.empty()) {
      created = runtime->InstantiateTemplate(runtime_key, inherits);
    }

    PublishWidgetGlobal(state, runtime_key);
    for (const auto& child : created) {
      if (!child.empty()) {
        PublishWidgetGlobal(state, child);
      }
    }
    FinalizePublishedWidgetGlobal(state, runtime_key);
    for (const auto& child : created) {
      if (!child.empty()) {
        FinalizePublishedWidgetGlobal(state, child);
      }
    }
    if (PushStoredWidgetTableByRuntimeKey(state, runtime_key)) {
      if (frame_id != 0) {
        StoreGlueFrameId(state, -1, frame_id);
      }
      lua_pop(state, 1);
    }

    if (auto* glue_runtime = GetGlueRuntime(state); glue_runtime != nullptr) {
      if (const auto r = glue_runtime->RunWidgetEvent(runtime_key, "OnLoad", frame_name + ".OnLoad", {});
          !r.ok) {
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                           "Glue CreateFrame OnLoad failed: widget=" + runtime_key + " err=" + r.error);
      }
      for (const auto& child : created) {
        if (child.empty()) continue;
        if (const auto r = glue_runtime->RunWidgetEvent(child, "OnLoad", child + ".OnLoad", {}); !r.ok) {
          openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                             "Glue CreateFrame OnLoad failed: widget=" + child + " err=" + r.error);
        }
      }
    }
  } else {
    PublishWidgetGlobal(state, frame_name);
    if (frame_id != 0) {
      lua_getglobal(state, frame_name.c_str());
      if (lua_istable(state, -1) != 0) {
        StoreGlueFrameId(state, -1, frame_id);
      }
      lua_pop(state, 1);
    }
  }
  if (runtime != nullptr && PushStoredWidgetTableByRuntimeKey(state, runtime_key)) {
    return 1;
  }
  lua_getglobal(state, frame_name.c_str());
  return 1;
}

void PublishWidgetsAsGlobals(lua_State* state, GlueWidgetRuntime* runtime) {
  if (runtime == nullptr) {
    return;
  }
  for (const auto& name : runtime->WidgetNames()) {
    PublishWidgetGlobal(state, name);
  }
  PublishWidgetGlobal(state, "UIParent");
  for (const auto& name : runtime->WidgetNames()) {
    FinalizePublishedWidgetGlobal(state, name);
  }
}

}
