/*
** $Id: lobject.h,v 2.20 2006/01/18 11:37:34 roberto Exp $
** Type definitions for Lua objects
** See Copyright Notice in lua.h
*/

#ifndef lobject_h
#define lobject_h

#include <stdarg.h>

#include "llimits.h"
#include "lua.h"

#define LAST_TAG	LUA_TTHREAD

#define NUM_TAGS	(LAST_TAG+1)

#define LUA_TPROTO	(LAST_TAG+1)
#define LUA_TUPVAL	(LAST_TAG+2)
#define LUA_TDEADKEY	(LAST_TAG+3)

typedef union GCObject GCObject;

#define CommonHeader	GCObject *next; unsigned int owner; lu_byte tt; \
	lu_byte marked; size_t accountedbytes

typedef struct GCheader {
  CommonHeader;
} GCheader;

typedef union {
  GCObject *gc;
  void *p;
  lua_Number n;
  int b;
} Value;

#define TValuefields	Value value; int tt; int taint

typedef struct lua_TValue {
  TValuefields;
} TValue;

LUAI_FUNC void luaW_copytaint (lua_State *L, TValue *destination,
                               const TValue *source);

#define ttisnil(o)	(ttype(o) == LUA_TNIL)
#define ttisnumber(o)	(ttype(o) == LUA_TNUMBER)
#define ttisstring(o)	(ttype(o) == LUA_TSTRING)
#define ttistable(o)	(ttype(o) == LUA_TTABLE)
#define ttisfunction(o)	(ttype(o) == LUA_TFUNCTION)
#define ttisboolean(o)	(ttype(o) == LUA_TBOOLEAN)
#define ttisuserdata(o)	(ttype(o) == LUA_TUSERDATA)
#define ttisthread(o)	(ttype(o) == LUA_TTHREAD)
#define ttislightuserdata(o)	(ttype(o) == LUA_TLIGHTUSERDATA)

#define ttype(o)	((o)->tt)
#define gcvalue(o)	check_exp(iscollectable(o), (o)->value.gc)
#define pvalue(o)	check_exp(ttislightuserdata(o), (o)->value.p)
#define nvalue(o)	check_exp(ttisnumber(o), (o)->value.n)
#define rawtsvalue(o)	check_exp(ttisstring(o), &(o)->value.gc->ts)
#define tsvalue(o)	(&rawtsvalue(o)->tsv)
#define rawuvalue(o)	check_exp(ttisuserdata(o), &(o)->value.gc->u)
#define uvalue(o)	(&rawuvalue(o)->uv)
#define clvalue(o)	check_exp(ttisfunction(o), &(o)->value.gc->cl)
#define hvalue(o)	check_exp(ttistable(o), &(o)->value.gc->h)
#define bvalue(o)	check_exp(ttisboolean(o), (o)->value.b)
#define thvalue(o)	check_exp(ttisthread(o), &(o)->value.gc->th)

#define l_isfalse(o)	(ttisnil(o) || (ttisboolean(o) && bvalue(o) == 0))

#define checkconsistency(obj) \
  lua_assert(!iscollectable(obj) || (ttype(obj) == (obj)->value.gc->gch.tt))

#define checkliveness(g,obj) \
  lua_assert(!iscollectable(obj) || \
  ((ttype(obj) == (obj)->value.gc->gch.tt) && !isdead(g, (obj)->value.gc)))

#define setnilvalue(obj) \
  do { TValue *i_o=(obj); i_o->value.gc=NULL; i_o->tt=LUA_TNIL; i_o->taint=0; } while (0)

#define setnvalue(obj,x) \
  do { TValue *i_o=(obj); i_o->value.n=(x); i_o->tt=LUA_TNUMBER; i_o->taint=0; } while (0)

#define setpvalue(obj,x) \
  do { TValue *i_o=(obj); i_o->value.p=(x); i_o->tt=LUA_TLIGHTUSERDATA; i_o->taint=0; } while (0)

#define setbvalue(obj,x) \
  do { TValue *i_o=(obj); i_o->value.b=(x); i_o->tt=LUA_TBOOLEAN; i_o->taint=0; } while (0)

#define setsvalue(L,obj,x) \
  do { TValue *i_o=(obj); \
    i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TSTRING; i_o->taint=0; \
    checkliveness(G(L),i_o); } while (0)

#define setuvalue(L,obj,x) \
  do { TValue *i_o=(obj); \
    i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TUSERDATA; i_o->taint=0; \
    checkliveness(G(L),i_o); } while (0)

#define setthvalue(L,obj,x) \
  do { TValue *i_o=(obj); \
    i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TTHREAD; i_o->taint=0; \
    checkliveness(G(L),i_o); } while (0)

#define setclvalue(L,obj,x) \
  do { TValue *i_o=(obj); \
    i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TFUNCTION; i_o->taint=0; \
    checkliveness(G(L),i_o); } while (0)

#define sethvalue(L,obj,x) \
  do { TValue *i_o=(obj); \
    i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TTABLE; i_o->taint=0; \
    checkliveness(G(L),i_o); } while (0)

#define setptvalue(L,obj,x) \
  do { TValue *i_o=(obj); \
    i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TPROTO; i_o->taint=0; \
    checkliveness(G(L),i_o); } while (0)

#define setobj(L,obj1,obj2) \
  do { const TValue *o2=(obj2); TValue *o1=(obj1); \
    o1->value = o2->value; o1->tt=o2->tt; luaW_copytaint((L),o1,o2); \
    checkliveness(G(L),o1); } while (0)

#define setobjraw(L,obj1,obj2) \
  do { const TValue *o2=(obj2); TValue *o1=(obj1); \
    o1->value=o2->value; o1->tt=o2->tt; o1->taint=o2->taint; \
    checkliveness(G(L),o1); } while (0)

#define setobjs2s	setobj

#define setobj2s	setobj
#define setsvalue2s	setsvalue
#define sethvalue2s	sethvalue
#define setptvalue2s	setptvalue

#define setobjt2t	setobj

#define setobj2t	setobj

#define setobj2n	setobj
#define setsvalue2n	setsvalue

#define setttype(obj, tt) (ttype(obj) = (tt))

#define iscollectable(o)	(ttype(o) >= LUA_TSTRING)

typedef TValue *StkId;

typedef union TString {
  L_Umaxalign dummy;
  struct {
    CommonHeader;
    lu_byte reserved;

    lu_byte nativefield;
    unsigned int hash;
    size_t len;
  } tsv;
} TString;

#define LUAW_NATIVE_STATE	1
#define LUAW_NATIVE_CONTAINER	2

#define LUAW_NATIVE_FIELD		1
#define LUAW_NATIVE_CONTAINER_FIELD	2

#define luaW_isnativefieldkey(o) \
	(ttisstring(o) && rawtsvalue(o)->tsv.nativefield != 0)

#define luaW_isnativecontainerkey(o) \
	(ttisstring(o) && \
	 rawtsvalue(o)->tsv.nativefield == LUAW_NATIVE_CONTAINER_FIELD)

#define luaW_marknativecontainer(key,v) \
	do { if (luaW_isnativecontainerkey(key)) { \
	       const TValue *i_v=(v); \
	       if (ttistable(i_v)) hvalue(i_v)->nativestate = LUAW_NATIVE_STATE; \
	     } } while (0)
#define luaW_isnativetable(t)	((t)->nativestate != 0)

#define getstr(ts)	cast(const char *, (ts) + 1)
#define svalue(o)       getstr(tsvalue(o))

typedef union Udata {
  L_Umaxalign dummy;
  struct {
    CommonHeader;
    struct Table *metatable;
    struct Table *env;
    size_t len;
  } uv;
} Udata;

typedef struct Proto {
  CommonHeader;
  TValue *k;
  Instruction *code;
  struct Proto **p;
  int *lineinfo;
  struct LocVar *locvars;
  TString **upvalues;
  TString  *source;
  int sizeupvalues;
  int sizek;
  int sizecode;
  int sizelineinfo;
  int sizep;
  int sizelocvars;
  int linedefined;
  int lastlinedefined;
  GCObject *gclist;
  lu_byte nups;
  lu_byte numparams;
  lu_byte is_vararg;
  lu_byte maxstacksize;
} Proto;

#define VARARG_HASARG		1
#define VARARG_ISVARARG		2
#define VARARG_NEEDSARG		4

typedef struct LocVar {
  TString *varname;
  int startpc;
  int endpc;
} LocVar;

typedef struct UpVal {
  CommonHeader;
  TValue *v;
  union {
    TValue value;
    struct {
      struct UpVal *prev;
      struct UpVal *next;
    } l;
  } u;
} UpVal;

#define ClosureHeader \
	CommonHeader; lu_byte isC; lu_byte nupvalues; GCObject *gclist; \
	struct Table *env

typedef struct CClosure {
  ClosureHeader;
  lua_CFunction f;
  TValue upvalue[1];
} CClosure;

typedef struct LClosure {
  ClosureHeader;
  struct Proto *p;
  UpVal *upvals[1];
} LClosure;

typedef union Closure {
  CClosure c;
  LClosure l;
} Closure;

#define iscfunction(o)	(ttype(o) == LUA_TFUNCTION && clvalue(o)->c.isC)
#define isLfunction(o)	(ttype(o) == LUA_TFUNCTION && !clvalue(o)->c.isC)

typedef union TKey {
  struct {
    TValuefields;
    struct Node *next;
  } nk;
  TValue tvk;
} TKey;

typedef struct Node {
  TValue i_val;
  TKey i_key;
} Node;

typedef struct Table {
  CommonHeader;
  lu_byte flags;
  lu_byte lsizenode;

  lu_byte nativestate;
  struct Table *metatable;
  TValue *array;
  Node *node;
  Node *lastfree;
  GCObject *gclist;
  int sizearray;
} Table;

#define lmod(s,size) \
	(check_exp((size&(size-1))==0, (cast(int, (s) & ((size)-1)))))

#define twoto(x)	(1<<(x))
#define sizenode(t)	(twoto((t)->lsizenode))

#define luaO_nilobject		(&luaO_nilobject_)

LUAI_DATA const TValue luaO_nilobject_;

#define ceillog2(x)	(luaO_log2((x)-1) + 1)

LUAI_FUNC int luaO_log2 (unsigned int x);
LUAI_FUNC int luaO_int2fb (unsigned int x);
LUAI_FUNC int luaO_fb2int (int x);
LUAI_FUNC int luaO_rawequalObj (const TValue *t1, const TValue *t2);
LUAI_FUNC lu_int32 luaO_str2uint32 (const char *s, char **endptr, int base);
LUAI_FUNC int luaO_str2d (const char *s, lua_Number *result);
LUAI_FUNC const char *luaO_pushvfstring (lua_State *L, const char *fmt,
                                                       va_list argp);
LUAI_FUNC const char *luaO_pushfstring (lua_State *L, const char *fmt, ...);
LUAI_FUNC void luaO_chunkid (char *out, const char *source, size_t len);

#endif
