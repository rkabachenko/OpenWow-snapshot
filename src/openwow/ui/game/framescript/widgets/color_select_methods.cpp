#include "openwow/ui/game/framescript/widgets/color_select_methods.h"

#include "openwow/ui/framexml/texture_role.h"
#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/game/framescript/core/frame_draw_layer_state.h"

#include "openwow/ui/game/framescript/core/frame_script_invocation.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/framescript/core/script_region_ownership.h"
#include "openwow/ui/game/framescript/core/frame_lua_object_tree.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/widgets/color_select_state.h"
#include "openwow/ui/widgets/script_object.h"

#include <lua.hpp>

namespace openwow::ui::game::frame_api {
namespace {

constexpr char kStateRegistryKey[] = "openwow.game.color_select_states";

enum class TextureSlot : int {
  Wheel = 1,
  WheelThumb,
  Value,
  ValueThumb,
};

struct TextureSlotPolicy {
  const char* draw_layer;
  openwow::ui::framexml::TextureRole role;
  bool clears_anchors;
};

constexpr TextureSlotPolicy GetTextureSlotPolicy(const TextureSlot slot) {
  switch (slot) {
    case TextureSlot::Wheel:
      return {.draw_layer = "ARTWORK",
              .role = openwow::ui::framexml::TextureRole::ColorSelectWheel,
              .clears_anchors = false};
    case TextureSlot::Value:
      return {.draw_layer = "ARTWORK",
              .role = openwow::ui::framexml::TextureRole::ColorSelectValue,
              .clears_anchors = false};
    case TextureSlot::WheelThumb:
      return {.draw_layer = "OVERLAY",
              .role = openwow::ui::framexml::TextureRole::ColorSelectWheelThumb,
              .clears_anchors = true};
    case TextureSlot::ValueThumb:
      return {.draw_layer = "OVERLAY",
              .role = openwow::ui::framexml::TextureRole::ColorSelectValueThumb,
              .clears_anchors = true};
  }
  return {.draw_layer = "ARTWORK",
          .role = openwow::ui::framexml::TextureRole::Normal,
          .clears_anchors = false};
}

int ValidateColorSelect(lua_State* lua) {
  if (lua_istable(lua, 1) == 0) {
    return luaL_error(
        lua,
        "Attempt to find 'this' in non-table object (used '.' instead of ':' ?)");
  }
  (void)detail::CanonicalizeLuaScriptObjectTable(lua, 1);
  if (detail::GetLuaCanonicalScriptObjectType(lua, 1) !=
      widgets::ScriptObjectType::ColorSelect) {
    return luaL_error(lua, "Wrong object type for member function");
  }
  return lua_absindex(lua, 1);
}

int PushStateRegistry(lua_State* lua) {
  lua_getfield(lua, LUA_REGISTRYINDEX, kStateRegistryKey);
  if (lua_istable(lua, -1) != 0) {
    return lua_absindex(lua, -1);
  }

  lua_pop(lua, 1);
  lua_newtable(lua);
  const int registry = lua_absindex(lua, -1);
  lua_newtable(lua);
  lua_pushliteral(lua, "k");
  lua_setfield(lua, -2, "__mode");
  lua_setmetatable(lua, registry);
  lua_pushvalue(lua, registry);
  lua_setfield(lua, LUA_REGISTRYINDEX, kStateRegistryKey);
  return registry;
}

widgets::ColorSelectState& PushColorSelectState(lua_State* lua) {
  const int self = ValidateColorSelect(lua);
  const int registry = PushStateRegistry(lua);
  lua_pushvalue(lua, self);
  lua_rawget(lua, registry);
  if (auto* state =
          static_cast<widgets::ColorSelectState*>(lua_touserdata(lua, -1));
      state != nullptr) {
    lua_remove(lua, registry);
    return *state;
  }

  lua_pop(lua, 1);
  auto* state = static_cast<widgets::ColorSelectState*>(
      lua_newuserdata(lua, sizeof(widgets::ColorSelectState)));
  *state = widgets::ColorSelectState{};
  lua_newtable(lua);
  lua_setfenv(lua, -2);

  lua_pushvalue(lua, self);
  lua_pushvalue(lua, -2);
  lua_rawset(lua, registry);
  lua_remove(lua, registry);
  return *state;
}

void FireColorSelected(lua_State* lua,
                       const widgets::ColorSelectState& state) {
  const auto rgb = state.rgb();
  lua_pushnumber(lua, rgb.red);
  lua_pushnumber(lua, rgb.green);
  lua_pushnumber(lua, rgb.blue);
  const auto result = InvokeFrameScriptHandler(lua, 1, "OnColorSelect", 3);
  if (result.status != LUA_OK) {
    lua_pop(lua, 1);
  }
}

int LuaGetColorRgb(lua_State* lua) {
  const auto& state = PushColorSelectState(lua);
  const auto rgb = state.rgb();
  lua_pushnumber(lua, rgb.red);
  lua_pushnumber(lua, rgb.green);
  lua_pushnumber(lua, rgb.blue);
  return 3;
}

int LuaSetColorRgb(lua_State* lua) {
  const int argument_count = lua_gettop(lua);
  auto& state = PushColorSelectState(lua);
  const double red = luaL_checknumber(lua, 2);
  const double green = luaL_checknumber(lua, 3);
  const double blue = luaL_checknumber(lua, 4);
  if (argument_count >= 5) {
    (void)luaL_checknumber(lua, 5);
  }
  state.SetRgb(red, green, blue);
  FireColorSelected(lua, state);
  return 0;
}

int LuaGetColorHsv(lua_State* lua) {
  const auto& state = PushColorSelectState(lua);
  const auto& hsv = state.hsv();
  lua_pushnumber(lua, hsv.hue);
  lua_pushnumber(lua, hsv.saturation);
  lua_pushnumber(lua, hsv.value);
  return 3;
}

int LuaSetColorHsv(lua_State* lua) {
  auto& state = PushColorSelectState(lua);
  state.SetHsv(static_cast<float>(luaL_checknumber(lua, 2)),
               static_cast<float>(luaL_checknumber(lua, 3)),
               static_cast<float>(luaL_checknumber(lua, 4)));
  FireColorSelected(lua, state);
  return 0;
}

void CallTextureMethod(lua_State* lua, const int texture_index,
                       const char* method) {
  const int texture = lua_absindex(lua, texture_index);
  lua_getfield(lua, texture, method);
  if (lua_isfunction(lua, -1) == 0) {
    lua_pop(lua, 1);
    return;
  }
  lua_pushvalue(lua, texture);
  lua_call(lua, 1, 0);
}

void AttachTexture(lua_State* lua, const int texture_index,
                   const TextureSlot slot) {
  const int texture = lua_absindex(lua, texture_index);
  const TextureSlotPolicy policy = GetTextureSlotPolicy(slot);
  ReparentScriptObjectTable(lua, texture, 1);
  SynchronizeTextureRole(lua, texture, policy.role);
  lua_pushstring(lua, policy.draw_layer);
  lua_setfield(lua, texture, "__ow_draw_layer");
  SyncRegionDrawLayerEnabled(lua, texture);
  if (policy.clears_anchors) {
    CallTextureMethod(lua, texture, "ClearAllPoints");
  }
}

void StoreTexture(lua_State* lua, const int state_index,
                  const TextureSlot slot, const int value_index) {
  const int state = lua_absindex(lua, state_index);
  const int value = lua_absindex(lua, value_index);
  lua_getfenv(lua, state);
  const int environment = lua_absindex(lua, -1);
  lua_rawgeti(lua, environment, static_cast<int>(slot));
  const bool unchanged = lua_rawequal(lua, -1, value) != 0;
  if (!unchanged && lua_istable(lua, -1) != 0) {
    ReparentScriptObjectTable(lua, -1, 0);
  }
  lua_pop(lua, 1);
  if (unchanged) {
    lua_pop(lua, 2);
    return;
  }
  if (lua_istable(lua, value) != 0) {
    AttachTexture(lua, value, slot);
  }
  lua_pushvalue(lua, value);
  lua_rawseti(lua, environment, static_cast<int>(slot));
  lua_pop(lua, 2);
}

int PushTexture(lua_State* lua, const TextureSlot slot) {
  (void)PushColorSelectState(lua);
  lua_getfenv(lua, -1);
  lua_rawgeti(lua, -1, static_cast<int>(slot));
  lua_remove(lua, -2);
  lua_remove(lua, -2);
  return 1;
}

bool IsTexture(lua_State* lua, const int index) {
  return lua_istable(lua, index) != 0 &&
         detail::GetLuaCanonicalScriptObjectType(lua, index) ==
             widgets::ScriptObjectType::Texture;
}

int CreateThumbTexture(lua_State* lua) {
  lua_getfield(lua, 1, "CreateTexture");
  lua_pushvalue(lua, 1);
  lua_pushnil(lua);
  lua_pushliteral(lua, "OVERLAY");
  lua_call(lua, 3, 1);
  const int texture = lua_absindex(lua, -1);

  lua_getfield(lua, texture, "SetTexture");
  lua_pushvalue(lua, texture);
  lua_pushvalue(lua, 2);
  lua_call(lua, 2, 0);
  return texture;
}

int SetTexture(lua_State* lua, const TextureSlot slot, const char* method,
  const bool allow_path) {
  const int argument_count = lua_gettop(lua);
  (void)PushColorSelectState(lua);
  const int state = lua_absindex(lua, -1);
  int texture = 2;
  if (lua_isnil(lua, 2) != 0) {
    StoreTexture(lua, state, slot, 2);
    lua_settop(lua, argument_count);
    return 0;
  }
  if (allow_path && lua_isstring(lua, 2) != 0) {
    texture = CreateThumbTexture(lua);
  } else if (!IsTexture(lua, 2)) {
    const char* name = openwow::ui::BorrowRawLuaStringField(lua, 1, "__ow_name");
    return luaL_error(lua, "Usage: %s:%s(%s)",
                      name != nullptr ? name : "<unnamed>",
                      method,
                      allow_path ? "texture or \"texture\" or nil"
                                 : "texture or nil");
  }
  StoreTexture(lua, state, slot, texture);
  lua_settop(lua, argument_count);
  return 0;
}

int LuaGetColorWheelTexture(lua_State* lua) {
  return PushTexture(lua, TextureSlot::Wheel);
}
int LuaSetColorWheelTexture(lua_State* lua) {
  return SetTexture(lua, TextureSlot::Wheel, "SetColorWheelTexture", false);
}
int LuaGetColorWheelThumbTexture(lua_State* lua) {
  return PushTexture(lua, TextureSlot::WheelThumb);
}
int LuaSetColorWheelThumbTexture(lua_State* lua) {
  return SetTexture(lua, TextureSlot::WheelThumb,
                    "SetColorWheelThumbTexture", true);
}
int LuaGetColorValueTexture(lua_State* lua) {
  return PushTexture(lua, TextureSlot::Value);
}
int LuaSetColorValueTexture(lua_State* lua) {
  return SetTexture(lua, TextureSlot::Value, "SetColorValueTexture", false);
}
int LuaGetColorValueThumbTexture(lua_State* lua) {
  return PushTexture(lua, TextureSlot::ValueThumb);
}
int LuaSetColorValueThumbTexture(lua_State* lua) {
  return SetTexture(lua, TextureSlot::ValueThumb,
                    "SetColorValueThumbTexture", true);
}

constexpr luaL_Reg kColorSelectMethods[] = {
    {"GetColorHSV", LuaGetColorHsv},
    {"GetColorRGB", LuaGetColorRgb},
    {"GetColorValueTexture", LuaGetColorValueTexture},
    {"GetColorValueThumbTexture", LuaGetColorValueThumbTexture},
    {"GetColorWheelTexture", LuaGetColorWheelTexture},
    {"GetColorWheelThumbTexture", LuaGetColorWheelThumbTexture},
    {"SetColorHSV", LuaSetColorHsv},
    {"SetColorRGB", LuaSetColorRgb},
    {"SetColorValueTexture", LuaSetColorValueTexture},
    {"SetColorValueThumbTexture", LuaSetColorValueThumbTexture},
    {"SetColorWheelTexture", LuaSetColorWheelTexture},
    {"SetColorWheelThumbTexture", LuaSetColorWheelThumbTexture},
    {nullptr, nullptr},
};

}

void ApplyColorSelectMethods(lua_State* lua) {
  luaL_setfuncs(lua, kColorSelectMethods, 0);
}

void InitializeColorSelectTextures(lua_State* lua, int frame_index,
                                   const ColorSelectTexturePaths& paths) {
  if (lua == nullptr || lua_istable(lua, frame_index) == 0) {
    return;
  }
  frame_index = lua_absindex(lua, frame_index);
  const int stack_base = lua_gettop(lua);

  const auto set_path = [&](const std::string_view path, const char* setter,
                            const bool setter_accepts_path) {
    if (path.empty()) {
      return;
    }
    lua_getfield(lua, frame_index, setter);
    lua_pushvalue(lua, frame_index);
    if (setter_accepts_path) {
      lua_pushlstring(lua, path.data(), path.size());
    } else {
      lua_getfield(lua, frame_index, "CreateTexture");
      lua_pushvalue(lua, frame_index);
      lua_pushnil(lua);
      lua_pushliteral(lua, "ARTWORK");
      lua_call(lua, 3, 1);
      const int texture = lua_absindex(lua, -1);
      lua_getfield(lua, texture, "SetTexture");
      lua_pushvalue(lua, texture);
      lua_pushlstring(lua, path.data(), path.size());
      lua_call(lua, 2, 0);
    }
    lua_call(lua, 2, 0);
  };

  set_path(paths.wheel, "SetColorWheelTexture", false);
  set_path(paths.wheel_thumb, "SetColorWheelThumbTexture", true);
  set_path(paths.value, "SetColorValueTexture", false);
  set_path(paths.value_thumb, "SetColorValueThumbTexture", true);
  lua_settop(lua, stack_base);
}

}
