/*
** $Id: lparser.h,v 1.57 2006/03/09 18:14:31 roberto Exp $
** Lua Parser
** See Copyright Notice in lua.h
*/

#ifndef lparser_h
#define lparser_h

#include "llimits.h"
#include "lobject.h"
#include "lzio.h"

typedef enum {
  VVOID,
  VNIL,
  VTRUE,
  VFALSE,
  VK,
  VKNUM,
  VLOCAL,
  VUPVAL,
  VGLOBAL,
  VINDEXED,
  VJMP,
  VRELOCABLE,
  VNONRELOC,
  VCALL,
  VVARARG
} expkind;

typedef struct expdesc {
  expkind k;
  union {
    struct { int info, aux; } s;
    lua_Number nval;
  } u;
  int t;
  int f;
} expdesc;

typedef struct upvaldesc {
  lu_byte k;
  lu_byte info;
} upvaldesc;

struct BlockCnt;

typedef struct FuncState {
  Proto *f;
  Table *h;
  struct FuncState *prev;
  struct LexState *ls;
  struct lua_State *L;
  struct BlockCnt *bl;
  int pc;
  int lasttarget;
  int jpc;
  int freereg;
  int nk;
  int np;
  short nlocvars;
  lu_byte nactvar;
  upvaldesc upvalues[LUAI_MAXUPVALUES];
  unsigned short actvar[LUAI_MAXVARS];
} FuncState;

LUAI_FUNC Proto *luaY_parser (lua_State *L, ZIO *z, Mbuffer *buff,
                                            const char *name);

#endif
