#pragma once

#include <string>
#include <unordered_map>
#include <variant>

extern "C" {
#include <lua.hpp>
}

namespace openwow::ui::anim {

class AnimationScriptHost {
 public:
  using Argument = std::variant<std::monostate, float, bool, const char*>;

  AnimationScriptHost() = default;
  ~AnimationScriptHost();
  AnimationScriptHost(const AnimationScriptHost&) = delete;
  AnimationScriptHost& operator=(const AnimationScriptHost&) = delete;

  void SetHandler(const std::string& handler, lua_State* lua, int ref,
                  int taint_source);
  [[nodiscard]] int GetHandlerRef(const std::string& handler) const;

  [[nodiscard]] int HandlerTaintSource(const std::string& handler) const;
  [[nodiscard]] bool HasHandler(const std::string& handler) const;

  void SetObjectRef(lua_State* lua, int ref);
  [[nodiscard]] lua_State* ObjectLua() const { return object_lua_; }
  [[nodiscard]] int ObjectRef() const { return object_ref_; }
  void Fire(const std::string& handler, lua_State* fallback_lua,
            Argument argument = {});

 private:

  struct Handler {
    lua_State* lua = nullptr;
    int ref = LUA_NOREF;
    int taint_source = 0;
  };

  void ClearObjectRef();

  std::unordered_map<std::string, Handler> handlers_;
  lua_State* object_lua_{nullptr};
  int object_ref_{LUA_NOREF};
};

}
