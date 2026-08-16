/*
** $Id: lstate.c,v 2.36 2006/05/24 14:15:50 roberto Exp $
** Global State
** See Copyright Notice in lua.h
*/

#include <stddef.h>

#define lstate_c
#define LUA_CORE

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "llex.h"
#include "lmem.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"

#define state_size(x)	(sizeof(x) + LUAI_EXTRASPACE)
#define fromstate(l)	(cast(lu_byte *, (l)) - LUAI_EXTRASPACE)
#define tostate(l)   (cast(lua_State *, cast(lu_byte *, l) + LUAI_EXTRASPACE))

typedef struct LG {
  lua_State l;
  global_State g;
} LG;

static void stack_init (lua_State *L1, lua_State *L) {

  L1->base_ci = luaM_newvector(L, BASIC_CI_SIZE, CallInfo);
  L1->ci = L1->base_ci;
  L1->size_ci = BASIC_CI_SIZE;
  L1->end_ci = L1->base_ci + L1->size_ci - 1;

  L1->stack = luaM_newvector(L, BASIC_STACK_SIZE + EXTRA_STACK, TValue);
  L1->stacksize = BASIC_STACK_SIZE + EXTRA_STACK;
  L1->top = L1->stack;
  L1->stack_last = L1->stack+(L1->stacksize - EXTRA_STACK)-1;

  L1->ci->func = L1->top;
  L1->ci->previousowner = 0;
  setnilvalue(L1->top++);
  L1->base = L1->ci->base = L1->top;
  L1->ci->top = L1->top + LUA_MINSTACK;
  luaC_refreshobject(L, obj2gco(L1));
}

static void freestack (lua_State *L, lua_State *L1) {
  luaM_freearray(L, L1->base_ci, L1->size_ci, CallInfo);
  luaM_freearray(L, L1->stack, L1->stacksize, TValue);
}

static void f_luaopen (lua_State *L, void *ud) {
  global_State *g = G(L);
  UNUSED(ud);
  stack_init(L, L);
  sethvalue(L, gt(L), luaH_new(L, 0, 2));
  sethvalue(L, registry(L), luaH_new(L, 0, 2));

  hvalue(registry(L))->nativestate = LUAW_NATIVE_CONTAINER;
  luaS_resize(L, MINSTRTABSIZE);
  luaT_init(L);
  luaX_init(L);
  luaS_fix(luaS_newliteral(L, MEMERRMSG));
  g->GCthreshold = (g->totalbytes > MAX_LUMEM/4)
                     ? MAX_LUMEM : 4*g->totalbytes;
}

static void preinit_state (lua_State *L, global_State *g) {
  G(L) = g;
  L->stack = NULL;
  L->stacksize = 0;
  L->errorJmp = NULL;
  L->hook = NULL;
  L->hookmask = 0;
  L->basehookcount = 0;
  L->allowhook = 1;
  resethookcount(L);
  L->openupval = NULL;
  L->size_ci = 0;
  L->nCcalls = 0;
  L->status = 0;
  L->allocationowner = 0;
  L->base_ci = L->ci = NULL;
  L->savedpc = NULL;
  L->errfunc = 0;
  setnilvalue(gt(L));
}

static void close_state (lua_State *L) {
  global_State *g = G(L);
  if (g->stateclosehook != NULL) {
    lua_StateCloseHook hook = g->stateclosehook;
    void *ud = g->stateclosehookud;
    g->stateclosehook = NULL;
    g->stateclosehookud = NULL;
    lua_unlock(L);
    hook(L, ud);
    lua_lock(L);
  }
  luaF_close(L, L->stack);
  luaC_freeall(L);
  lua_assert(g->rootgc == obj2gco(L));
  lua_assert(g->strt.nuse == 0);
  luaM_freearray(L, G(L)->strt.hash, G(L)->strt.size, TString *);
  luaZ_freebuffer(L, &g->buff);
  freestack(L, L);
  lua_assert(g->totalbytes == sizeof(LG));
  (*g->frealloc)(g->ud, fromstate(L), state_size(LG), 0);
}

lua_State *luaE_newthread (lua_State *L) {
  lua_State *L1 = tostate(luaM_malloc(L, state_size(lua_State)));
  luaC_link(L, obj2gco(L1), LUA_TTHREAD);
  preinit_state(L1, G(L));
  L1->allocationowner = L->allocationowner;
  stack_init(L1, L);
  setobj2n(L, gt(L1), gt(L));
  L1->hookmask = L->hookmask;
  L1->basehookcount = L->basehookcount;
  L1->hook = L->hook;
  resethookcount(L1);
  lua_assert(iswhite(obj2gco(L1)));
  return L1;
}

void luaE_freethread (lua_State *L, lua_State *L1) {
  luaF_close(L1, L1->stack);
  lua_assert(L1->openupval == NULL);
  luai_userstatefree(L1);
  freestack(L, L1);
  luaM_freemem(L, fromstate(L1), state_size(lua_State));
}

LUA_API lua_State *lua_newstate (lua_Alloc f, void *ud) {
  int i;
  lua_State *L;
  global_State *g;
  void *l = (*f)(ud, NULL, 0, state_size(LG));
  if (l == NULL) return NULL;
  L = tostate(l);
  g = &((LG *)L)->g;
  L->next = NULL;
  L->owner = 0;
  L->accountedbytes = 0;
  L->tt = LUA_TTHREAD;
  g->currentwhite = bit2mask(WHITE0BIT, FIXEDBIT);
  L->marked = luaC_white(g);
  set2bits(L->marked, FIXEDBIT, SFIXEDBIT);
  preinit_state(L, g);
  g->frealloc = f;
  g->ud = ud;
  g->mainthread = L;
  g->uvhead.owner = 0;
  g->uvhead.accountedbytes = 0;
  g->uvhead.u.l.prev = &g->uvhead;
  g->uvhead.u.l.next = &g->uvhead;
  g->GCthreshold = 0;
  g->strt.size = 0;
  g->strt.nuse = 0;
  g->strt.hash = NULL;
  setnilvalue(registry(L));
  luaZ_initbuffer(L, &g->buff);
  g->panic = NULL;
  g->allocationowner = 0;
  g->sandboxclosureowner = 0;
  g->executiontaint = 0;
  g->tainttracking = 0;
  g->objectmemoryhook = NULL;
  g->objectmemoryhookud = NULL;
  g->stateclosehook = NULL;
  g->stateclosehookud = NULL;
  g->clientcpuprofiler = NULL;
  g->gcstate = GCSpause;
  g->rootgc = obj2gco(L);
  g->sweepstrgc = 0;
  g->sweepgc = &g->rootgc;
  g->gray = NULL;
  g->grayagain = NULL;
  g->weak = NULL;
  g->tmudata = NULL;
  g->totalbytes = sizeof(LG);
  g->gcpause = LUAI_GCPAUSE;
  g->gcstepmul = LUAI_GCMUL;
  g->gcdept = 0;
  g->gcstepcalls = 0;
  g->gcworkunits = 0;
  g->gccycles = 0;
  g->gcboundedyields = 0;
  for (i=0; i<NUM_TAGS; i++) g->mt[i] = NULL;
  if (luaD_rawrunprotected(L, f_luaopen, NULL) != 0) {

    close_state(L);
    L = NULL;
  }
  else
    luai_userstateopen(L);
  return L;
}

static void callallgcTM (lua_State *L, void *ud) {
  UNUSED(ud);
  luaC_callGCTM(L);
}

LUA_API void lua_close (lua_State *L) {
  L = G(L)->mainthread;
  lua_lock(L);
  luaF_close(L, L->stack);
  luaC_separateudata(L, 1);
  L->errfunc = 0;
  do {
    L->ci = L->base_ci;
    L->base = L->top = L->ci->base;
    L->nCcalls = 0;
  } while (luaD_rawrunprotected(L, callallgcTM, NULL) != 0);
  lua_assert(G(L)->tmudata == NULL);
  luai_userstateclose(L);
  close_state(L);
}
