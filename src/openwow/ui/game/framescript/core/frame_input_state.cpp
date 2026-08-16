#include "openwow/ui/game/framescript/core/frame_input_state.h"

#include "openwow/ui/game/framescript/core/frame_lua_receiver.h"
#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/game/framescript/core/frame_script_object_runtime.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/lua_c_api_convenience.h"

#include <lua.hpp>

namespace openwow::ui::game::frame_api {
namespace {

using detail::AllowLuaFrameProtectedMutation;
using detail::GetLuaWidgetShownState;
using detail::ScriptReadBoolArgOrDefault;
using detail::lua_pushwowbool;

bool TryReadStoredBooleanField(lua_State *L, int index, const char *field_name, bool *value) {
  index = lua_absindex(L, index);
  lua_getfield(L, index, field_name);
  const bool has_boolean = lua_isboolean(L, -1) != 0;
  if (has_boolean && value != nullptr) {
    *value = lua_toboolean(L, -1) != 0;
  }
  lua_pop(L, 1);
  return has_boolean;
}

bool ReadStoredFrameInputCategoryEnabled(lua_State *L, int self_idx, const char *field_name,
                                         const char *legacy_field_name = nullptr) {
  bool enabled = false;
  if (TryReadStoredBooleanField(L, self_idx, field_name, &enabled)) {
    return enabled;
  }
  if (legacy_field_name != nullptr &&
      TryReadStoredBooleanField(L, self_idx, legacy_field_name, &enabled)) {
    return enabled;
  }
  return false;
}

}

void NotifyFrameInputMutation(lua_State *L, int self_idx, bool reindex_only) {
  auto *manager = runtime::WorldUiRuntimeContext::FromLua(L);
  if (manager == nullptr) {
    return;
  }
  manager->NotifyFrameInputCategoryMutation(GetFrameManagerKey(L, self_idx), reindex_only);
}

bool GetLuaBooleanField(lua_State *L, int table_index, const char *field) {
  lua_getfield(L, table_index, field);
  const bool result = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  return result;
}

int SetValidatedFrameShownState(lua_State *L, const bool shown) {
  const int self_idx = ValidateFrameSelf(L);
  const bool changed = shown ? detail::ShowLuaScriptFrame(L, self_idx)
                             : detail::HideLuaScriptFrame(L, self_idx);
  if (changed) {
    NotifyFrameInputMutation(L, self_idx, true);
  }
  return 0;
}

int PushValidatedFrameShownState(lua_State *L) {
  const int self_idx = ValidateFrameSelf(L);
  lua_pushwowbool(L, GetLuaWidgetShownState(L, self_idx));
  return 1;
}

int SetFrameInputCategoryEnabled(lua_State *L, const char *field_name) {
  const int self_idx = ValidateFrameResizeSelf(L);
  if (!AllowLuaFrameProtectedMutation(L, self_idx)) {
    return 0;
  }

  lua_pushboolean(L, ScriptReadBoolArgOrDefault(L, 2, true));
  lua_setfield(L, self_idx, field_name);
  NotifyFrameInputMutation(L, self_idx, true);
  return 0;
}

int PushFrameInputCategoryEnabled(lua_State *L, const char *field_name,
                                  const char *legacy_field_name) {
  const int self_idx = ValidateFrameResizeSelf(L);
  lua_pushwowbool(L,
                  ReadStoredFrameInputCategoryEnabled(L, self_idx, field_name, legacy_field_name));
  return 1;
}

}
