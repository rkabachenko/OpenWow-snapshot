/*
** $Id: lapi.h,v 2.2 2005/04/25 19:24:10 roberto Exp $
** Auxiliary functions from Lua API
** See Copyright Notice in lua.h
*/

#ifndef lapi_h
#define lapi_h

#include "lobject.h"

LUAI_FUNC void luaA_pushobject (lua_State *L, const TValue *o);
LUAI_FUNC int luaW_currenttaint (lua_State *L);
LUAI_FUNC void luaW_setcurrenttaint (lua_State *L, TValue *value);
LUAI_FUNC void luaW_copytaint (lua_State *L, TValue *destination,
                               const TValue *source);
LUAI_FUNC void luaW_applywritetaint (lua_State *L, TValue *o);

#endif
