#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/framescript/core/click_frame_lookup.h"
#include "openwow/ui/lua_taint_api.h"

#include "openwow/foundation/text/ascii.h"

#include <string>
#include <string_view>

namespace openwow::ui::game::detail {
namespace {

constexpr const char* kClickFrameLookupCacheRegistryKey =
    "openwow.click_frame_lookup_cache";

std::string MakeClickFrameLookupCacheKey(const std::string_view frame_name) {

  return openwow::text::ToLowerAscii(std::string(frame_name));
}

int EnsureClickFrameLookupCacheTable(lua_State* L) {
  const openwow::ui::ScopedNeutralLuaExecutionTaint neutral_taint(L);
  lua_getfield(L, LUA_REGISTRYINDEX, kClickFrameLookupCacheRegistryKey);
  if (lua_istable(L, -1) != 0) {
    return lua_absindex(L, -1);
  }

  lua_pop(L, 1);
  lua_newtable(L);
  lua_pushvalue(L, -1);
  lua_setfield(L, LUA_REGISTRYINDEX, kClickFrameLookupCacheRegistryKey);
  return lua_absindex(L, -1);
}

void RemoveCachedClickFrameLookupEntry(lua_State* L,
                                       const int cache_index,
                                       const std::string_view frame_name) {
  const std::string key = MakeClickFrameLookupCacheKey(frame_name);

  lua_getfield(L, cache_index, key.c_str());
  if (lua_isnumber(L, -1) != 0) {
    luaL_unref(L, LUA_REGISTRYINDEX, static_cast<int>(lua_tointeger(L, -1)));
  }
  lua_pop(L, 1);

  lua_pushnil(L);
  lua_setfield(L, cache_index, key.c_str());
}

bool PushCachedClickFrameLookupResult(lua_State* L,
                                      const std::string_view frame_name) {
  const int cache_index = EnsureClickFrameLookupCacheTable(L);
  const std::string cache_key = MakeClickFrameLookupCacheKey(frame_name);

  lua_getfield(L, cache_index, cache_key.c_str());
  if (lua_isnumber(L, -1) == 0) {
    lua_pop(L, 2);
    return false;
  }

  const int cache_ref = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);

  lua_rawgeti(L, LUA_REGISTRYINDEX, cache_ref);
  if (lua_istable(L, -1) == 0) {
    lua_pop(L, 1);
    RemoveCachedClickFrameLookupEntry(L, cache_index, frame_name);
    lua_pop(L, 1);
    return false;
  }

  lua_remove(L, cache_index);
  return true;
}

bool PushValidatedNamedFrameLikeObject(lua_State* L,
                                       const std::string_view frame_name) {
  const std::string global_name(frame_name);
  lua_getglobal(L, global_name.c_str());
  if (lua_istable(L, -1) == 0) {
    lua_pop(L, 1);
    return false;
  }

  const int global_index = lua_absindex(L, -1);
  lua_getfield(L, global_index, "__ow_name");
  const char* resolved_name = lua_tostring(L, -1);
  const bool name_matches =
      resolved_name != nullptr &&
      openwow::text::EqualsIgnoreCaseAscii(resolved_name, frame_name);
  lua_pop(L, 1);
  if (!name_matches) {
    lua_pop(L, 1);
    return false;
  }

  if (!IsFrameLikeLookupObjectType(GetLuaFrameLookupObjectType(L, global_index))) {
    lua_pop(L, 1);
    return false;
  }

  lua_getfield(L, global_index, "__ow_ref");
  if (lua_isnumber(L, -1) == 0) {
    lua_pop(L, 2);
    return false;
  }
  const int registry_ref = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 1);

  lua_rawgeti(L, LUA_REGISTRYINDEX, registry_ref);
  if (lua_istable(L, -1) == 0 || lua_rawequal(L, global_index, -1) == 0) {
    lua_pop(L, 2);
    return false;
  }

  lua_pop(L, 1);
  return true;
}

void CacheClickFrameLookupResult(lua_State* L,
                                 const std::string_view frame_name,
                                 const int table_index) {
  const int absolute_table_index = lua_absindex(L, table_index);
  const int cache_index = EnsureClickFrameLookupCacheTable(L);
  const std::string cache_key = MakeClickFrameLookupCacheKey(frame_name);

  RemoveCachedClickFrameLookupEntry(L, cache_index, frame_name);

  lua_pushvalue(L, absolute_table_index);
  const int cache_ref = luaL_ref(L, LUA_REGISTRYINDEX);
  lua_pushinteger(L, cache_ref);
  lua_setfield(L, cache_index, cache_key.c_str());
  lua_pop(L, 1);
}

}

bool PushNamedFrameLikeObject(lua_State* L, const std::string_view frame_name) {
  if (L == nullptr) {
    return false;
  }

  if (PushCachedClickFrameLookupResult(L, frame_name)) {
    return true;
  }

  if (!PushValidatedNamedFrameLikeObject(L, frame_name)) {
    return false;
  }

  CacheClickFrameLookupResult(L, frame_name, -1);
  return true;
}

void ClearClickFrameLookupCache(lua_State* L) {
  if (L == nullptr) {
    return;
  }

  lua_getfield(L, LUA_REGISTRYINDEX, kClickFrameLookupCacheRegistryKey);
  if (lua_istable(L, -1) == 0) {
    lua_pop(L, 1);
    return;
  }

  const int cache_index = lua_absindex(L, -1);
  lua_pushnil(L);
  while (lua_next(L, cache_index) != 0) {
    if (lua_isnumber(L, -1) != 0) {
      luaL_unref(L, LUA_REGISTRYINDEX, static_cast<int>(lua_tointeger(L, -1)));
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);

  lua_pushnil(L);
  lua_setfield(L, LUA_REGISTRYINDEX, kClickFrameLookupCacheRegistryKey);
}

}
