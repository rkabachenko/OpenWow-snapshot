
#pragma once

#include <cstdint>

struct lua_State;

namespace openwow::ui {

void lua_set_taint(lua_State* L, int idx, int taint);

int lua_get_taint(lua_State* L, int idx);

struct LuaExecutionTaintState {
  int source{0};
  std::uint32_t tracking_depth{0};
};

[[nodiscard]] LuaExecutionTaintState lua_get_execution_taint_state(lua_State* L);
void lua_set_execution_taint_state(lua_State* L, LuaExecutionTaintState state);

class ScopedNeutralLuaExecutionTaint final {
 public:
  explicit ScopedNeutralLuaExecutionTaint(lua_State* const L)
      : lua_(L), saved_(lua_get_execution_taint_state(L)) {
    lua_set_execution_taint_state(lua_, {});
  }
  ~ScopedNeutralLuaExecutionTaint() {
    lua_set_execution_taint_state(lua_, saved_);
  }
  ScopedNeutralLuaExecutionTaint(const ScopedNeutralLuaExecutionTaint&) = delete;
  ScopedNeutralLuaExecutionTaint& operator=(
      const ScopedNeutralLuaExecutionTaint&) = delete;

 private:
  lua_State* lua_;
  LuaExecutionTaintState saved_;
};

class ScopedLuaExecutionTaintSource final {
 public:
  ScopedLuaExecutionTaintSource(lua_State* const L, const int source)
      : lua_(L), saved_(lua_get_execution_taint_state(L)) {
    auto installed = saved_;
    installed.source = source;
    if (installed.tracking_depth != ~std::uint32_t{0}) {
      ++installed.tracking_depth;
    }
    lua_set_execution_taint_state(lua_, installed);
  }
  ~ScopedLuaExecutionTaintSource() {
    lua_set_execution_taint_state(lua_, saved_);
  }
  ScopedLuaExecutionTaintSource(const ScopedLuaExecutionTaintSource&) = delete;
  ScopedLuaExecutionTaintSource& operator=(
      const ScopedLuaExecutionTaintSource&) = delete;

 private:
  lua_State* lua_;
  LuaExecutionTaintState saved_;
};

}
