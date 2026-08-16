
#include "openwow/ui/framescript_lua_loader.h"

#include "openwow/core/md5.h"
#include "openwow/core/storm_string.h"
#include "openwow/ui/frame_script_events.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/vfs/sfile_core.h"

extern "C" {
#include <lua.hpp>
#include <lua.hpp>
}

#include <cstdarg>
#include <cstring>
#include <vector>

namespace openwow::ui {

using openwow::core::SStrCopy;
using openwow::core::SStrLen;
using openwow::core::SStrPrintf;
using openwow::core::SStrStrI;
using openwow::vfs::SFileOpenFileAndLoadData;
using openwow::vfs::SFileFreeLoadedData;

static bool IsSeparator(char c) {
  return c == '\\' || c == '/';
}

void FrameScript_CollapseDotDotSegments(char* path, std::size_t ) {
  if (!path) return;

  char* dot_dot = SStrStrI(path, "..");
  if (!dot_dot) return;

  while (dot_dot != nullptr) {

    if (dot_dot == path) break;
    char before = *(dot_dot - 1);
    if (!IsSeparator(before)) break;

    char after = dot_dot[2];
    if (!IsSeparator(after)) break;

    char* scan = dot_dot - 2;
    while (scan >= path) {
      if (IsSeparator(*scan)) break;
      --scan;
    }

    SStrCopy(scan + 1, dot_dot + 3, 0x7FFFFFFF);

    dot_dot = SStrStrI(path, "..");
  }
}

std::string FrameScript_NormalizePath(const std::string& path) {

  std::string result = path;
  result.resize(result.size() + 1, '\0');
  FrameScript_CollapseDotDotSegments(result.data(), result.capacity());

  result.resize(std::strlen(result.c_str()));
  return result;
}

static const void* SkipUtf8Bom(const void* data, std::uint32_t* size) {
  auto bytes = static_cast<const std::uint8_t*>(data);
  if (*size >= 3 &&
      bytes[0] == 0xEF &&
      bytes[1] == 0xBB &&
      bytes[2] == 0xBF) {
    *size -= 3;
    return bytes + 3;
  }
  return data;
}

struct BufferReader {
  const char* data;
  std::size_t size;
};

static const char* LuaBufferReader(lua_State* , void* ud, size_t* sz) {
  auto* reader = static_cast<BufferReader*>(ud);
  if (reader->size == 0) {
    *sz = 0;
    return nullptr;
  }
  *sz = reader->size;
  reader->size = 0;
  return reader->data;
}

int FrameXML_CompileWrappedInlineScript(lua_State* L,
                                        const char* chunk_name,
                                        const char* format,
                                        const char* body,
                                        IScriptErrorHandler* err) {

  const std::size_t format_len = SStrLen(format);
  const std::size_t body_len = SStrLen(body);
  const std::size_t buf_size = format_len + body_len + 1;
  std::vector<char> buf(buf_size);

  SStrPrintf(buf.data(), buf_size, format, body);

  const int error_handler_ref =
      openwow::ui::frame_script_events::FrameScript_GetErrorHandlerRef();
  lua_rawgeti(L, LUA_REGISTRYINDEX, error_handler_ref);

  std::uint32_t load_size = static_cast<std::uint32_t>(SStrLen(buf.data()));
  const void* load_data = SkipUtf8Bom(buf.data(), &load_size);

  BufferReader reader{static_cast<const char*>(load_data), load_size};
  int load_result = lua_load(L, LuaBufferReader, &reader, chunk_name);

  if (load_result != 0) {

    if (err) {
      const char* error_msg = lua_tostring(L, -1);
      err->Report(2, "%s", error_msg ? error_msg : "");
    }
    if (lua_pcall(L, 1, 0, 0) != 0) {
      lua_settop(L, -2);
    }
    return -1;
  }

  if (lua_pcall(L, 0, 1, -2) != 0) {

    if (err) {
      const char* error_msg = lua_tostring(L, -1);
      err->Report(2, "%s", error_msg ? error_msg : "");
    }
    lua_settop(L, -3);
    return -1;
  }

  int ref = luaL_ref(L, LUA_REGISTRYINDEX);

  lua_settop(L, -2);

  return ref;
}

int FrameScript_ExecuteLuaBuffer(lua_State* L,
                                 const void* data,
                                 std::uint32_t size,
                                 const char* chunk_name,
                                 IScriptErrorHandler* err,
                                 const char* context) {
  if (!L) return 0;

  int error_handler_ref =
      openwow::ui::frame_script_events::FrameScript_GetErrorHandlerRef();
  lua_rawgeti(L, LUA_REGISTRYINDEX, error_handler_ref);

  std::uint32_t load_size = size;
  const void* load_data = SkipUtf8Bom(data, &load_size);

  BufferReader reader{static_cast<const char*>(load_data), load_size};
  int load_result = lua_load(L, LuaBufferReader, &reader, chunk_name);

  if (load_result != 0) {

    if (err) {
      const char* error_msg = lua_tostring(L, -1);
      err->Report(2, "%s", error_msg ? error_msg : "unknown compile error");
    }

    if (lua_pcall(L, 1, 0, 0) != 0) {
      lua_settop(L, -2);
    }
    return 0;
  }

  int nargs = 0;
  if (context) {
    lua_pushstring(L, context);

    lua_pushvalue(L, -4);
    nargs = 2;
  }

  int errfunc_index = -(nargs + 2);
  if (lua_pcall(L, nargs, 0, errfunc_index) != 0) {

    if (err) {
      const char* error_msg = lua_tostring(L, -1);
      err->Report(2, "%s", error_msg ? error_msg : "unknown runtime error");
    }
    lua_settop(L, -3);
    return 0;
  }

  lua_settop(L, -2);
  return 1;
}

int FrameScript_LoadLuaFile(lua_State* L,
                            const char* path,
                            const char* context,
                            openwow::core::MD5Context* md5_ctx,
                            IScriptErrorHandler* err) {

  char normalized[260];
  const char* effective_path = path;

  if (SStrStrI(path, "..")) {
    SStrCopy(normalized, path, 260);
    FrameScript_CollapseDotDotSegments(normalized, 260);
    effective_path = normalized;
  }

  char chunk_name[261];
  SStrPrintf(chunk_name, 0x105u, "@%s", effective_path);

  void* file_data = nullptr;
  std::size_t file_size = 0;
  if (!SFileOpenFileAndLoadData(nullptr, effective_path, &file_data,
                                &file_size, 0, 1, 0)) {
    if (err) {
      err->Report(2, "Error loading %s", effective_path);
    }
    return 0;
  }

  if (md5_ctx) {
    openwow::core::MD5_Update(md5_ctx, static_cast<const char*>(file_data),
                              file_size);
  }

  int result = FrameScript_ExecuteLuaBuffer(
      L, file_data, static_cast<std::uint32_t>(file_size),
      chunk_name, err, context);

  SFileFreeLoadedData(file_data);

  return result;
}

std::uint32_t FrameScript_SetSandboxClosureOwner(lua_State* L,
                                                 std::uint32_t owner) {
  return lua_setsandboxclosureowner(L, owner);
}

std::uint32_t FrameScript_GetSandboxClosureOwner(lua_State* L) {
  return lua_getsandboxclosureowner(L);
}

int FrameScript_ExecuteSandboxedLuaBuffer(lua_State* L,
                                          const void* data,
                                          std::uint32_t size,
                                          const char* chunk_name,
                                          IScriptErrorHandler* err) {
  if (!L) return 0;

  int error_handler_ref =
      openwow::ui::frame_script_events::FrameScript_GetErrorHandlerRef();
  lua_rawgeti(L, LUA_REGISTRYINDEX, error_handler_ref);

  std::uint32_t load_size = size;
  const void* load_data = SkipUtf8Bom(data, &load_size);

  BufferReader reader{static_cast<const char*>(load_data), load_size};
  int load_result = lua_load(L, LuaBufferReader, &reader, chunk_name);

  if (load_result != 0) {

    if (err) {
      const char* error_msg = lua_tostring(L, -1);
      err->Report(2, "%s", error_msg ? error_msg : "unknown compile error");
    }
    if (lua_pcall(L, 1, 0, 0) != 0) {
      lua_settop(L, lua_gettop(L) - 1);

    }
    return 0;
  }

  lua_createtable(L, 0, 0);

  lua_insert(L, -3);

  lua_pushvalue(L, -3);

  lua_setfenv(L, -2);

  const std::uint32_t previous_sandbox_owner =
      FrameScript_SetSandboxClosureOwner(
          L, kSavedVariablesSandboxClosureOwner);

  int exec_result = lua_pcall(L, 0, 0, -2);

  static_cast<void>(
      FrameScript_SetSandboxClosureOwner(L, previous_sandbox_owner));

  if (exec_result != 0) {

    if (err) {
      const char* error_msg = lua_tostring(L, -1);
      err->Report(2, "%s", error_msg ? error_msg : "unknown runtime error");
    }
    lua_settop(L, lua_gettop(L) - 3);
    return 0;
  }

  lua_settop(L, lua_gettop(L) - 1);

  lua_pushnil(L);

  while (lua_next(L, -2) != 0) {

    if (lua_type(L, -2) == LUA_TSTRING) {
      const char* key_str = lua_tostring(L, -2);
      lua_pushstring(L, key_str);

      lua_insert(L, -2);

      lua_rawset(L, LUA_GLOBALSINDEX);

    } else {
      lua_settop(L, lua_gettop(L) - 1);

    }
  }

  lua_settop(L, lua_gettop(L) - 1);

  return 1;
}

int FrameScript_LoadAndExecuteLuaFile(lua_State* L,
                                      const char* filename,
                                      IScriptErrorHandler* err) {
  if (!L || !filename) return 0;

  char chunk_name[261];
  SStrPrintf(chunk_name, 0x105u, "@%s", filename);

  void* file_data = nullptr;
  std::size_t file_size = 0;
  if (!SFileOpenFileAndLoadData(nullptr, filename, &file_data,
                                &file_size, 0, 1, 0)) {
    if (err) {
      err->Report(2, "Error loading %s", filename);
    }
    return 0;
  }

  int result = FrameScript_ExecuteSandboxedLuaBuffer(
      L, file_data, static_cast<std::uint32_t>(file_size), chunk_name, err);

  SFileFreeLoadedData(file_data);

  return result;
}

}
