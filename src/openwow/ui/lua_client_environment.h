#pragma once

#include "openwow/net/client_services.h"

extern "C" {
#include <lua.h>
}

namespace openwow::ui {

enum class LuaClientPlatform {
  kWindows,
  kMac,
  kLinux,
  kOther,
};

inline constexpr LuaClientPlatform kRetailLuaClientPlatform =
    LuaClientPlatform::kMac;

[[nodiscard]] inline constexpr LuaClientPlatform GetHostLuaClientPlatform() noexcept {
#if defined(_WIN32)
  return LuaClientPlatform::kWindows;
#elif defined(__APPLE__)
  return LuaClientPlatform::kMac;
#elif defined(__linux__)
  return LuaClientPlatform::kLinux;
#else
  return LuaClientPlatform::kOther;
#endif
}

inline int PushRetailLuaClientPlatformQuery(
    lua_State *state, const LuaClientPlatform queried_platform) {
  if (kRetailLuaClientPlatform == queried_platform) {
    lua_pushnumber(state, 1.0);
  } else {
    lua_pushnil(state);
  }
  return 1;
}

inline int PushLuaLiveExpansionLevel(lua_State *state) {
  lua_pushnumber(
      state,
      static_cast<lua_Number>(openwow::net::ClientServices::Instance().GetExpansionLevel()));
  return 1;
}

}
