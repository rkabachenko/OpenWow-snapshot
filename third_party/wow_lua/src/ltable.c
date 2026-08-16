/*
** $Id: ltable.c,v 2.32 2006/01/18 11:49:02 roberto Exp $
** Lua tables (hash)
** See Copyright Notice in lua.h
*/

#include <math.h>
#include <string.h>

#define ltable_c
#define LUA_CORE

#include "lua.h"

#include "lapi.h"
#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "ltable.h"

#if LUAI_BITSINT > 26
#define MAXBITS		26
#else
#define MAXBITS		(LUAI_BITSINT-2)
#endif

#define MAXASIZE	(1 << MAXBITS)

#define hashpow2(t,n)      (gnode(t, lmod((n), sizenode(t))))

#define hashstr(t,str)  hashpow2(t, (str)->tsv.hash)
#define hashboolean(t,p)        hashpow2(t, p)

#define hashmod(t,n)	(gnode(t, ((n) % ((sizenode(t)-1)|1))))

#define hashpointer(t,p)	hashmod(t, IntPoint(p))

#define numints		cast_int(sizeof(lua_Number)/sizeof(int))

#define dummynode		(&dummynode_)

static const Node dummynode_ = {
  {{NULL}, LUA_TNIL, 0},
  {{{NULL}, LUA_TNIL, 0, NULL}}
};

static Node *hashnum (const Table *t, lua_Number n) {
  unsigned int a[numints];
  int i;
  n += 1;
  lua_assert(sizeof(a) <= sizeof(n));
  memcpy(a, &n, sizeof(a));
  for (i = 1; i < numints; i++) a[0] += a[i];
  return hashmod(t, a[0]);
}

static Node *mainposition (const Table *t, const TValue *key) {
  switch (ttype(key)) {
    case LUA_TNUMBER:
      return hashnum(t, nvalue(key));
    case LUA_TSTRING:
      return hashstr(t, rawtsvalue(key));
    case LUA_TBOOLEAN:
      return hashboolean(t, bvalue(key));
    case LUA_TLIGHTUSERDATA:
      return hashpointer(t, pvalue(key));
    default:
      return hashpointer(t, gcvalue(key));
  }
}

static int arrayindex (const TValue *key) {
  if (ttisnumber(key)) {
    lua_Number n = nvalue(key);
    int k;
    lua_number2int(k, n);
    if (luai_numeq(cast_num(k), n))
      return k;
  }
  return -1;
}

static int findindex (lua_State *L, Table *t, StkId key) {
  int i;
  if (ttisnil(key)) return -1;
  i = arrayindex(key);
  if (0 < i && i <= t->sizearray)
    return i-1;
  else {
    Node *n = mainposition(t, key);
    do {

      if (luaO_rawequalObj(key2tval(n), key) ||
            (ttype(gkey(n)) == LUA_TDEADKEY && iscollectable(key) &&
             gcvalue(gkey(n)) == gcvalue(key))) {
        i = cast_int(n - gnode(t, 0));

        return i + t->sizearray;
      }
      else n = gnext(n);
    } while (n);
    luaG_runerror(L, "invalid key to " LUA_QL("next"));
    return 0;
  }
}

int luaH_next (lua_State *L, Table *t, StkId key) {

  const int native = luaW_isnativetable(t);
  int i = findindex(L, t, key);
  for (i++; i < t->sizearray; i++) {
    if (!ttisnil(&t->array[i])) {
      setnvalue(key, cast_num(i+1));
      if (native) {
        setobjraw(L, key+1, &t->array[i]);
      } else {
        luaW_setcurrenttaint(L, key);
        setobj2s(L, key+1, &t->array[i]);
      }
      return 1;
    }
  }
  for (i -= t->sizearray; i < sizenode(t); i++) {
    if (!ttisnil(gval(gnode(t, i)))) {
      if (native) {
        setobjraw(L, key, key2tval(gnode(t, i)));
        setobjraw(L, key+1, gval(gnode(t, i)));
      } else {
        setobj2s(L, key, key2tval(gnode(t, i)));
        setobj2s(L, key+1, gval(gnode(t, i)));
      }
      return 1;
    }
  }
  return 0;
}

static int computesizes (int nums[], int *narray) {
  int i;
  int twotoi;
  int a = 0;
  int na = 0;
  int n = 0;
  for (i = 0, twotoi = 1; twotoi/2 < *narray; i++, twotoi *= 2) {
    if (nums[i] > 0) {
      a += nums[i];
      if (a > twotoi/2) {
        n = twotoi;
        na = a;
      }
    }
    if (a == *narray) break;
  }
  *narray = n;
  lua_assert(*narray/2 <= na && na <= *narray);
  return na;
}

static int countint (const TValue *key, int *nums) {
  int k = arrayindex(key);
  if (0 < k && k <= MAXASIZE) {
    nums[ceillog2(k)]++;
    return 1;
  }
  else
    return 0;
}

static int numusearray (const Table *t, int *nums) {
  int lg;
  int ttlg;
  int ause = 0;
  int i = 1;
  for (lg=0, ttlg=1; lg<=MAXBITS; lg++, ttlg*=2) {
    int lc = 0;
    int lim = ttlg;
    if (lim > t->sizearray) {
      lim = t->sizearray;
      if (i > lim)
        break;
    }

    for (; i <= lim; i++) {
      if (!ttisnil(&t->array[i-1]))
        lc++;
    }
    nums[lg] += lc;
    ause += lc;
  }
  return ause;
}

static int numusehash (const Table *t, int *nums, int *pnasize) {
  int totaluse = 0;
  int ause = 0;
  int i = sizenode(t);
  while (i--) {
    Node *n = &t->node[i];
    if (!ttisnil(gval(n))) {
      ause += countint(key2tval(n), nums);
      totaluse++;
    }
  }
  *pnasize += ause;
  return totaluse;
}

static void setarrayvector (lua_State *L, Table *t, int size,
                            int newcelltaint) {
  int i;
  luaM_reallocvector(L, t->array, t->sizearray, size, TValue);
  for (i=t->sizearray; i<size; i++) {
    setnilvalue(&t->array[i]);
    t->array[i].taint = newcelltaint;
  }
  t->sizearray = size;
  luaC_refreshobject(L, obj2gco(t));
}

static void setnodevector (lua_State *L, Table *t, int size,
                           int newcelltaint) {
  int lsize;
  if (size == 0) {
    t->node = cast(Node *, dummynode);
    lsize = 0;
  }
  else {
    int i;
    lsize = ceillog2(size);
    if (lsize > MAXBITS)
      luaG_runerror(L, "table overflow");
    size = twoto(lsize);
    t->node = luaM_newvector(L, size, Node);
    for (i=0; i<size; i++) {
      Node *n = gnode(t, i);
      gnext(n) = NULL;
      setnilvalue(gkey(n));
      gkey(n)->taint = newcelltaint;
      setnilvalue(gval(n));
      gval(n)->taint = newcelltaint;
    }
  }
  t->lsizenode = cast_byte(lsize);
  t->lastfree = gnode(t, size);
  luaC_refreshobject(L, obj2gco(t));
}

static void resize (lua_State *L, Table *t, int nasize, int nhsize) {
  int i;
  int oldasize = t->sizearray;
  int oldhsize = t->lsizenode;
  Node *nold = t->node;

  if (nasize > oldasize)
    setarrayvector(L, t, nasize, 0);

  setnodevector(L, t, nhsize, 0);
  if (nasize < oldasize) {
    t->sizearray = nasize;

    for (i=nasize; i<oldasize; i++) {
      if (!ttisnil(&t->array[i]))
        setobjraw(L, luaH_setnum(L, t, i+1), &t->array[i]);
    }

    luaM_reallocvector(L, t->array, oldasize, nasize, TValue);
  }

  for (i = twoto(oldhsize) - 1; i >= 0; i--) {
    Node *old = nold+i;
    if (!ttisnil(gval(old)))
      setobjraw(L, luaH_set(L, t, key2tval(old)), gval(old));
  }
  if (nold != dummynode)
    luaM_freearray(L, nold, twoto(oldhsize), Node);
  luaC_refreshobject(L, obj2gco(t));
}

void luaH_resizearray (lua_State *L, Table *t, int nasize) {
  int nsize = (t->node == dummynode) ? 0 : sizenode(t);
  resize(L, t, nasize, nsize);
}

static void rehash (lua_State *L, Table *t, const TValue *ek) {
  int nasize, na;
  int nums[MAXBITS+1];
  int i;
  int totaluse;
  for (i=0; i<=MAXBITS; i++) nums[i] = 0;
  nasize = numusearray(t, nums);
  totaluse = nasize;
  totaluse += numusehash(t, nums, &nasize);

  nasize += countint(ek, nums);
  totaluse++;

  na = computesizes(nums, &nasize);

  resize(L, t, nasize, totaluse - na);
}

Table *luaH_new (lua_State *L, int narray, int nhash) {
  Table *t = luaM_new(L, Table);
  luaC_link(L, obj2gco(t), LUA_TTABLE);
  t->metatable = NULL;
  t->flags = cast_byte(~0);

  t->nativestate = 0;
  t->array = NULL;
  t->sizearray = 0;
  t->lsizenode = 0;
  t->node = cast(Node *, dummynode);
  setarrayvector(L, t, narray, luaW_currenttaint(L));
  setnodevector(L, t, nhash, luaW_currenttaint(L));
  return t;
}

void luaH_free (lua_State *L, Table *t) {
  if (t->node != dummynode)
    luaM_freearray(L, t->node, sizenode(t), Node);
  luaM_freearray(L, t->array, t->sizearray, TValue);
  luaM_free(L, t);
}

static Node *getfreepos (Table *t) {
  while (t->lastfree-- > t->node) {
    if (ttisnil(gkey(t->lastfree)))
      return t->lastfree;
  }
  return NULL;
}

static TValue *newkey (lua_State *L, Table *t, const TValue *key) {
  Node *mp = mainposition(t, key);
  if (!ttisnil(gval(mp)) || mp == dummynode) {
    Node *othern;
    Node *n = getfreepos(t);
    if (n == NULL) {
      rehash(L, t, key);
      return luaH_set(L, t, key);
    }
    lua_assert(n != dummynode);
    othern = mainposition(t, key2tval(mp));
    if (othern != mp) {

      while (gnext(othern) != mp) othern = gnext(othern);
      gnext(othern) = n;
      *n = *mp;
      gnext(mp) = NULL;
      setnilvalue(gval(mp));
      luaW_setcurrenttaint(L, gval(mp));
    }
    else {

      gnext(n) = gnext(mp);
      gnext(mp) = n;
      mp = n;
    }
  }

  gkey(mp)->value = key->value; gkey(mp)->tt = key->tt;
  luaC_barriert(L, t, key);
  lua_assert(ttisnil(gval(mp)));
  return gval(mp);
}

const TValue *luaH_getnum (Table *t, int key) {

  if (cast(unsigned int, key-1) < cast(unsigned int, t->sizearray))
    return &t->array[key-1];
  else {
    lua_Number nk = cast_num(key);
    Node *n = hashnum(t, nk);
    do {
      if (ttisnumber(gkey(n)) && luai_numeq(nvalue(gkey(n)), nk))
        return gval(n);
      else n = gnext(n);
    } while (n);
    return luaO_nilobject;
  }
}

const TValue *luaH_getstr (Table *t, TString *key) {
  Node *n = hashstr(t, key);
  do {
    if (ttisstring(gkey(n)) && rawtsvalue(gkey(n)) == key)
      return gval(n);
    else n = gnext(n);
  } while (n);
  return luaO_nilobject;
}

const TValue *luaH_get (Table *t, const TValue *key) {
  switch (ttype(key)) {
    case LUA_TNIL: return luaO_nilobject;
    case LUA_TSTRING: return luaH_getstr(t, rawtsvalue(key));
    case LUA_TNUMBER: {
      int k;
      lua_Number n = nvalue(key);
      lua_number2int(k, n);
      if (luai_numeq(cast_num(k), nvalue(key)))
        return luaH_getnum(t, k);

    }
    default: {
      Node *n = mainposition(t, key);
      do {
        if (luaO_rawequalObj(key2tval(n), key))
          return gval(n);
        else n = gnext(n);
      } while (n);
      return luaO_nilobject;
    }
  }
}

TValue *luaH_set (lua_State *L, Table *t, const TValue *key) {
  const TValue *p = luaH_get(t, key);
  t->flags = 0;
  if (p != luaO_nilobject)
    return cast(TValue *, p);
  else {
    if (ttisnil(key)) luaG_runerror(L, "table index is nil");
    else if (ttisnumber(key) && luai_numisnan(nvalue(key)))
      luaG_runerror(L, "table index is NaN");
    return newkey(L, t, key);
  }
}

TValue *luaH_setnum (lua_State *L, Table *t, int key) {
  const TValue *p = luaH_getnum(t, key);
  if (p != luaO_nilobject)
    return cast(TValue *, p);
  else {
    TValue k;
    setnvalue(&k, cast_num(key));
    luaW_setcurrenttaint(L, &k);
    return newkey(L, t, &k);
  }
}

TValue *luaH_setstr (lua_State *L, Table *t, TString *key) {
  const TValue *p = luaH_getstr(t, key);
  if (p != luaO_nilobject)
    return cast(TValue *, p);
  else {
    TValue k;
    setsvalue(L, &k, key);
    luaW_setcurrenttaint(L, &k);
    return newkey(L, t, &k);
  }
}

static int unbound_search (Table *t, unsigned int j) {
  unsigned int i = j;
  j++;

  while (!ttisnil(luaH_getnum(t, j))) {
    i = j;
    j *= 2;
    if (j > cast(unsigned int, MAX_INT)) {

      i = 1;
      while (!ttisnil(luaH_getnum(t, i))) i++;
      return i - 1;
    }
  }

  while (j - i > 1) {
    unsigned int m = (i+j)/2;
    if (ttisnil(luaH_getnum(t, m))) j = m;
    else i = m;
  }
  return i;
}

int luaH_getn (Table *t) {
  unsigned int j = t->sizearray;
  if (j > 0 && ttisnil(&t->array[j - 1])) {

    unsigned int i = 0;
    while (j - i > 1) {
      unsigned int m = (i+j)/2;
      if (ttisnil(&t->array[m - 1])) j = m;
      else i = m;
    }
    return i;
  }

  else if (t->node == dummynode)
    return j;
  else return unbound_search(t, j);
}

#if defined(LUA_DEBUG)

Node *luaH_mainposition (const Table *t, const TValue *key) {
  return mainposition(t, key);
}

int luaH_isdummy (Node *n) { return n == dummynode; }

#endif
