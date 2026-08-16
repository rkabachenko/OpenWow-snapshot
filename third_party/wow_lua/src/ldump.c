/*
** $Id: ldump.c,v 1.15 2006/02/16 15:53:49 lhf Exp $
** save precompiled Lua chunks
** See Copyright Notice in lua.h
*/

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ldump_c
#define LUA_CORE

#include "lua.h"

#include "lobject.h"
#include "lstate.h"
#include "lundump.h"

typedef struct {
 lua_State* L;
 lua_Writer writer;
 void* data;
 int strip;
 int status;
} DumpState;

static void DumpBlock(const void* b, size_t size, DumpState* D)
{
 if (D->status==0)
 {
  lua_unlock(D->L);
  D->status=(*D->writer)(D->L,b,size,D->data);
  lua_lock(D->L);
 }
}

static void DumpChar(int y, DumpState* D)
{
 unsigned char x=(unsigned char)y;
 DumpBlock(&x,1,D);
}

static void DumpUnsigned32(uint32_t x, DumpState* D)
{
 unsigned char b[4];
 b[0]=(unsigned char)x;
 b[1]=(unsigned char)(x>>8);
 b[2]=(unsigned char)(x>>16);
 b[3]=(unsigned char)(x>>24);
 DumpBlock(b,sizeof(b),D);
}

static void DumpInt(int x, DumpState* D)
{
 DumpUnsigned32((uint32_t)x,D);
}

static void DumpUnsigned64(uint64_t x, DumpState* D)
{
 unsigned char b[8];
 int i;
 for (i=0; i<8; i++)
  b[i]=(unsigned char)(x>>(i*8));
 DumpBlock(b,sizeof(b),D);
}

static void DumpNumber(lua_Number x, DumpState* D)
{
 uint64_t bits;
 memcpy(&bits,&x,sizeof(bits));
 DumpUnsigned64(bits,D);
}

static void DumpString(const TString* s, DumpState* D)
{
 if (s==NULL || getstr(s)==NULL)
  DumpUnsigned32(0,D);
 else
 {
  uint32_t size;
  if (s->tsv.len>=UINT32_MAX)
  {
   D->status=1;
   return;
  }
  size=(uint32_t)s->tsv.len+1;
  DumpUnsigned32(size,D);
  DumpBlock(getstr(s),size,D);
 }
}

static void DumpCode(const Proto* f, DumpState* D)
{
 int i;
 DumpInt(f->sizecode,D);
 for (i=0; i<f->sizecode; i++)
  DumpUnsigned32((uint32_t)f->code[i],D);
}

static void DumpFunction(const Proto* f, const TString* p, DumpState* D);

static void DumpConstants(const Proto* f, DumpState* D)
{
 int i,n=f->sizek;
 DumpInt(n,D);
 for (i=0; i<n; i++)
 {
  const TValue* o=&f->k[i];
  DumpChar(ttype(o),D);
  switch (ttype(o))
  {
   case LUA_TNIL:
    break;
   case LUA_TBOOLEAN:
    DumpChar(bvalue(o),D);
    break;
   case LUA_TNUMBER:
    DumpNumber(nvalue(o),D);
    break;
   case LUA_TSTRING:
    DumpString(rawtsvalue(o),D);
    break;
   default:
    lua_assert(0);
    break;
  }
 }
 n=f->sizep;
 DumpInt(n,D);
 for (i=0; i<n; i++) DumpFunction(f->p[i],f->source,D);
}

static void DumpDebug(const Proto* f, DumpState* D)
{
 int i,n;
 n=(D->strip) ? 0 : f->sizelineinfo;
 DumpInt(n,D);
 for (i=0; i<n; i++) DumpInt(f->lineinfo[i],D);
 n=(D->strip) ? 0 : f->sizelocvars;
 DumpInt(n,D);
 for (i=0; i<n; i++)
 {
  DumpString(f->locvars[i].varname,D);
  DumpInt(f->locvars[i].startpc,D);
  DumpInt(f->locvars[i].endpc,D);
 }
 n=(D->strip) ? 0 : f->sizeupvalues;
 DumpInt(n,D);
 for (i=0; i<n; i++) DumpString(f->upvalues[i],D);
}

static void DumpFunction(const Proto* f, const TString* p, DumpState* D)
{
 DumpString((f->source==p || D->strip) ? NULL : f->source,D);
 DumpInt(f->linedefined,D);
 DumpInt(f->lastlinedefined,D);
 DumpChar(f->nups,D);
 DumpChar(f->numparams,D);
 DumpChar(f->is_vararg,D);
 DumpChar(f->maxstacksize,D);
 DumpCode(f,D);
 DumpConstants(f,D);
 DumpDebug(f,D);
}

static void DumpHeader(DumpState* D)
{
 char h[LUAC_HEADERSIZE];
 luaU_header(h);
 DumpBlock(h,LUAC_HEADERSIZE,D);
}

int luaU_dump (lua_State* L, const Proto* f, lua_Writer w, void* data, int strip)
{
 DumpState D;
 if (sizeof(lua_Number)!=8 || sizeof(Instruction)!=4 || sizeof(int)<4)
  return 1;
 D.L=L;
 D.writer=w;
 D.data=data;
 D.strip=strip;
 D.status=0;
 DumpHeader(&D);
 DumpFunction(f,NULL,&D);
 return D.status;
}
