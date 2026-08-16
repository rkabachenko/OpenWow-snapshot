#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

struct lua_State;

namespace openwow::core {
struct MD5Context;
}

namespace openwow::ui {

struct IScriptErrorHandler {
  virtual ~IScriptErrorHandler() = default;

  virtual void Reserved1() {}
  virtual void Reserved2() {}
  virtual void Report(int severity, const char* fmt, ...) = 0;
};

void FrameScript_CollapseDotDotSegments(char* path, std::size_t capacity);

std::string FrameScript_NormalizePath(const std::string& path);

int FrameXML_CompileWrappedInlineScript(lua_State* L,
                                        const char* chunk_name,
                                        const char* format,
                                        const char* body,
                                        IScriptErrorHandler* err);

int FrameScript_ExecuteLuaBuffer(lua_State* L,
                                 const void* data,
                                 std::uint32_t size,
                                 const char* chunk_name,
                                 IScriptErrorHandler* err,
                                 const char* context);

int FrameScript_LoadLuaFile(lua_State* L,
                            const char* path,
                            const char* context,
                            openwow::core::MD5Context* md5_ctx,
                            IScriptErrorHandler* err);

inline constexpr std::uint32_t kSavedVariablesSandboxClosureOwner =
    ~std::uint32_t{0};
[[nodiscard]] std::uint32_t FrameScript_SetSandboxClosureOwner(
    lua_State* state, std::uint32_t owner);
[[nodiscard]] std::uint32_t FrameScript_GetSandboxClosureOwner(
    lua_State* state);

int FrameScript_LoadAndExecuteLuaFile(lua_State* L,
                                      const char* filename,
                                      IScriptErrorHandler* err);

int FrameScript_ExecuteSandboxedLuaBuffer(lua_State* L,
                                          const void* data,
                                          std::uint32_t size,
                                          const char* chunk_name,
                                          IScriptErrorHandler* err);

}
