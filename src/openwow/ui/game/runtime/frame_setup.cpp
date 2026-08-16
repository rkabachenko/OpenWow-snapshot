#include "openwow/ui/game/runtime/frame_materializer.h"

#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/foundation/text/ascii.h"
#include "openwow/game/localization.h"
#include "openwow/ui/frame_attribute_lua.h"
#include "openwow/ui/frame_script_events.h"
#include "openwow/ui/framexml/anchor_semantics.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/framescript/core/frame_backdrop_runtime.h"
#include "openwow/ui/game/framescript/core/frame_font_runtime.h"
#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/game/framescript/core/frame_region_factory.h"
#include "openwow/ui/game/framescript/core/frame_runtime_identity.h"
#include "openwow/ui/game/framescript/core/frame_script_invocation.h"
#include "openwow/ui/game/framescript/core/script_region_ownership.h"
#include "openwow/ui/game/framescript/widgets/color_select_methods.h"
#include "openwow/ui/game/framescript/widgets/edit_box_methods.h"
#include "openwow/ui/game/runtime/frame_store.h"
#include "openwow/ui/game/secure_execution.h"
#include "openwow/ui/lua_taint_api.h"
#include "openwow/ui/widgets/script_object.h"
#include "openwow/ui/widgets/simple_frame.h"
#include "openwow/ui/widgets/status_bar.h"

extern "C" {
#include <lua.hpp>
}

#include <cstdlib>

namespace openwow::ui::game::runtime {
namespace {

using UiFrame = openwow::ui::framexml::UiFrame;

bool PushAttribute(lua_State *state, const UiFrame::InitialAttribute &attribute) {
  const std::string type =
      openwow::text::ToLowerAscii(attribute.type.empty() ? std::string("string") : attribute.type);
  if (type == "nil") {
    lua_pushnil(state);
    return true;
  }
  if (attribute.value.empty())
    return false;
  if (type == "boolean") {
    bool value = false;
    switch (attribute.value.front()) {
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
    case 'T':
    case 'Y':
    case 't':
    case 'y':
      value = true;
      break;
    default: {
      const std::string lowered = openwow::text::ToLowerAscii(attribute.value);
      value = lowered == "on" || lowered == "enabled";
      break;
    }
    }
    lua_pushboolean(state, value);
  } else if (type == "number") {
    lua_pushnumber(state, std::strtod(attribute.value.c_str(), nullptr));
  } else {
    lua_pushlstring(state, attribute.value.data(), attribute.value.size());
  }
  return true;
}

void ApplyAttributes(lua_State *state, int index, const UiFrame &frame) {
  const openwow::ui::ScopedNeutralLuaExecutionTaint neutral_taint(state);
  index = lua_absindex(state, index);
  for (const auto &attribute : frame.initial_attributes) {
    if (attribute.name.empty() || !PushAttribute(state, attribute))
      continue;
    StoreLuaFrameAttributeValue(state, index, NormalizeFrameAttributeKey(attribute.name.c_str()),
                                -1);
    lua_pop(state, 1);
  }
}

void StoreScriptTaintSource(lua_State *state, int index, const char *handler, bool present,
                            openwow::ui::game::TaintSourceId source) {
  const openwow::ui::ScopedNeutralLuaExecutionTaint neutral_taint(state);
  std::string key(openwow::ui::game::kLuaFrameScriptTaintFieldPrefix);
  key += handler != nullptr ? handler : "";
  if (present)
    lua_pushinteger(state, source);
  else
    lua_pushnil(state);
  lua_setfield(state, lua_absindex(state, index), key.c_str());
}

std::string DisplayName(const UiFrame &frame) {
  return !frame.LuaName().empty() ? std::string(frame.LuaName())
         : frame.name.empty()     ? "<unnamed>"
                                  : frame.name;
}

}

void PublishFrameAnchorsToLua(lua_State *lua, int frame_index,
                              const UiFrame &frame) {
  if (lua == nullptr || frame.anchors.empty() ||
      lua_istable(lua, frame_index) == 0)
    return;
  frame_index = lua_absindex(lua, frame_index);
  lua_newtable(lua);
  const int anchors = lua_absindex(lua, -1);
  int item = 1;
  for (const auto &anchor : frame.anchors) {
    lua_newtable(lua);
    const auto point = framexml::EffectiveAnchorPoint(anchor);
    lua_pushlstring(lua, point.data(), point.size());
    lua_setfield(lua, -2, "point");
    if (!anchor.relative_to.empty()) {
      lua_pushstring(lua, anchor.relative_to.c_str());
      lua_setfield(lua, -2, "relativeTo");
    }
    const auto relative = framexml::EffectiveAnchorRelativePoint(anchor);
    lua_pushlstring(lua, relative.data(), relative.size());
    lua_setfield(lua, -2, "relativePoint");
    lua_pushnumber(lua, anchor.x);
    lua_setfield(lua, -2, "x");
    lua_pushnumber(lua, anchor.y);
    lua_setfield(lua, -2, "y");
    if (anchor.flags != 0) {
      lua_pushinteger(lua, anchor.flags);
      lua_setfield(lua, -2, "__ow_flags");
    }
    lua_rawseti(lua, anchors, item++);
  }
  lua_setfield(lua, frame_index, "__ow_anchors");
  openwow::ui::game::detail::ReindexLuaAnchorDependents(lua, frame_index);
}

void FrameMaterializer::ApplyFullFrameSetup(const UiFrame &frame) {
  const int index = lua_absindex(lua_, -1);
  const auto type = widgets::ScriptObjectTypeFromName(frame.kind);
  if (type == widgets::ScriptObjectType::Texture || type == widgets::ScriptObjectType::FontString) {
    auto configured = frame;
    if (type == widgets::ScriptObjectType::FontString && !configured.text.empty()) {
      const std::string localized =
          openwow::game::ResolveLocalizedGlobalString(lua_, configured.text);
      if (!localized.empty())
        configured.text = localized;
    }
    frame_api::ApplyFrameXmlRegionDefinition(lua_, index, configured);
    ApplyAttributes(lua_, index, frame);
    return;
  }

  frame_api::RegisterFrameScriptMethods(lua_);
  frame_api::ApplyRegisteredFrameMethods(lua_);
  frame_api::ApplyFrameTypeMethods(lua_, frame.kind.c_str());
  frame_api::ApplyFrameXmlBackdropDefinition(lua_, index, frame);

  if (type == widgets::ScriptObjectType::MessageFrame ||
      type == widgets::ScriptObjectType::ScrollingMessageFrame) {
    if (!frame.message_font_style.empty() &&
        frame_api::PushNamedFontObject(lua_, frame.message_font_style.c_str())) {
      frame_api::SetBoundFontObject(lua_, index, -1);
      frame_api::CopyNamedFontObjectStyle(lua_, index, -1);
      lua_pop(lua_, 1);
    }
    if (frame.message_fading.has_value()) {
      lua_pushboolean(lua_, *frame.message_fading);
      lua_setfield(lua_, index, "__ow_smf_fading");
    }
    if (frame.message_display_duration.has_value()) {
      lua_pushnumber(lua_, *frame.message_display_duration);
      lua_setfield(lua_, index, "__ow_smf_timevis");
    }
    if (frame.message_fade_duration.has_value()) {
      lua_pushnumber(lua_, *frame.message_fade_duration);
      lua_setfield(lua_, index, "__ow_smf_fadedur");
    }
    if (frame.message_max_lines.has_value()) {
      lua_pushinteger(lua_, *frame.message_max_lines);
      lua_setfield(lua_, index, "__ow_smf_maxlines");
    }
    if (!frame.message_insert_mode.empty()) {
      const bool top = type == widgets::ScriptObjectType::ScrollingMessageFrame
                           ? frame.message_insert_mode == "TOP"
                           : frame.message_insert_mode != "BOTTOM";
      lua_pushstring(lua_, top ? "TOP" : "BOTTOM");
      lua_setfield(lua_, index, "__ow_smf_insert");
    }
  }

  if (type == widgets::ScriptObjectType::EditBox) {
    if (!frame.text.empty()) {
      lua_pushlstring(lua_, frame.text.data(), frame.text.size());
      lua_setfield(lua_, index, "__ow_eb_text");
    }
    lua_pushboolean(lua_, frame.password);
    lua_setfield(lua_, index, "__ow_eb_password");
    if (frame.max_letters >= 0) {
      lua_pushinteger(lua_, frame.max_letters);
      lua_setfield(lua_, index, "__ow_eb_maxletters");
    }
    if (frame.has_text_insets) {
      lua_pushnumber(lua_, frame.text_inset_left);
      lua_setfield(lua_, index, "__ow_eb_inset_l");
      lua_pushnumber(lua_, frame.text_inset_right);
      lua_setfield(lua_, index, "__ow_eb_inset_r");
      lua_pushnumber(lua_, frame.text_inset_top);
      lua_setfield(lua_, index, "__ow_eb_inset_t");
      lua_pushnumber(lua_, frame.text_inset_bottom);
      lua_setfield(lua_, index, "__ow_eb_inset_b");
    }
    frame_api::SyncEditBoxInternalDisplayText(lua_, index);
  }

  const auto boolean = [&](const char *field, bool value) {
    lua_pushboolean(lua_, value);
    lua_setfield(lua_, index, field);
  };
  const auto number = [&](const char *field, double value) {
    lua_pushnumber(lua_, value);
    lua_setfield(lua_, index, field);
  };
  if (frame.has_id) {
    lua_pushinteger(lua_, frame.id);
    lua_setfield(lua_, index, "__ow_id");
  }
  if (frame.width.has_value())
    number("__ow_width", *frame.width);
  if (frame.height.has_value())
    number("__ow_height", *frame.height);
  if (!frame.frame_strata.empty()) {
    lua_pushstring(lua_, frame.frame_strata.c_str());
    lua_setfield(lua_, index, frame_api::kLuaFrameStrataField);
  }
  if (frame.frame_level != 0) {
    lua_pushinteger(lua_, frame.frame_level);
    lua_setfield(lua_, index, frame_api::kLuaFrameLevelField);
  }
  if (frame.depth != 0.0f)
    number("__ow_depth", frame.depth);
  if (frame.user_placed)
    boolean("__ow_user_placed", true);
  if (frame.movable.has_value())
    boolean("__ow_movable", *frame.movable);
  if (frame.resizable.has_value())
    boolean("__ow_resizable", *frame.resizable);
  if (frame.dont_save_position)
    boolean("__ow_dontsavepos", true);
  if (frame.enable_mouse || frame.enable_mouse_explicit)
    boolean("__ow_enableMouse", frame.enable_mouse);
  if (frame.enable_keyboard || frame.enable_keyboard_explicit)
    boolean("__ow_enableKeyboard", frame.enable_keyboard);
  if (frame.protected_frame)
    boolean("__ow_protected", true);

  if (frame.cooldown_reverse.has_value())
    boolean("__ow_cd_reverse", *frame.cooldown_reverse);
  if (frame.cooldown_draw_edge.has_value())
    boolean("__ow_cd_draw_edge", *frame.cooldown_draw_edge);
  if (frame.has_model_scale)
    number("__ow_model_scale", frame.model_scale);
  if (frame.has_hit_rect_insets) {
    number("__ow_hit_l", frame.hit_rect_inset_left);
    number("__ow_hit_r", frame.hit_rect_inset_right);
    number("__ow_hit_t", frame.hit_rect_inset_top);
    number("__ow_hit_b", frame.hit_rect_inset_bottom);
    if (auto *object = dependencies_.frames.FindSimpleFrame(frame.name); object != nullptr)
      object->SetHitRectInsets(frame.hit_rect_inset_left, frame.hit_rect_inset_right,
                               frame.hit_rect_inset_top, frame.hit_rect_inset_bottom);
  }

  const auto font = [&](const char *field, const std::string &style) {
    if (style.empty())
      return;
    if (!frame_api::PushNamedFontObject(lua_, style.c_str())) {
      lua_pop(lua_, 1);
      diagnostics::Log(diagnostics::LogLevel::kWarn,
                       "FrameMaterializer: couldn't find button font " + style + " for " +
                           frame.name);
      return;
    }
    lua_setfield(lua_, index, field);
  };
  font("__ow_btn_normal_font", frame.button_normal_font_style);
  font("__ow_btn_dis_font", frame.button_disabled_font_style);
  font("__ow_btn_hl_font", frame.button_highlight_font_style);
  const auto color = [&](const framexml::UiColor &value, const char *prefix) {
    number((std::string(prefix) + "_r").c_str(), value.r);
    number((std::string(prefix) + "_g").c_str(), value.g);
    number((std::string(prefix) + "_b").c_str(), value.b);
    number((std::string(prefix) + "_a").c_str(), value.a);
  };
  if (frame.button_normal_color)
    color(*frame.button_normal_color, "__ow_btn_normal");
  if (frame.button_disabled_color)
    color(*frame.button_disabled_color, "__ow_btn_disabled");
  if (frame.button_highlight_color)
    color(*frame.button_highlight_color, "__ow_btn_highlight");

  if (frame.status_bar.has_value()) {
    if (auto *bar = dependencies_.frames.FindStatusBar(frame.name); bar != nullptr) {
      const auto &definition = *frame.status_bar;
      if (definition.color) {
        const auto &c = *definition.color;
        bar->SetInitialColor(c.red, c.green, c.blue, c.alpha);
        if (auto attached = bar->TakeInitialColorForAttachedTexture(); attached.has_value()) {
          lua_getfield(lua_, index, "__ow_sb_texture");
          if (lua_istable(lua_, -1))
            (void)frame_api::ApplyTextureVertexColor(lua_, -1, *attached);
          lua_pop(lua_, 1);
        }
      }
      if (definition.minimum && definition.maximum &&
          widgets::ValidateStatusBarRange(*definition.minimum, *definition.maximum) ==
              widgets::StatusBarRangeError::None) {
        (void)bar->SetRange(*definition.minimum, *definition.maximum);
        if (definition.default_value)
          (void)bar->SetValue(*definition.default_value);
      }
      using Orientation = widgets::ParsedStatusBarOrientationKind;
      if (definition.orientation.kind == Orientation::Horizontal)
        bar->SetOrientation(widgets::StatusBarOrientation::Horizontal);
      else if (definition.orientation.kind == Orientation::Vertical)
        bar->SetOrientation(widgets::StatusBarOrientation::Vertical);
      if (definition.rotates_texture)
        bar->SetRotatesTexture(*definition.rotates_texture);
    }
  }
  if (frame.slider.has_value()) {

    const auto &definition = *frame.slider;
    using Orientation = widgets::ParsedSliderOrientationKind;
    if (definition.orientation.kind == Orientation::Horizontal) {
      lua_pushstring(lua_, "HORIZONTAL");
      lua_setfield(lua_, index, "__ow_sl_orient");
    } else if (definition.orientation.kind == Orientation::Vertical) {
      lua_pushstring(lua_, "VERTICAL");
      lua_setfield(lua_, index, "__ow_sl_orient");
    }
    if (definition.minimum.has_value())
      number("__ow_sl_min", *definition.minimum);
    if (definition.maximum.has_value())
      number("__ow_sl_max", *definition.maximum);
    if (definition.value_step.has_value())
      number("__ow_sl_step", *definition.value_step);
    if (definition.default_value.has_value())
      number("__ow_sl_val", *definition.default_value);
  }
  if (frame.runtime_kind == UiFrame::RuntimeKind::ColorSelect) {
    frame_api::InitializeColorSelectTextures(lua_, index,
                                             {.wheel = frame.color_wheel_texture_file,
                                              .wheel_thumb = frame.color_wheel_thumb_texture_file,
                                              .value = frame.color_value_texture_file,
                                              .value_thumb = frame.color_value_thumb_texture_file});
  }
  if (frame.word_wrap)
    boolean("__ow_wordwrap", *frame.word_wrap);
  if (frame.non_space_wrap)
    boolean("__ow_nonspacewrap", *frame.non_space_wrap);
  if (frame.indented_word_wrap)
    boolean("__ow_indented_wrap", *frame.indented_word_wrap);

  PublishFrameAnchorsToLua(lua_, index, frame);
  ApplyAttributes(lua_, index, frame);
}

void FrameMaterializer::WireScriptHandlers(const UiFrame &frame, int ref) {
  if (lua_ == nullptr || frame.script_handlers.empty())
    return;
  const int top = lua_gettop(lua_);
  for (const auto &handler : frame.script_handlers) {
    if (handler.event.empty())
      continue;
    const auto *info = LookupFrameScriptTypeInfo(frame.kind, handler.event);
    if (info == nullptr) {
      diagnostics::Log(diagnostics::LogLevel::kWarn,
                       "Frame " + DisplayName(frame) + ": Unknown script element " + handler.event);
      continue;
    }
    const std::string key = std::string("__ow_script_") + info->canonical_name;
    lua_rawgeti(lua_, LUA_REGISTRYINDEX, ref);
    if (!lua_istable(lua_, -1)) {
      lua_settop(lua_, top);
      return;
    }
    const int frame_index = lua_absindex(lua_, -1);
    lua_pushnil(lua_);
    lua_setfield(lua_, frame_index, key.c_str());
    StoreScriptTaintSource(lua_, frame_index, info->canonical_name, false, 0);
    lua_settop(lua_, top);
    if (handler.function.empty() && handler.body.empty())
      continue;
    std::string error;
    if (!script_cache_.PushResolvedHandler(lua_, handler, framexml::XmlScriptOwner::Frame,
                                           &error)) {
      if (!handler.function.empty() && error == "Unknown function " + handler.function) {
        diagnostics::Log(diagnostics::LogLevel::kWarn,
                         "Frame " + DisplayName(frame) + ": Unknown function " + handler.function +
                             " in element " + handler.event);
      } else if (!error.empty()) {
        diagnostics::Log(diagnostics::LogLevel::kWarn, "Frame " + DisplayName(frame) +
                                                           ": Failed to load script element " +
                                                           handler.event + ": " + error);
      }
      continue;
    }
    lua_rawgeti(lua_, LUA_REGISTRYINDEX, ref);
    if (!lua_istable(lua_, -1)) {
      lua_settop(lua_, top);
      return;
    }
    const int resolved_frame_index = lua_absindex(lua_, -1);
    {

      const openwow::ui::ScopedNeutralLuaExecutionTaint neutral_taint(lua_);
      lua_pushvalue(lua_, -2);
      lua_setfield(lua_, resolved_frame_index, key.c_str());
    }

    StoreScriptTaintSource(lua_, resolved_frame_index, info->canonical_name, true, 0);
    lua_settop(lua_, top);
  }
  lua_rawgeti(lua_, LUA_REGISTRYINDEX, ref);
  if (!lua_istable(lua_, -1)) {
    lua_settop(lua_, top);
    return;
  }
  const int index = lua_absindex(lua_, -1);
  const bool has_update = PushFrameScriptHandler(lua_, index, "OnUpdate");
  auto key = [&]() -> std::string {
    lua_getfield(lua_, index, frame_api::kLuaFrameRuntimeKeyField);
    const char *value = lua_tostring(lua_, -1);
    std::string result = value != nullptr ? value : "";
    lua_pop(lua_, 1);
    return result;
  }();
  if (has_update && !key.empty()) {
    std::uint8_t strata = 3;
    std::int32_t level = 0;
    if (auto *native = dependencies_.frames.FindSimpleFrame(key); native != nullptr) {
      strata = static_cast<std::uint8_t>(native->GetFrameStrata());
      level = native->GetFrameLevel();
    }
    dependencies_.register_on_update(key, strata, level);
  }
  if (has_update)
    lua_pop(lua_, 1);
  if (!key.empty()) {
    if (auto *tracked = dependencies_.frames.FindFrame(key); tracked != nullptr)
      SyncRuntimeMetadata(index, *tracked);
  }
  const auto *on_load = LookupFrameScriptTypeInfo(frame.kind, "OnLoad");
  const auto invocation = InvokeFrameScriptHandler(
      lua_, index, on_load != nullptr ? on_load->canonical_name : "OnLoad", 0);
  if (invocation.status != LUA_OK) {
    const char *error = lua_tostring(lua_, -1);
    diagnostics::Log(diagnostics::LogLevel::kWarn, "FrameMaterializer: OnLoad error for " +
                                                       DisplayName(frame) + ": " +
                                                       (error != nullptr ? error : "(null)"));
    lua_pop(lua_, 1);
  }
  lua_settop(lua_, top);
}

}
