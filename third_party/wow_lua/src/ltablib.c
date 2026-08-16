/*
** $Id: ltablib.c,v 1.38 2005/10/23 17:38:15 roberto Exp $
** Library for Table Manipulation
** See Copyright Notice in lua.h
*/

#include <stddef.h>

#define ltablib_c
#define LUA_LIB

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"

#define aux_getn(L,n)	(luaL_checktype(L, n, LUA_TTABLE), luaL_getn(L, n))

static int foreachi (lua_State *L) {
  int i;
  int n = aux_getn(L, 1);
  luaL_checktype(L, 2, LUA_TFUNCTION);
  for (i=1; i <= n; i++) {
    lua_pushvalue(L, 2);
    lua_pushinteger(L, i);
    lua_rawgeti(L, 1, i);
    lua_call(L, 2, 1);
    if (!lua_isnil(L, -1))
      return 1;
    lua_pop(L, 1);
  }
  return 0;
}

static int foreach (lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  luaL_checktype(L, 2, LUA_TFUNCTION);
  lua_pushnil(L);
  while (lua_next(L, 1)) {
    lua_pushvalue(L, 2);
    lua_pushvalue(L, -3);
    lua_pushvalue(L, -3);
    lua_call(L, 2, 1);
    if (!lua_isnil(L, -1))
      return 1;
    lua_pop(L, 2);
  }
  return 0;
}

static int maxn (lua_State *L) {
  lua_Number max = 0;
  luaL_checktype(L, 1, LUA_TTABLE);
  lua_pushnil(L);
  while (lua_next(L, 1)) {
    lua_pop(L, 1);
    if (lua_type(L, -1) == LUA_TNUMBER) {
      lua_Number v = lua_tonumber(L, -1);
      if (v > max) max = v;
    }
  }
  lua_pushnumber(L, max);
  return 1;
}

static int getn (lua_State *L) {
  lua_pushinteger(L, aux_getn(L, 1));
  return 1;
}

static int setn (lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
#ifndef luaL_setn
  luaL_setn(L, 1, luaL_checkint(L, 2));
#else
  luaL_error(L, LUA_QL("setn") " is obsolete");
#endif
  lua_pushvalue(L, 1);
  return 1;
}

static int tinsert (lua_State *L) {
  int e = aux_getn(L, 1) + 1;
  int pos;
  switch (lua_gettop(L)) {
    case 2: {
      pos = e;
      break;
    }
    case 3: {
      int i;
      pos = luaL_checkint(L, 2);
      if (pos > e) e = pos;
      for (i = e; i > pos; i--) {
        lua_rawgeti(L, 1, i-1);
        lua_rawseti(L, 1, i);
      }
      break;
    }
    default: {
      return luaL_error(L, "wrong number of arguments to " LUA_QL("insert"));
    }
  }
  luaL_setn(L, 1, e);
  lua_rawseti(L, 1, pos);
  return 0;
}

static int tremove (lua_State *L) {
  int e = aux_getn(L, 1);
  int pos = luaL_optint(L, 2, e);
  if (e == 0) return 0;
  luaL_setn(L, 1, e - 1);
  lua_rawgeti(L, 1, pos);
  for ( ;pos<e; pos++) {
    lua_rawgeti(L, 1, pos+1);
    lua_rawseti(L, 1, pos);
  }
  lua_pushnil(L);
  lua_rawseti(L, 1, e);
  return 1;
}

static int tconcat (lua_State *L) {
  luaL_Buffer b;
  size_t lsep;
  int i, last;
  const char *sep = luaL_optlstring(L, 2, "", &lsep);
  luaL_checktype(L, 1, LUA_TTABLE);
  i = luaL_optint(L, 3, 1);
  last = luaL_opt(L, luaL_checkint, 4, luaL_getn(L, 1));
  luaL_buffinit(L, &b);
  for (; i <= last; i++) {
    lua_rawgeti(L, 1, i);
    luaL_argcheck(L, lua_isstring(L, -1), 1, "table contains non-strings");
    luaL_addvalue(&b);
    if (i != last)
      luaL_addlstring(&b, sep, lsep);
  }
  luaL_pushresult(&b);
  return 1;
}

static void set2 (lua_State *L, int i, int j) {
  lua_rawseti(L, 1, i);
  lua_rawseti(L, 1, j);
}

static int sort_comp (lua_State *L, int a, int b) {
  if (!lua_isnil(L, 2)) {
    int res;
    lua_pushvalue(L, 2);
    lua_pushvalue(L, a-1);
    lua_pushvalue(L, b-2);
    lua_call(L, 2, 1);
    res = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return res;
  }
  else
    return lua_lessthan(L, a, b);
}

static void auxsort (lua_State *L, int l, int u) {
  while (l < u) {
    int i, j;

    lua_rawgeti(L, 1, l);
    lua_rawgeti(L, 1, u);
    if (sort_comp(L, -1, -2))
      set2(L, l, u);
    else
      lua_pop(L, 2);
    if (u-l == 1) break;
    i = (l+u)/2;
    lua_rawgeti(L, 1, i);
    lua_rawgeti(L, 1, l);
    if (sort_comp(L, -2, -1))
      set2(L, i, l);
    else {
      lua_pop(L, 1);
      lua_rawgeti(L, 1, u);
      if (sort_comp(L, -1, -2))
        set2(L, i, u);
      else
        lua_pop(L, 2);
    }
    if (u-l == 2) break;
    lua_rawgeti(L, 1, i);
    lua_pushvalue(L, -1);
    lua_rawgeti(L, 1, u-1);
    set2(L, i, u-1);

    i = l; j = u-1;
    for (;;) {

      while (lua_rawgeti(L, 1, ++i), sort_comp(L, -1, -2)) {
        if (i>u) luaL_error(L, "invalid order function for sorting");
        lua_pop(L, 1);
      }

      while (lua_rawgeti(L, 1, --j), sort_comp(L, -3, -1)) {
        if (j<l) luaL_error(L, "invalid order function for sorting");
        lua_pop(L, 1);
      }
      if (j<i) {
        lua_pop(L, 3);
        break;
      }
      set2(L, i, j);
    }
    lua_rawgeti(L, 1, u-1);
    lua_rawgeti(L, 1, i);
    set2(L, u-1, i);

    if (i-l < u-i) {
      j=l; i=i-1; l=i+2;
    }
    else {
      j=i+1; i=u; u=j-2;
    }
    auxsort(L, j, i);
  }
}

static int sort (lua_State *L) {
  int n = aux_getn(L, 1);
  luaL_checkstack(L, 40, "");
  if (!lua_isnoneornil(L, 2))
    luaL_checktype(L, 2, LUA_TFUNCTION);
  lua_settop(L, 2);
  auxsort(L, 1, n);
  return 0;
}

static const luaL_Reg tab_funcs[] = {
  {"concat", tconcat},
  {"foreach", foreach},
  {"foreachi", foreachi},
  {"getn", getn},
  {"maxn", maxn},
  {"insert", tinsert},
  {"remove", tremove},
  {"setn", setn},
  {"sort", sort},
  {NULL, NULL}
};

LUALIB_API int luaopen_table (lua_State *L) {
  luaL_register(L, LUA_TABLIBNAME, tab_funcs);
  return 1;
}
