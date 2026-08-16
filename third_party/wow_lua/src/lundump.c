/*
** $Id: lundump.c,v 1.60 2006/02/16 15:53:49 lhf Exp $
** load precompiled Lua chunks
** See Copyright Notice in lua.h
*/

#include <limits.h>
#include <stdint.h>
#include <string.h>

#define lundump_c
#define LUA_CORE

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstring.h"
#include "lundump.h"
#include "lzio.h"

typedef struct {
 lua_State* L;
 ZIO* Z;
 Mbuffer* b;
 const char* name;
 int littleendian;
 int depth;
} LoadState;

#ifdef LUAC_TRUST_BINARIES
#define IF(c,s)
#else
#define IF(c,s)  if (c) error(S,s)

static void error(LoadState* S, const char* why)
{
 luaO_pushfstring(S->L,"%s: %s in precompiled chunk",S->name,why);
 luaD_throw(S->L,LUA_ERRSYNTAX);
}
#endif

static void LoadBlock(LoadState* S, void* b, size_t size)
{
 size_t r=luaZ_read(S->Z,b,size);
 IF (r!=0, "unexpected end");
}

static int LoadByte(LoadState* S)
{
 unsigned char x;
 LoadBlock(S,&x,1);
 return x;
}

static uint32_t LoadUnsigned32(LoadState* S)
{
 unsigned char b[4];
 LoadBlock(S,b,sizeof(b));
 if (S->littleendian)
  return (uint32_t)b[0] | ((uint32_t)b[1]<<8) |
         ((uint32_t)b[2]<<16) | ((uint32_t)b[3]<<24);
 return ((uint32_t)b[0]<<24) | ((uint32_t)b[1]<<16) |
        ((uint32_t)b[2]<<8) | (uint32_t)b[3];
}

static uint64_t LoadUnsigned64(LoadState* S)
{
 unsigned char b[8];
 uint64_t x=0;
 int i;
 LoadBlock(S,b,sizeof(b));
 if (S->littleendian)
  for (i=7; i>=0; i--) x=(x<<8)|b[i];
 else
  for (i=0; i<8; i++) x=(x<<8)|b[i];
 return x;
}

static int LoadInt(LoadState* S)
{
 uint32_t x=LoadUnsigned32(S);
 IF (x>(uint32_t)INT_MAX, "bad integer");
 return (int)x;
}

static lua_Number LoadNumber(LoadState* S)
{
 uint64_t bits=LoadUnsigned64(S);
 lua_Number x;
 memcpy(&x,&bits,sizeof(x));
 return x;
}

static TString* LoadString(LoadState* S)
{
 uint32_t serialized_size=LoadUnsigned32(S);
 size_t size;
 IF ((uint64_t)serialized_size>(uint64_t)MAX_SIZET, "string size overflow");
 size=(size_t)serialized_size;
 if (serialized_size==0)
  return NULL;
 else
 {
  char* s=luaZ_openspace(S->L,S->b,size);
  LoadBlock(S,s,size);
  IF (s[size-1]!='\0', "unterminated string");
  return luaS_newlstr(S->L,s,size-1);
 }
}

static void LoadCode(LoadState* S, Proto* f)
{
 int i,n=LoadInt(S);
 IF (n==0, "empty code");
 f->code=luaM_newvector(S->L,n,Instruction);
 f->sizecode=n;
 for (i=0; i<n; i++) f->code[i]=(Instruction)LoadUnsigned32(S);
}

static Proto* LoadFunction(LoadState* S, TString* p);

static void LoadConstants(LoadState* S, Proto* f)
{
 int i,n;
 n=LoadInt(S);
 f->k=luaM_newvector(S->L,n,TValue);
 f->sizek=n;
 for (i=0; i<n; i++) setnilvalue(&f->k[i]);
 for (i=0; i<n; i++)
 {
  TValue* o=&f->k[i];
  int t=LoadByte(S);
  switch (t)
  {
   case LUA_TNIL:
    setnilvalue(o);
    break;
   case LUA_TBOOLEAN:
    setbvalue(o,LoadByte(S));
    break;
   case LUA_TNUMBER:
    setnvalue(o,LoadNumber(S));
    break;
   case LUA_TSTRING:
    setsvalue2n(S->L,o,LoadString(S));
    break;
   default:
    IF (1, "bad constant");
    break;
  }
 }
 n=LoadInt(S);
 f->p=luaM_newvector(S->L,n,Proto*);
 f->sizep=n;
 for (i=0; i<n; i++) f->p[i]=NULL;
 for (i=0; i<n; i++) f->p[i]=LoadFunction(S,f->source);
}

static void LoadDebug(LoadState* S, Proto* f)
{
 int i,n;
 n=LoadInt(S);
 f->lineinfo=luaM_newvector(S->L,n,int);
 f->sizelineinfo=n;
 for (i=0; i<n; i++) f->lineinfo[i]=LoadInt(S);
 n=LoadInt(S);
 f->locvars=luaM_newvector(S->L,n,LocVar);
 f->sizelocvars=n;
 for (i=0; i<n; i++) f->locvars[i].varname=NULL;
 for (i=0; i<n; i++)
 {
  f->locvars[i].varname=LoadString(S);
  f->locvars[i].startpc=LoadInt(S);
  f->locvars[i].endpc=LoadInt(S);
 }
 n=LoadInt(S);
 f->upvalues=luaM_newvector(S->L,n,TString*);
 f->sizeupvalues=n;
 for (i=0; i<n; i++) f->upvalues[i]=NULL;
 for (i=0; i<n; i++) f->upvalues[i]=LoadString(S);
}

static Proto* LoadFunction(LoadState* S, TString* p)
{
 Proto* f;
 IF (S->depth>=LUAI_MAXCCALLS, "code too deep");
 S->depth++;
 f=luaF_newproto(S->L);
 setptvalue2s(S->L,S->L->top,f); incr_top(S->L);
 f->source=LoadString(S); if (f->source==NULL) f->source=p;
 f->linedefined=LoadInt(S);
 f->lastlinedefined=LoadInt(S);
 f->nups=(lu_byte)LoadByte(S);
 f->numparams=(lu_byte)LoadByte(S);
 f->is_vararg=(lu_byte)LoadByte(S);
 f->maxstacksize=(lu_byte)LoadByte(S);
 LoadCode(S,f);
 LoadConstants(S,f);
 LoadDebug(S,f);
 IF (!luaG_checkcode(f), "bad code");
 luaC_refreshobject(S->L,obj2gco(f));
 S->L->top--;
 S->depth--;
 return f;
}

static void LoadHeader(LoadState* S)
{
 unsigned char h[LUAC_HEADERSIZE];
 LoadBlock(S,h,sizeof(h));
 IF (memcmp(h,LUA_SIGNATURE,sizeof(LUA_SIGNATURE)-1)!=0,
     "bad header");
 IF (h[4]!=LUAC_VERSION || h[5]!=LUAC_FORMAT || h[6]>1 ||
     h[7]!=4 || h[8]!=4 || h[9]!=4 || h[10]!=8 || h[11]!=0,
     "bad header");
 IF (sizeof(lua_Number)!=8 || sizeof(Instruction)!=4 || sizeof(int)<4,
     "bad header");
 S->littleendian=(h[6]!=0);
}

Proto* luaU_undump (lua_State* L, ZIO* Z, Mbuffer* buff, const char* name)
{
 LoadState S;
 if (*name=='@' || *name=='=')
  S.name=name+1;
 else if (*name==LUA_SIGNATURE[0])
  S.name="binary string";
 else
  S.name=name;
 S.L=L;
 S.depth=0;
 S.Z=Z;
 S.b=buff;
 S.littleendian=1;
 LoadHeader(&S);
 return LoadFunction(&S,luaS_newliteral(L,"=?"));
}

void luaU_header (char* h)
{
 memcpy(h,LUA_SIGNATURE,sizeof(LUA_SIGNATURE)-1);
 h+=sizeof(LUA_SIGNATURE)-1;
 *h++=(char)LUAC_VERSION;
 *h++=(char)LUAC_FORMAT;
 *h++=1;
 *h++=4;
 *h++=4;
 *h++=4;
 *h++=8;
 *h++=0;
}
