#include "openwow/ui/lua_taint_api.h"

extern "C" {
#include <lua.hpp>
#include "lobject.h"
#include "lstate.h"
}

namespace openwow::ui {
namespace {

TValue* LuaIndexToValue(lua_State* L, int idx) {
  if (idx > 0) {
    TValue* value = L->base + (idx - 1);
    return value < L->top ? value : nullptr;
  }

  if (idx > LUA_REGISTRYINDEX) {
    return (idx != 0 && -idx <= L->top - L->base) ? L->top + idx : nullptr;
  }

  switch (idx) {
    case LUA_REGISTRYINDEX:
      return registry(L);
    case LUA_ENVIRONINDEX: {
      Closure* func = curr_func(L);
      sethvalue(L, &L->env, func->c.env);
      return &L->env;
    }
    case LUA_GLOBALSINDEX:
      return gt(L);
    default: {
      Closure* func = curr_func(L);
      idx = LUA_GLOBALSINDEX - idx;
      return idx <= func->c.nupvalues ? &func->c.upvalue[idx - 1] : nullptr;
    }
  }
}

}

void lua_set_taint(lua_State* L, const int idx, const int taint) {
  if (L == nullptr) {
    return;
  }

  if (TValue* value = LuaIndexToValue(L, idx); value != nullptr) {
    value->taint = taint;
  }
}

int lua_get_taint(lua_State* L, const int idx) {
  if (L == nullptr) {
    return 0;
  }

  const TValue* value = LuaIndexToValue(L, idx);
  return value != nullptr ? value->taint : 0;
}

LuaExecutionTaintState lua_get_execution_taint_state(lua_State* L) {
  if (L == nullptr) {
    return {};
  }
  return {
      .source = lua_getexecutiontaint(L),
      .tracking_depth = static_cast<std::uint32_t>(lua_gettainttracking(L)),
  };
}

void lua_set_execution_taint_state(lua_State* L,
                                   const LuaExecutionTaintState state) {
  if (L == nullptr) {
    return;
  }
  (void)lua_setexecutiontaint(L, state.source);
  (void)lua_settainttracking(L, state.tracking_depth);
}

}
