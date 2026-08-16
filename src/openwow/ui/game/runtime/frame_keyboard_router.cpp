#include "openwow/ui/game/runtime/frame_input_router.h"

#include "openwow/foundation/text/utf8.h"
#include "openwow/game/actions/bindings/adapters/platform/sdl_binding_input.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/framescript/core/frame_script_invocation.h"
#include "openwow/ui/game/framescript/core/script_region_ownership.h"
#include "openwow/ui/game/framescript/widgets/edit_box_methods.h"
#include "openwow/ui/game/framescript/widgets/edit_box_state.h"
#include "openwow/ui/game/runtime/edit_box_input.h"
#include "openwow/ui/game/runtime/frame_store.h"
#include "openwow/ui/game/runtime/frame_traversal_index.h"

#include <lua.hpp>

#include <algorithm>
#include <string_view>

namespace openwow::ui::game::runtime {
namespace {

template <typename PushArguments>
bool FireFrameHandler(lua_State* state, int frame_ref, const char* handler,
                      int argument_count, PushArguments&& push_arguments) {
  const int top = lua_gettop(state);
  lua_rawgeti(state, LUA_REGISTRYINDEX, frame_ref);
  if (lua_istable(state, -1) == 0) {
    lua_settop(state, top);
    return false;
  }
  const int frame_index = lua_absindex(state, -1);
  push_arguments();
  const auto invocation = InvokeFrameScriptHandler(
      state, frame_index, handler, argument_count);
  if (invocation.status != LUA_OK) {
    lua_pop(state, 1);
  }
  lua_settop(state, top);
  return invocation.invoked;
}

bool FireNoArg(lua_State* state, int frame_ref, const char* handler) {
  return FireFrameHandler(state, frame_ref, handler, 0, [] {});
}

bool FireString(lua_State* state, int frame_ref, const char* handler,
                std::string_view argument) {
  return FireFrameHandler(state, frame_ref, handler, 1, [&] {
    lua_pushlstring(state, argument.data(), argument.size());
  });
}

bool GetOptionalBooleanField(lua_State* state, int index, const char* field,
                             bool* value) {
  index = lua_absindex(state, index);
  lua_getfield(state, index, field);
  const bool present = lua_isboolean(state, -1) != 0;
  if (present) {
    *value = lua_toboolean(state, -1) != 0;
  }
  lua_pop(state, 1);
  return present;
}

bool FrameUsesKeyboard(lua_State* state, const FrameStore& frames,
                       std::string_view frame_name) {
  const auto ref = frames.FindLuaRef(frame_name);
  if (!ref.has_value()) {
    return false;
  }
  const int top = lua_gettop(state);
  lua_rawgeti(state, LUA_REGISTRYINDEX, *ref);
  bool enabled = false;
  if (lua_istable(state, -1) != 0) {
    (void)(GetOptionalBooleanField(state, -1, "__ow_keyboard_enabled",
                                   &enabled) ||
           GetOptionalBooleanField(state, -1, "__ow_enableKeyboard",
                                   &enabled));
  }
  lua_settop(state, top);
  return enabled;
}

bool FrameIsShown(lua_State* state, const FrameStore& frames,
                  std::string_view frame_name) {
  const auto ref = frames.FindLuaRef(frame_name);
  if (!ref.has_value()) {
    const auto* frame = frames.FindFrame(frame_name);
    return frame != nullptr && frame->visible;
  }
  const int top = lua_gettop(state);
  lua_rawgeti(state, LUA_REGISTRYINDEX, *ref);
  const bool shown = lua_istable(state, -1) != 0 &&
                     detail::GetLuaWidgetShownState(state, -1);
  lua_settop(state, top);
  return shown;
}

int ReadInteger(lua_State* state, int frame_index, const char* field,
                int fallback) {
  lua_getfield(state, frame_index, field);
  const int value = lua_isnumber(state, -1) != 0
                        ? static_cast<int>(lua_tointeger(state, -1))
                        : fallback;
  lua_pop(state, 1);
  return value;
}

bool ReadBoolean(lua_State* state, int frame_index, const char* field) {
  lua_getfield(state, frame_index, field);
  const bool value = lua_toboolean(state, -1) != 0;
  lua_pop(state, 1);
  return value;
}

LuaEditBoxInputState ReadEditBoxInputState(lua_State* state, int frame_index) {
  LuaEditBoxInputState result;
  lua_getfield(state, frame_index, "__ow_eb_text");
  std::size_t text_size = 0;
  if (const char* text = lua_tolstring(state, -1, &text_size); text != nullptr) {
    result.text.assign(text, text_size);
  }
  lua_pop(state, 1);
  result.cursor = ReadInteger(state, frame_index, "__ow_eb_cursor",
                              static_cast<int>(result.text.size()));
  result.cursor = openwow::text::ClampUtf8ByteIndex(result.text, result.cursor);
  result.selection_start =
      ReadInteger(state, frame_index, "__ow_eb_hl_start", result.cursor);
  result.selection_end =
      ReadInteger(state, frame_index, "__ow_eb_hl_end", result.cursor);
  if (result.selection_start < 0) {
    result.selection_start = static_cast<int>(result.text.size());
  }
  if (result.selection_end < 0) {
    result.selection_end = static_cast<int>(result.text.size());
  }
  result.selection_start = openwow::text::ClampUtf8ByteIndex(
      result.text, result.selection_start);
  result.selection_end = openwow::text::ClampUtf8ByteIndex(
      result.text, result.selection_end);
  if (result.selection_start > result.selection_end) {
    std::swap(result.selection_start, result.selection_end);
  }
  result.max_letters =
      ReadInteger(state, frame_index, "__ow_eb_maxletters", 0);
  result.max_bytes = ReadInteger(state, frame_index, "__ow_eb_maxbytes", 0);
  result.count_invisible =
      ReadBoolean(state, frame_index, "__ow_eb_countinvis");
  result.numeric = ReadBoolean(state, frame_index, "__ow_eb_numeric");
  result.multiline = ReadBoolean(state, frame_index, "__ow_eb_multiline");
  return result;
}

void StoreEditBoxInputState(lua_State* state, int frame_index,
                            const LuaEditBoxInputState& value) {
  lua_pushlstring(state, value.text.data(), value.text.size());
  lua_setfield(state, frame_index, "__ow_eb_text");
  lua_pushinteger(state, value.cursor);
  lua_setfield(state, frame_index, "__ow_eb_cursor");
  lua_pushinteger(state, value.selection_start);
  lua_setfield(state, frame_index, "__ow_eb_hl_start");
  lua_pushinteger(state, value.selection_end);
  lua_setfield(state, frame_index, "__ow_eb_hl_end");
  frame_api::SyncEditBoxInternalDisplayText(state, frame_index);
}

}

bool FrameInputRouter::HandleKeyDown(std::uint32_t key, bool shift_down,
                                     bool ctrl_down) {
  if (lua_ == nullptr) {
    return false;
  }
  RebuildTraversalIfDirty();
  UpdateFocusedEditBoxInputLanguage();
  const std::string key_name =
      openwow::game::actions::bindings::adapters::platform::
          SdlScancodeToBaseKey(static_cast<int>(key));

  if (key_name == "ESCAPE" && !focused_frame_.empty()) {
    const std::string focus_owner = focused_frame_;
    if (const auto ref = frames_.FindLuaRef(focus_owner); ref.has_value()) {
      (void)FireNoArg(lua_, *ref, "OnEscapePressed");
    }
    if (focused_frame_ == focus_owner) {
      TransitionKeyboardFocus("");
    }
    return true;
  }

  if (!focused_frame_.empty() &&
      FrameIsShown(lua_, frames_, focused_frame_)) {
    const auto ref = frames_.FindLuaRef(focused_frame_);
    if (ref.has_value()) {
      const bool key_handler_invoked =
          FrameUsesKeyboard(lua_, frames_, focused_frame_) &&
          FireString(lua_, *ref, "OnKeyDown", key_name);
      if (IsEditBoxEditingKey(key_name, ctrl_down)) {
        const int top = lua_gettop(lua_);
        lua_rawgeti(lua_, LUA_REGISTRYINDEX, *ref);
        if (lua_istable(lua_, -1) != 0) {
          const int frame_index = lua_absindex(lua_, -1);
          auto edit = ReadEditBoxInputState(lua_, frame_index);
          const bool text_changed = ApplyEditBoxEditingKey(
              edit, key_name, shift_down, ctrl_down);
          StoreEditBoxInputState(lua_, frame_index, edit);
          QueueEditBoxDirtyState(lua_, frame_index, text_changed, text_changed,
                                 true);
          lua_settop(lua_, top);
          return true;
        }
        lua_settop(lua_, top);
      }

      const char* special_handler = nullptr;
      if (key_name == "ENTER") {
        special_handler = "OnEnterPressed";
      } else if (key_name == "TAB") {
        special_handler = "OnTabPressed";
      } else if (key_name == "SPACE") {
        special_handler = "OnSpacePressed";
      }
      if (special_handler != nullptr &&
          FireNoArg(lua_, *ref, special_handler)) {
        return true;
      }
      if (key_handler_invoked || !focused_frame_.empty()) {
        return true;
      }
    }
  }

  for (const auto& entry : traversal_.input_snapshot()) {
    if (entry.effective_visible && entry.uses_keyboard &&
        entry.lua_ref != LUA_NOREF &&
        FireString(lua_, entry.lua_ref, "OnKeyDown", key_name)) {
      return true;
    }
  }
  return false;
}

bool FrameInputRouter::HandleKeyUp(std::uint32_t key) {
  if (lua_ == nullptr) {
    return false;
  }
  RebuildTraversalIfDirty();
  const std::string key_name =
      openwow::game::actions::bindings::adapters::platform::
          SdlScancodeToBaseKey(static_cast<int>(key));
  if (!focused_frame_.empty() &&
      FrameIsShown(lua_, frames_, focused_frame_)) {
    if (const auto ref = frames_.FindLuaRef(focused_frame_); ref.has_value() &&
        FrameUsesKeyboard(lua_, frames_, focused_frame_) &&
        FireString(lua_, *ref, "OnKeyUp", key_name)) {
      return true;
    }
    return true;
  }
  for (const auto& entry : traversal_.input_snapshot()) {
    if (entry.effective_visible && entry.uses_keyboard &&
        entry.lua_ref != LUA_NOREF &&
        FireString(lua_, entry.lua_ref, "OnKeyUp", key_name)) {
      return true;
    }
  }
  return false;
}

bool FrameInputRouter::HandleTextInput(const char* text) {
  if (lua_ == nullptr || text == nullptr || text[0] == '\0') {
    return false;
  }
  UpdateFocusedEditBoxInputLanguage();
  if (focused_frame_.empty() ||
      !FrameIsShown(lua_, frames_, focused_frame_)) {
    return false;
  }
  const auto ref = frames_.FindLuaRef(focused_frame_);
  if (!ref.has_value()) {
    return false;
  }
  const std::string utf8_text = text;
  for (int offset = 0; offset < static_cast<int>(utf8_text.size());) {
    const int next = openwow::text::Utf8NextByteIndex(utf8_text, offset);
    if (next <= offset) {
      break;
    }
    const std::string_view codepoint = std::string_view(utf8_text).substr(
        static_cast<std::size_t>(offset),
        static_cast<std::size_t>(next - offset));
    const int top = lua_gettop(lua_);
    lua_rawgeti(lua_, LUA_REGISTRYINDEX, *ref);
    if (lua_istable(lua_, -1) != 0) {
      const int frame_index = lua_absindex(lua_, -1);
      auto edit = ReadEditBoxInputState(lua_, frame_index);
      const auto mutation = ApplyEditBoxTextInput(edit, codepoint);
      if (mutation.state_changed) {
        StoreEditBoxInputState(lua_, frame_index, edit);
        QueueEditBoxDirtyState(lua_, frame_index, true, true, true);
      }
      lua_settop(lua_, top);
      if (mutation.accepted) {
        (void)FireString(lua_, *ref, "OnChar", codepoint);
      }
    } else {
      lua_settop(lua_, top);
    }
    offset = next;
  }
  return true;
}

}
