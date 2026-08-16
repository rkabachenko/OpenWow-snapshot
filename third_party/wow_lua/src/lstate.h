/*
** $Id: lstate.h,v 2.24 2006/02/06 18:27:59 roberto Exp $
** Global State
** See Copyright Notice in lua.h
*/

#ifndef lstate_h
#define lstate_h

#include "lua.h"

#include "lobject.h"
#include "ltm.h"
#include "lzio.h"

struct lua_longjmp;

#define gt(L)	(&L->l_gt)

#define registry(L)	(&G(L)->l_registry)

#define EXTRA_STACK   5

#define BASIC_CI_SIZE           8

#define BASIC_STACK_SIZE        (2*LUA_MINSTACK)

typedef struct stringtable {
  GCObject **hash;
  lu_int32 nuse;
  int size;
} stringtable;

typedef struct CallInfo {
  StkId base;
  StkId func;
  StkId	top;
  const Instruction *savedpc;
  int nresults;
  int tailcalls;
  unsigned int previousowner;
} CallInfo;

#define curr_func(L)	(clvalue(L->ci->func))
#define ci_func(ci)	(clvalue((ci)->func))
#define f_isLua(ci)	(!ci_func(ci)->c.isC)
#define isLua(ci)	(ttisfunction((ci)->func) && f_isLua(ci))

typedef struct global_State {
  stringtable strt;
  lua_Alloc frealloc;
  void *ud;
  lu_byte currentwhite;
  lu_byte gcstate;
  int sweepstrgc;
  GCObject *rootgc;
  GCObject **sweepgc;
  GCObject *gray;
  GCObject *grayagain;
  GCObject *weak;
  GCObject *tmudata;
  Mbuffer buff;
  lu_mem GCthreshold;
  lu_mem totalbytes;
  lu_mem estimate;
  lu_mem gcdept;
  int gcpause;
  int gcstepmul;
  uint64_t gcstepcalls;
  uint64_t gcworkunits;
  uint64_t gccycles;
  uint64_t gcboundedyields;
  lua_CFunction panic;
  TValue l_registry;
  struct lua_State *mainthread;
  UpVal uvhead;
  struct Table *mt[NUM_TAGS];
  TString *tmname[TM_N];
  unsigned int allocationowner;
  unsigned int sandboxclosureowner;
  int executiontaint;
  unsigned int tainttracking;
  lua_ObjectMemoryHook objectmemoryhook;
  void *objectmemoryhookud;
  lua_StateCloseHook stateclosehook;
  void *stateclosehookud;
  void *clientcpuprofiler;
} global_State;

struct lua_State {
  CommonHeader;
  lu_byte status;
  StkId top;
  StkId base;
  global_State *l_G;
  CallInfo *ci;
  const Instruction *savedpc;
  StkId stack_last;
  StkId stack;
  CallInfo *end_ci;
  CallInfo *base_ci;
  int stacksize;
  int size_ci;
  unsigned short nCcalls;
  lu_byte hookmask;
  lu_byte allowhook;
  int basehookcount;
  int hookcount;
  lua_Hook hook;
  TValue l_gt;
  TValue env;
  GCObject *openupval;
  GCObject *gclist;
  struct lua_longjmp *errorJmp;
  ptrdiff_t errfunc;
  unsigned int allocationowner;
};

#define G(L)	(L->l_G)

union GCObject {
  GCheader gch;
  union TString ts;
  union Udata u;
  union Closure cl;
  struct Table h;
  struct Proto p;
  struct UpVal uv;
  struct lua_State th;
};

#define rawgco2ts(o)	check_exp((o)->gch.tt == LUA_TSTRING, &((o)->ts))
#define gco2ts(o)	(&rawgco2ts(o)->tsv)
#define rawgco2u(o)	check_exp((o)->gch.tt == LUA_TUSERDATA, &((o)->u))
#define gco2u(o)	(&rawgco2u(o)->uv)
#define gco2cl(o)	check_exp((o)->gch.tt == LUA_TFUNCTION, &((o)->cl))
#define gco2h(o)	check_exp((o)->gch.tt == LUA_TTABLE, &((o)->h))
#define gco2p(o)	check_exp((o)->gch.tt == LUA_TPROTO, &((o)->p))
#define gco2uv(o)	check_exp((o)->gch.tt == LUA_TUPVAL, &((o)->uv))
#define ngcotouv(o) \
	check_exp((o) == NULL || (o)->gch.tt == LUA_TUPVAL, &((o)->uv))
#define gco2th(o)	check_exp((o)->gch.tt == LUA_TTHREAD, &((o)->th))

#define obj2gco(v)	(cast(GCObject *, (v)))

LUAI_FUNC lua_State *luaE_newthread (lua_State *L);
LUAI_FUNC void luaE_freethread (lua_State *L, lua_State *L1);

#endif
