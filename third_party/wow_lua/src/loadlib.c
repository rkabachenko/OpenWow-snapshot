/*
** $Id: loadlib.c,v 1.52 2006/04/10 18:27:23 roberto Exp $
** Dynamic library loader for Lua
** See Copyright Notice in lua.h
**
** This module contains an implementation of loadlib for Unix systems
** that have dlfcn, an implementation for Darwin (Mac OS X), an
** implementation for Windows, and a stub for other systems.
*/

#include <stdlib.h>
#include <string.h>

#define loadlib_c
#define LUA_LIB

#include "lauxlib.h"
#include "lobject.h"
#include "lua.h"
#include "lualib.h"

#define LUA_POF		"luaopen_"

#define LUA_OFSEP	"_"

#define LIBPREFIX	"LOADLIB: "

#define POF		LUA_POF
#define LIB_FAIL	"open"

#define ERRLIB		1
#define ERRFUNC		2

#define setprogdir(L)		((void)0)

static void ll_unloadlib (void *lib);
static void *ll_load (lua_State *L, const char *path);
static lua_CFunction ll_sym (lua_State *L, void *lib, const char *sym);

#if defined(LUA_DL_DLOPEN)

#include <dlfcn.h>

static void ll_unloadlib (void *lib) {
  dlclose(lib);
}

static void *ll_load (lua_State *L, const char *path) {
  void *lib = dlopen(path, RTLD_NOW);
  if (lib == NULL) lua_pushstring(L, dlerror());
  return lib;
}

static lua_CFunction ll_sym (lua_State *L, void *lib, const char *sym) {
  lua_CFunction f = (lua_CFunction)dlsym(lib, sym);
  if (f == NULL) lua_pushstring(L, dlerror());
  return f;
}

#elif defined(LUA_DL_DLL)

#include <windows.h>

#undef setprogdir

static void setprogdir (lua_State *L) {
  char buff[MAX_PATH + 1];
  char *lb;
  DWORD nsize = sizeof(buff)/sizeof(char);
  DWORD n = GetModuleFileName(NULL, buff, nsize);
  if (n == 0 || n == nsize || (lb = strrchr(buff, '\\')) == NULL)
    luaL_error(L, "unable to get ModuleFileName");
  else {
    *lb = '\0';
    luaL_gsub(L, lua_tostring(L, -1), LUA_EXECDIR, buff);
    lua_remove(L, -2);
  }
}

static void pusherror (lua_State *L) {
  int error = GetLastError();
  char buffer[128];
  if (FormatMessage(FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM,
      NULL, error, 0, buffer, sizeof(buffer), NULL))
    lua_pushstring(L, buffer);
  else
    lua_pushfstring(L, "system error %d\n", error);
}

static void ll_unloadlib (void *lib) {
  FreeLibrary((HINSTANCE)lib);
}

static void *ll_load (lua_State *L, const char *path) {
  HINSTANCE lib = LoadLibrary(path);
  if (lib == NULL) pusherror(L);
  return lib;
}

static lua_CFunction ll_sym (lua_State *L, void *lib, const char *sym) {
  lua_CFunction f = (lua_CFunction)GetProcAddress((HINSTANCE)lib, sym);
  if (f == NULL) pusherror(L);
  return f;
}

#elif defined(LUA_DL_DYLD)

#include <mach-o/dyld.h>

#undef POF
#define POF	"_" LUA_POF

static void pusherror (lua_State *L) {
  const char *err_str;
  const char *err_file;
  NSLinkEditErrors err;
  int err_num;
  NSLinkEditError(&err, &err_num, &err_file, &err_str);
  lua_pushstring(L, err_str);
}

static const char *errorfromcode (NSObjectFileImageReturnCode ret) {
  switch (ret) {
    case NSObjectFileImageInappropriateFile:
      return "file is not a bundle";
    case NSObjectFileImageArch:
      return "library is for wrong CPU type";
    case NSObjectFileImageFormat:
      return "bad format";
    case NSObjectFileImageAccess:
      return "cannot access file";
    case NSObjectFileImageFailure:
    default:
      return "unable to load library";
  }
}

static void ll_unloadlib (void *lib) {
  NSUnLinkModule((NSModule)lib, NSUNLINKMODULE_OPTION_RESET_LAZY_REFERENCES);
}

static void *ll_load (lua_State *L, const char *path) {
  NSObjectFileImage img;
  NSObjectFileImageReturnCode ret;

  if(!_dyld_present()) {
    lua_pushliteral(L, "dyld not present");
    return NULL;
  }
  ret = NSCreateObjectFileImageFromFile(path, &img);
  if (ret == NSObjectFileImageSuccess) {
    NSModule mod = NSLinkModule(img, path, NSLINKMODULE_OPTION_PRIVATE |
                       NSLINKMODULE_OPTION_RETURN_ON_ERROR);
    NSDestroyObjectFileImage(img);
    if (mod == NULL) pusherror(L);
    return mod;
  }
  lua_pushstring(L, errorfromcode(ret));
  return NULL;
}

static lua_CFunction ll_sym (lua_State *L, void *lib, const char *sym) {
  NSSymbol nss = NSLookupSymbolInModule((NSModule)lib, sym);
  if (nss == NULL) {
    lua_pushfstring(L, "symbol " LUA_QS " not found", sym);
    return NULL;
  }
  return (lua_CFunction)NSAddressOfSymbol(nss);
}

#else

#undef LIB_FAIL
#define LIB_FAIL	"absent"

#define DLMSG	"dynamic libraries not enabled; check your Lua installation"

static void ll_unloadlib (void *lib) {
  (void)lib;
}

static void *ll_load (lua_State *L, const char *path) {
  (void)path;
  lua_pushliteral(L, DLMSG);
  return NULL;
}

static lua_CFunction ll_sym (lua_State *L, void *lib, const char *sym) {
  (void)lib; (void)sym;
  lua_pushliteral(L, DLMSG);
  return NULL;
}

#endif

static void **ll_register (lua_State *L, const char *path) {
  void **plib;
  lua_pushfstring(L, "%s%s", LIBPREFIX, path);
  lua_gettable(L, LUA_REGISTRYINDEX);
  if (!lua_isnil(L, -1))
    plib = (void **)lua_touserdata(L, -1);
  else {
    lua_pop(L, 1);
    plib = (void **)lua_newuserdata(L, sizeof(const void *));
    *plib = NULL;
    luaL_getmetatable(L, "_LOADLIB");
    lua_setmetatable(L, -2);
    lua_pushfstring(L, "%s%s", LIBPREFIX, path);
    lua_pushvalue(L, -2);
    lua_settable(L, LUA_REGISTRYINDEX);
  }
  return plib;
}

static int gctm (lua_State *L) {
  void **lib = (void **)luaL_checkudata(L, 1, "_LOADLIB");
  if (*lib) ll_unloadlib(*lib);
  *lib = NULL;
  return 0;
}

static int ll_loadfunc (lua_State *L, const char *path, const char *sym) {
  void **reg = ll_register(L, path);
  if (*reg == NULL) *reg = ll_load(L, path);
  if (*reg == NULL)
    return ERRLIB;
  else {
    lua_CFunction f = ll_sym(L, *reg, sym);
    if (f == NULL)
      return ERRFUNC;
    lua_pushcfunction(L, f);
    return 0;
  }
}

static int ll_loadlib (lua_State *L) {
  const char *path = luaL_checkstring(L, 1);
  const char *init = luaL_checkstring(L, 2);
  int stat = ll_loadfunc(L, path, init);
  if (stat == 0)
    return 1;
  else {
    lua_pushnil(L);
    lua_insert(L, -2);
    lua_pushstring(L, (stat == ERRLIB) ?  LIB_FAIL : "init");
    return 3;
  }
}

static int readable (const char *filename) {
  FILE *f = fopen(filename, "r");
  if (f == NULL) return 0;
  fclose(f);
  return 1;
}

static const char *pushnexttemplate (lua_State *L, const char *path) {
  const char *l;
  while (*path == *LUA_PATHSEP) path++;
  if (*path == '\0') return NULL;
  l = strchr(path, *LUA_PATHSEP);
  if (l == NULL) l = path + strlen(path);
  lua_pushlstring(L, path, l - path);
  return l;
}

static const char *findfile (lua_State *L, const char *name,
                                           const char *pname) {
  const char *path;
  name = luaL_gsub(L, name, ".", LUA_DIRSEP);
  lua_getfield(L, LUA_ENVIRONINDEX, pname);
  path = lua_tostring(L, -1);
  if (path == NULL)
    luaL_error(L, LUA_QL("package.%s") " must be a string", pname);
  lua_pushstring(L, "");
  while ((path = pushnexttemplate(L, path)) != NULL) {
    const char *filename;
    filename = luaL_gsub(L, lua_tostring(L, -1), LUA_PATH_MARK, name);
    if (readable(filename))
      return filename;
    lua_pop(L, 2);
    luaO_pushfstring(L, "\n\tno file " LUA_QS, filename);
    lua_concat(L, 2);
  }
  return NULL;
}

static void loaderror (lua_State *L, const char *filename) {
  luaL_error(L, "error loading module " LUA_QS " from file " LUA_QS ":\n\t%s",
                lua_tostring(L, 1), filename, lua_tostring(L, -1));
}

static int loader_Lua (lua_State *L) {
  const char *filename;
  const char *name = luaL_checkstring(L, 1);
  filename = findfile(L, name, "path");
  if (filename == NULL) return 1;
  if (luaL_loadfile(L, filename) != 0)
    loaderror(L, filename);
  return 1;
}

static const char *mkfuncname (lua_State *L, const char *modname) {
  const char *funcname;
  const char *mark = strchr(modname, *LUA_IGMARK);
  if (mark) modname = mark + 1;
  funcname = luaL_gsub(L, modname, ".", LUA_OFSEP);
  funcname = lua_pushfstring(L, POF"%s", funcname);
  lua_remove(L, -2);
  return funcname;
}

static int loader_C (lua_State *L) {
  const char *funcname;
  const char *name = luaL_checkstring(L, 1);
  const char *filename = findfile(L, name, "cpath");
  if (filename == NULL) return 1;
  funcname = mkfuncname(L, name);
  if (ll_loadfunc(L, filename, funcname) != 0)
    loaderror(L, filename);
  return 1;
}

static int loader_Croot (lua_State *L) {
  const char *funcname;
  const char *filename;
  const char *name = luaL_checkstring(L, 1);
  const char *p = strchr(name, '.');
  int stat;
  if (p == NULL) return 0;
  lua_pushlstring(L, name, p - name);
  filename = findfile(L, lua_tostring(L, -1), "cpath");
  if (filename == NULL) return 1;
  funcname = mkfuncname(L, name);
  if ((stat = ll_loadfunc(L, filename, funcname)) != 0) {
    if (stat != ERRFUNC) loaderror(L, filename);
    luaO_pushfstring(L, "\n\tno module " LUA_QS " in file " LUA_QS,
                        name, filename);
    return 1;
  }
  return 1;
}

static int loader_preload (lua_State *L) {
  const char *name = luaL_checkstring(L, 1);
  lua_getfield(L, LUA_ENVIRONINDEX, "preload");
  if (!lua_istable(L, -1))
    luaL_error(L, LUA_QL("package.preload") " must be a table");
  lua_getfield(L, -1, name);
  if (lua_isnil(L, -1))
    luaO_pushfstring(L, "\n\tno field package.preload['%s']", name);
  return 1;
}

static const int sentinel_ = 0;
#define sentinel	((void *)&sentinel_)

static int ll_require (lua_State *L) {
  const char *name = luaL_checkstring(L, 1);
  int i;
  lua_settop(L, 1);
  lua_getfield(L, LUA_REGISTRYINDEX, "_LOADED");
  lua_getfield(L, 2, name);
  if (lua_toboolean(L, -1)) {
    if (lua_touserdata(L, -1) == sentinel)
      luaL_error(L, "loop or previous error loading module " LUA_QS, name);
    return 1;
  }

  lua_getfield(L, LUA_ENVIRONINDEX, "loaders");
  if (!lua_istable(L, -1))
    luaL_error(L, LUA_QL("package.loaders") " must be a table");
  lua_pushstring(L, "");
  for (i=1; ; i++) {
    lua_rawgeti(L, -2, i);
    if (lua_isnil(L, -1))
      luaL_error(L, "module " LUA_QS " not found:%s",
                    name, lua_tostring(L, -2));
    lua_pushstring(L, name);
    lua_call(L, 1, 1);
    if (lua_isfunction(L, -1))
      break;
    else if (lua_isstring(L, -1))
      lua_concat(L, 2);
    else
      lua_pop(L, 1);
  }
  lua_pushlightuserdata(L, sentinel);
  lua_setfield(L, 2, name);
  lua_pushstring(L, name);
  lua_call(L, 1, 1);
  if (!lua_isnil(L, -1))
    lua_setfield(L, 2, name);
  lua_getfield(L, 2, name);
  if (lua_touserdata(L, -1) == sentinel) {
    lua_pushboolean(L, 1);
    lua_pushvalue(L, -1);
    lua_setfield(L, 2, name);
  }
  return 1;
}

static void setfenv (lua_State *L) {
  lua_Debug ar;
  lua_getstack(L, 1, &ar);
  lua_getinfo(L, "f", &ar);
  lua_pushvalue(L, -2);
  lua_setfenv(L, -2);
  lua_pop(L, 1);
}

static void dooptions (lua_State *L, int n) {
  int i;
  for (i = 2; i <= n; i++) {
    lua_pushvalue(L, i);
    lua_pushvalue(L, -2);
    lua_call(L, 1, 0);
  }
}

static void modinit (lua_State *L, const char *modname) {
  const char *dot;
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "_M");
  lua_pushstring(L, modname);
  lua_setfield(L, -2, "_NAME");
  dot = strrchr(modname, '.');
  if (dot == NULL) dot = modname;
  else dot++;

  lua_pushlstring(L, modname, dot - modname);
  lua_setfield(L, -2, "_PACKAGE");
}

static int ll_module (lua_State *L) {
  const char *modname = luaL_checkstring(L, 1);
  int loaded = lua_gettop(L) + 1;
  lua_getfield(L, LUA_REGISTRYINDEX, "_LOADED");
  lua_getfield(L, loaded, modname);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);

    if (luaL_findtable(L, LUA_GLOBALSINDEX, modname, 1) != NULL)
      return luaL_error(L, "name conflict for module " LUA_QS, modname);
    lua_pushvalue(L, -1);
    lua_setfield(L, loaded, modname);
  }

  lua_getfield(L, -1, "_NAME");
  if (!lua_isnil(L, -1))
    lua_pop(L, 1);
  else {
    lua_pop(L, 1);
    modinit(L, modname);
  }
  lua_pushvalue(L, -1);
  setfenv(L);
  dooptions(L, loaded - 1);
  return 0;
}

static int ll_seeall (lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  if (!lua_getmetatable(L, 1)) {
    lua_createtable(L, 0, 1);
    lua_pushvalue(L, -1);
    lua_setmetatable(L, 1);
  }
  lua_pushvalue(L, LUA_GLOBALSINDEX);
  lua_setfield(L, -2, "__index");
  return 0;
}

#define AUXMARK		"\1"

static void setpath (lua_State *L, const char *fieldname, const char *envname,
                                   const char *def) {
  const char *path = getenv(envname);
  if (path == NULL)
    lua_pushstring(L, def);
  else {

    path = luaL_gsub(L, path, LUA_PATHSEP LUA_PATHSEP,
                              LUA_PATHSEP AUXMARK LUA_PATHSEP);
    luaL_gsub(L, path, AUXMARK, def);
    lua_remove(L, -2);
  }
  setprogdir(L);
  lua_setfield(L, -2, fieldname);
}

static const luaL_Reg pk_funcs[] = {
  {"loadlib", ll_loadlib},
  {"seeall", ll_seeall},
  {NULL, NULL}
};

static const luaL_Reg ll_funcs[] = {
  {"module", ll_module},
  {"require", ll_require},
  {NULL, NULL}
};

static const lua_CFunction loaders[] =
  {loader_preload, loader_Lua, loader_C, loader_Croot, NULL};

LUALIB_API int luaopen_package (lua_State *L) {
  int i;

  luaL_newmetatable(L, "_LOADLIB");
  lua_pushcfunction(L, gctm);
  lua_setfield(L, -2, "__gc");

  luaL_register(L, LUA_LOADLIBNAME, pk_funcs);
#if defined(LUA_COMPAT_LOADLIB)
  lua_getfield(L, -1, "loadlib");
  lua_setfield(L, LUA_GLOBALSINDEX, "loadlib");
#endif
  lua_pushvalue(L, -1);
  lua_replace(L, LUA_ENVIRONINDEX);

  lua_createtable(L, 0, sizeof(loaders)/sizeof(loaders[0]) - 1);

  for (i=0; loaders[i] != NULL; i++) {
    lua_pushcfunction(L, loaders[i]);
    lua_rawseti(L, -2, i+1);
  }
  lua_setfield(L, -2, "loaders");
  setpath(L, "path", LUA_PATH, LUA_PATH_DEFAULT);
  setpath(L, "cpath", LUA_CPATH, LUA_CPATH_DEFAULT);

  lua_pushstring(L, LUA_DIRSEP "\n" LUA_PATHSEP "\n" LUA_PATH_MARK "\n"
                    LUA_EXECDIR "\n" LUA_IGMARK);
  lua_setfield(L, -2, "config");

  luaL_findtable(L, LUA_REGISTRYINDEX, "_LOADED", 2);
  lua_setfield(L, -2, "loaded");

  lua_newtable(L);
  lua_setfield(L, -2, "preload");
  lua_pushvalue(L, LUA_GLOBALSINDEX);
  luaL_register(L, NULL, ll_funcs);
  lua_pop(L, 1);
  return 1;
}
