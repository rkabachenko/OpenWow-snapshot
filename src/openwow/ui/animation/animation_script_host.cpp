#include "openwow/ui/animation/animation_script_host.h"

#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/game/framescript/core/frame_script_invocation.h"
#include "openwow/ui/game/secure_execution.h"

namespace openwow::ui::anim {

AnimationScriptHost::~AnimationScriptHost() {
  for (auto& [_, handler] : handlers_) {
    if (handler.lua != nullptr && handler.ref != LUA_NOREF)
      luaL_unref(handler.lua, LUA_REGISTRYINDEX, handler.ref);
  }
  ClearObjectRef();
}

void AnimationScriptHost::SetHandler(const std::string& name, lua_State* lua,
                                     const int ref, const int taint_source) {
  auto& handler = handlers_[name];
  if (handler.lua != nullptr && handler.ref != LUA_NOREF)
    luaL_unref(handler.lua, LUA_REGISTRYINDEX, handler.ref);

  handler = {.lua = lua, .ref = ref, .taint_source = taint_source};
}

int AnimationScriptHost::GetHandlerRef(const std::string& name) const {
  const auto it = handlers_.find(name);
  return it == handlers_.end() ? LUA_NOREF : it->second.ref;
}

int AnimationScriptHost::HandlerTaintSource(const std::string& name) const {
  const auto it = handlers_.find(name);
  return it == handlers_.end() ? 0 : it->second.taint_source;
}

bool AnimationScriptHost::HasHandler(const std::string& name) const {
  return GetHandlerRef(name) != LUA_NOREF;
}

void AnimationScriptHost::SetObjectRef(lua_State* lua, const int ref) {
  ClearObjectRef();
  object_lua_ = lua;
  object_ref_ = ref;
}

void AnimationScriptHost::Fire(const std::string& name, lua_State* fallback_lua,
                               const Argument argument) {
  const auto it = handlers_.find(name);
  if (it == handlers_.end() || it->second.ref == LUA_NOREF)
    return;

  const auto& handler = it->second;
  lua_State* lua = handler.lua != nullptr ? handler.lua : fallback_lua;
  if (lua == nullptr || object_lua_ != lua || object_ref_ == LUA_NOREF)
    return;

  const int original_top = lua_gettop(lua);
  lua_rawgeti(lua, LUA_REGISTRYINDEX, object_ref_);
  const int self_index = lua_absindex(lua, -1);
  lua_rawgeti(lua, LUA_REGISTRYINDEX, handler.ref);

  int argument_count = 0;
  if (lua_isfunction(lua, -1)) {
    if (const auto* number = std::get_if<float>(&argument)) {
      lua_pushnumber(lua, *number);
      argument_count = 1;
    } else if (const auto* boolean = std::get_if<bool>(&argument)) {
      lua_pushboolean(lua, *boolean);
      argument_count = 1;
    } else if (const auto* text = std::get_if<const char*>(&argument)) {
      lua_pushstring(lua, *text != nullptr ? *text : "");
      argument_count = 1;
    }

    (void)openwow::ui::game::InvokeFrameScriptFunction(
        lua, self_index, argument_count,
        openwow::ui::game::FrameScriptInvocationKind::kHandler,
        handler.taint_source != 0
            ? openwow::ui::game::FrameScriptInvocationSecurity::kInsecure
            : openwow::ui::game::FrameScriptInvocationSecurity::kSecure,
        0, handler.taint_source);
  }
  lua_settop(lua, original_top);
}

void AnimationScriptHost::ClearObjectRef() {
  if (object_lua_ != nullptr && object_ref_ != LUA_NOREF)
    luaL_unref(object_lua_, LUA_REGISTRYINDEX, object_ref_);
  object_lua_ = nullptr;
  object_ref_ = LUA_NOREF;
}

}
