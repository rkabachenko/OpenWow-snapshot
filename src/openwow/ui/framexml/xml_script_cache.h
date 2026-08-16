#pragma once

#include "openwow/ui/animation/animation_xml.h"
#include "openwow/ui/frame_script_type_info.h"
#include "openwow/ui/framexml/ui_frame.h"
#include "openwow/ui/lua_taint_api.h"

#include <string>
#include <string_view>
#include <unordered_map>

extern "C" {
#include <lua.hpp>
}

namespace openwow::ui::framexml {

enum class XmlScriptOwner {
  Frame,
  Animation,
  AnimationGroup,
};

inline const char *GetXmlScriptWrapperFormat(XmlScriptOwner owner, std::string_view event_name) {
  if (owner == XmlScriptOwner::Frame) {
    return openwow::ui::GetAnyFrameScriptWrapperFormat(event_name);
  }

  const std::string event_name_copy(event_name);
  const auto *slot = owner == XmlScriptOwner::Animation
                         ? openwow::ui::anim::GetAnimScriptSlot(event_name_copy.c_str())
                         : openwow::ui::anim::GetAnimGroupScriptSlot(event_name_copy.c_str());

  if (slot == nullptr) {
    return nullptr;
  }

  return slot->wrapper_format;
}

class XmlScriptCache {
public:
  XmlScriptCache() = default;
  ~XmlScriptCache() {
    Clear();
  }

  XmlScriptCache(const XmlScriptCache &) = delete;
  XmlScriptCache &operator=(const XmlScriptCache &) = delete;

  bool HasCachedInlineHandler(lua_State *L, const ScriptHandler &handler) const {
    if (L == nullptr) {
      return false;
    }

    return compiled_refs_.find(CacheKey{L, handler.cache_key.get()}) != compiled_refs_.end();
  }

  bool EnsureInlineHandlerCompiled(lua_State *L, const ScriptHandler &handler, XmlScriptOwner owner,
                                   std::string *error = nullptr) {
    if (L == nullptr) {
      if (error != nullptr) {
        *error = "Lua state is null";
      }
      return false;
    }

    if (handler.body.empty()) {
      return true;
    }

    if (HasCachedInlineHandler(L, handler)) {
      return true;
    }

    const char *wrapper_format = GetXmlScriptWrapperFormat(owner, handler.event);
    if (wrapper_format == nullptr) {
      if (error != nullptr) {
        *error = "Unsupported script handler " + handler.event;
      }
      return false;
    }

    const openwow::ui::ScopedLuaExecutionTaintSource declaring_scope(
        L, handler.declaring_taint_source);

    std::string chunk = wrapper_format;
    if (const auto marker = chunk.find("%s"); marker != std::string::npos) {

      chunk.replace(marker, 2, handler.body + "\n");
    } else {
      chunk.append(handler.body);
    }

    const std::string chunk_name = "*:" + handler.event;
    if (luaL_loadbuffer(L, chunk.c_str(), chunk.size(), chunk_name.c_str()) != 0) {
      if (error != nullptr) {
        const char *text = lua_tostring(L, -1);
        *error = text != nullptr ? text : "luaL_loadbuffer failed";
      }
      lua_pop(L, 1);
      return false;
    }

    if (lua_pcall(L, 0, 1, 0) != 0) {
      if (error != nullptr) {
        const char *text = lua_tostring(L, -1);
        *error = text != nullptr ? text : "lua_pcall failed";
      }
      lua_pop(L, 1);
      return false;
    }

    if (lua_isfunction(L, -1) == 0) {
      if (error != nullptr) {
        *error = "XML script wrapper did not return a function";
      }
      lua_pop(L, 1);
      return false;
    }

    lua_pushvalue(L, -1);
    compiled_refs_.emplace(CacheKey{L, handler.cache_key.get()}, luaL_ref(L, LUA_REGISTRYINDEX));
    lua_pop(L, 1);
    return true;
  }

  bool PushResolvedHandler(lua_State *L, const ScriptHandler &handler, XmlScriptOwner owner,
                           std::string *error = nullptr) {
    if (L == nullptr) {
      if (error != nullptr) {
        *error = "Lua state is null";
      }
      return false;
    }

    if (GetXmlScriptWrapperFormat(owner, handler.event) == nullptr) {
      if (error != nullptr) {
        *error = "Unsupported script handler " + handler.event;
      }
      return false;
    }

    if (!EnsureInlineHandlerCompiled(L, handler, owner, error)) {
      return false;
    }

    if (!handler.function.empty()) {

      const openwow::ui::ScopedLuaExecutionTaintSource declaring_scope(
          L, handler.declaring_taint_source);
      lua_getglobal(L, handler.function.c_str());
      if (lua_isfunction(L, -1) != 0) {
        return true;
      }
      lua_pop(L, 1);

      lua_pushlstring(L, handler.function.data(), handler.function.size());
      lua_pushinteger(L, handler.declaring_taint_source);
      lua_pushcclosure(L, &DispatchDeferredNamedHandler, 2);
      return true;
    }

    if (handler.body.empty()) {
      if (error != nullptr) {
        *error = "Script body is empty";
      }
      return false;
    }

    if (const auto it = compiled_refs_.find(CacheKey{L, handler.cache_key.get()});
        it != compiled_refs_.end()) {
      lua_rawgeti(L, LUA_REGISTRYINDEX, it->second);
      return true;
    }
    if (error != nullptr) {
      *error = "Inline handler cache missing for " + handler.event;
    }
    return false;
  }

  void ClearState(lua_State *L) {
    for (auto it = compiled_refs_.begin(); it != compiled_refs_.end();) {
      if (it->first.lua_state == L) {
        if (L != nullptr && it->second != LUA_NOREF) {
          luaL_unref(L, LUA_REGISTRYINDEX, it->second);
        }
        it = compiled_refs_.erase(it);
      } else {
        ++it;
      }
    }
  }

  void Clear() {
    for (const auto &[key, ref] : compiled_refs_) {
      if (key.lua_state != nullptr && ref != LUA_NOREF) {
        luaL_unref(key.lua_state, LUA_REGISTRYINDEX, ref);
      }
    }
    compiled_refs_.clear();
  }

private:
  static int DispatchDeferredNamedHandler(lua_State *L) {
    const int argument_count = lua_gettop(L);
    const char *const function_name = lua_tostring(L, lua_upvalueindex(1));
    const auto declaring_source =
        static_cast<int>(lua_tointeger(L, lua_upvalueindex(2)));
    if (function_name == nullptr) {
      return 0;
    }

    int resolved_source = declaring_source;
    {
      const openwow::ui::ScopedLuaExecutionTaintSource declaring_scope(
          L, declaring_source);
      lua_getglobal(L, function_name);
      if (lua_isfunction(L, -1) == 0) {
        lua_pop(L, 1);
        return 0;
      }
      resolved_source = openwow::ui::lua_get_taint(L, -1);
    }
    lua_insert(L, 1);
    const openwow::ui::ScopedLuaExecutionTaintSource call_scope(L,
                                                                resolved_source);
    if (lua_pcall(L, argument_count, LUA_MULTRET, 0) != 0) {
      return lua_error(L);
    }
    return lua_gettop(L);
  }

  struct CacheKey {
    lua_State *lua_state;
    const void *identity;

    bool operator==(const CacheKey &other) const {
      return lua_state == other.lua_state && identity == other.identity;
    }
  };

  struct CacheKeyHash {
    std::size_t operator()(const CacheKey &key) const {
      const auto lhs = reinterpret_cast<std::uintptr_t>(key.lua_state);
      const auto rhs = reinterpret_cast<std::uintptr_t>(key.identity);
      return std::hash<std::uintptr_t>{}(lhs ^ (rhs << 1));
    }
  };

  std::unordered_map<CacheKey, int, CacheKeyHash> compiled_refs_;
};

}
