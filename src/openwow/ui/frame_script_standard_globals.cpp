#include "openwow/ui/frame_script_standard_globals.h"

#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/lua_debug_legacy.h"
#include "openwow/ui/lua_legacy_length.h"
#include "openwow/ui/lua_post_hook_closure.h"
#include "openwow/ui/lua_scrub.h"
#include "openwow/ui/runtime/diagnostics/frame_script_profiling.h"

extern "C" {
#include <lua.hpp>
}

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <string>
#include <string_view>

namespace openwow::ui {
namespace {

constexpr double kDegreesPerRadian = 180.0 / 3.14159265358979323846264338327950288;
constexpr double kRadiansPerDegree = 3.14159265358979323846264338327950288 / 180.0;

const char* ErrorHandlerRegistryKey(const FrameScriptGlobalProfile profile) {
  return profile == FrameScriptGlobalProfile::Glue
             ? kGlueLuaErrorHandlerRegistryKey
             : kGameLuaErrorHandlerRegistryKey;
}

void SetGlobalFunction(lua_State* state, const char* name, lua_CFunction function) {
  ReplaceLuaGlobal(state, name, function);
}

void AliasLibraryFunction(lua_State* state, const char* library, const char* field,
                          const char* global_name) {
  lua_getglobal(state, library);
  if (lua_istable(state, -1) != 0) {
    lua_getfield(state, -1, field);
    ReplaceLuaGlobalValue(state, global_name, -1);
    lua_pop(state, 1);
  }
  lua_pop(state, 1);
}

void SetLibraryFunction(lua_State* state, const char* library, const char* field,
                        lua_CFunction function) {
  lua_getglobal(state, library);
  if (lua_istable(state, -1) != 0) {
    lua_pushcfunction(state, function);
    lua_setfield(state, -2, field);
  }
  lua_pop(state, 1);
}

void AliasGlobalIntoLibrary(lua_State* state, const char* global_name,
                            const char* library, const char* field) {
  lua_getglobal(state, library);
  if (lua_istable(state, -1) != 0) {
    lua_getglobal(state, global_name);
    lua_setfield(state, -2, field);
  }
  lua_pop(state, 1);
}

void AliasLibraryField(lua_State* state, const char* library, const char* field,
                       const char* global_name) {
  lua_getglobal(state, library);
  if (lua_istable(state, -1) != 0) {
    lua_getfield(state, -1, field);
    ReplaceLuaGlobalValue(state, global_name, -1);
    lua_pop(state, 1);
  }
  lua_pop(state, 1);
}

int LuaGetGlobal(lua_State* state) {
  if (lua_isstring(state, 1) == 0) {
    lua_pushnil(state);
    return 1;
  }

  lua_getglobal(state, lua_tostring(state, 1));
  return 1;
}

int LuaSetGlobal(lua_State* state) {
  const char* name = luaL_checkstring(state, 1);
  lua_settop(state, 2);
  lua_setglobal(state, name);
  return 0;
}

int LuaStrTrim(lua_State* state) {
  std::size_t length = 0;
  const char* text = luaL_checklstring(state, 1, &length);
  const char* chars = luaL_optstring(state, 2, " \t\r\n\f\v");
  const std::string_view view(text, length);

  const std::size_t first = view.find_first_not_of(chars);
  if (first == std::string_view::npos) {
    lua_pushliteral(state, "");
    return 1;
  }

  const std::size_t last = view.find_last_not_of(chars);
  lua_pushlstring(state, view.data() + first, last - first + 1u);
  return 1;
}

int LuaStrSplit(lua_State* state) {
  const char* delimiter = luaL_checkstring(state, 1);
  const char* text = luaL_optstring(state, 2, "");
  const int limit = static_cast<int>(luaL_optinteger(state, 3, 0));

  const std::string input(text);
  const std::string separator(delimiter);
  if (separator.empty()) {
    lua_pushstring(state, input.c_str());
    return 1;
  }

  int count = 0;
  std::size_t offset = 0;
  while (offset <= input.size()) {

    if (lua_checkstack(state, 1) == 0) {
      return luaL_error(state, "strsplit: too many results");
    }

    if (limit > 0 && count >= limit - 1) {
      lua_pushstring(state, input.substr(offset).c_str());
      ++count;
      break;
    }

    const std::size_t next = input.find(separator, offset);
    if (next == std::string::npos) {
      lua_pushstring(state, input.substr(offset).c_str());
      ++count;
      break;
    }

    lua_pushstring(state, input.substr(offset, next - offset).c_str());
    ++count;
    offset = next + separator.size();
  }

  return count;
}

int LuaStrJoin(lua_State* state) {
  const char* delimiter = luaL_checkstring(state, 1);
  const int top = lua_gettop(state);

  std::string joined;
  for (int i = 2; i <= top; ++i) {
    if (i > 2) {
      joined += delimiter;
    }
    const char* part = luaL_tolstring(state, i, nullptr);
    if (part != nullptr) {
      joined += part;
    }
    lua_pop(state, 1);
  }

  lua_pushstring(state, joined.c_str());
  return 1;
}

int LuaStrReplace(lua_State* state) {
  luaL_checkany(state, 1);
  luaL_checkany(state, 2);
  const char* replacement = luaL_optstring(state, 3, "");
  lua_getglobal(state, "string");
  lua_getfield(state, -1, "gsub");
  lua_pushvalue(state, 1);
  lua_pushvalue(state, 2);
  lua_pushstring(state, replacement);
  lua_call(state, 3, 2);
  lua_remove(state, -3);
  return 2;
}

int LuaStrConcat(lua_State* state) {
  const int top = lua_gettop(state);
  std::string joined;
  for (int i = 1; i <= top; ++i) {
    const char* part = luaL_tolstring(state, i, nullptr);
    if (part != nullptr) {
      joined += part;
    }
    lua_pop(state, 1);
  }
  lua_pushstring(state, joined.c_str());
  return 1;
}

int LuaStrLenUtf8(lua_State* state) {
  std::size_t length = 0;
  const char* text = luaL_checklstring(state, 1, &length);

  int count = 0;
  for (std::size_t i = 0; i < length; ++i) {
    if ((static_cast<unsigned char>(text[i]) & 0xC0u) != 0x80u) {
      ++count;
    }
  }

  lua_pushinteger(state, count);
  return 1;
}

int LuaWipe(lua_State* state) {
  luaL_checktype(state, 1, LUA_TTABLE);

  lua_pushnil(state);
  while (lua_next(state, 1) != 0) {
    lua_pop(state, 1);
    lua_pushvalue(state, -1);
    lua_pushnil(state);
    lua_rawset(state, 1);
  }

  lua_settop(state, 1);
  return 1;
}

int LuaGetN(lua_State* state) {
  luaL_checktype(state, 1, LUA_TTABLE);
  lua_pushinteger(state, static_cast<lua_Integer>(openwow::ui::LuaLegacyLength(state, 1)));
  return 1;
}

int LuaForEachI(lua_State* state) {
  luaL_checktype(state, 1, LUA_TTABLE);
  luaL_checktype(state, 2, LUA_TFUNCTION);

  const auto length = static_cast<lua_Integer>(openwow::ui::LuaLegacyLength(state, 1));
  for (lua_Integer index = 1; index <= length; ++index) {
    lua_pushvalue(state, 2);
    lua_pushinteger(state, index);
    lua_rawgeti(state, 1, index);
    lua_call(state, 2, 1);
    if (lua_isnil(state, -1) == 0) {
      return 1;
    }
    lua_pop(state, 1);
  }

  return 0;
}

int LuaRemoveMulti(lua_State* state) {
  luaL_checktype(state, 1, LUA_TTABLE);
  const int position = static_cast<int>(luaL_optinteger(state, 2, 1));
  const int count = static_cast<int>(luaL_optinteger(state, 3, 1));
  if (count <= 0) {
    return 0;
  }

  const int length = static_cast<int>(lua_rawlen(state, 1));
  for (int i = position; i <= length - count; ++i) {
    lua_rawgeti(state, 1, i + count);
    lua_rawseti(state, 1, i);
  }
  for (int i = length - count + 1; i <= length; ++i) {
    lua_pushnil(state);
    lua_rawseti(state, 1, i);
  }

  return 0;
}

int LuaSetErrorHandler(lua_State* state) {
  luaL_checktype(state, 1, LUA_TFUNCTION);
  const auto profile = static_cast<FrameScriptGlobalProfile>(
      lua_tointeger(state, lua_upvalueindex(1)));
  lua_pushvalue(state, 1);
  lua_setfield(state, LUA_REGISTRYINDEX, ErrorHandlerRegistryKey(profile));
  return 0;
}

int LuaDefaultMessageErrorHandler(lua_State* state) {
  lua_getglobal(state, "message");
  if (lua_isfunction(state, -1) != 0) {
    lua_pushvalue(state, 1);
    lua_pcall(state, 1, 0, 0);
  } else {
    lua_pop(state, 1);
    if (const char* message = lua_tostring(state, 1); message != nullptr) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                         std::string("Lua error: ") + message);
    }
  }
  return 0;
}

int LuaNoOpErrorHandler(lua_State*) {
  return 0;
}

int LuaGetErrorHandler(lua_State* state) {
  const auto profile = static_cast<FrameScriptGlobalProfile>(
      lua_tointeger(state, lua_upvalueindex(1)));

  lua_getfield(state, LUA_REGISTRYINDEX, ErrorHandlerRegistryKey(profile));
  if (lua_isnil(state, -1) != 0) {
    lua_pop(state, 1);
    lua_pushcfunction(state, profile == FrameScriptGlobalProfile::Glue
                                 ? LuaDefaultMessageErrorHandler
                                 : LuaNoOpErrorHandler);
  }
  return 1;
}

int LuaDebugStack(lua_State* state) {
  return openwow::ui::PushLegacyDebugStack(state);
}

int LuaDebugLocals(lua_State* state) {
  int arg = 1;
  if (lua_isthread(state, arg) != 0) {
    ++arg;
  }

  int level = static_cast<int>(luaL_optinteger(state, arg, 1));
  if (level <= 0) {
    level = 1;
  }

  const std::string result = openwow::ui::BuildLegacyDebugLocalsString(state, level);
  lua_pushstring(state, result.c_str());
  return 1;
}

int LuaDate(lua_State* state) {
  const char* format = luaL_optstring(state, 1, "%c");
  std::time_t time_value = lua_isnumber(state, 2) != 0
                               ? static_cast<std::time_t>(lua_tonumber(state, 2))
                               : std::time(nullptr);

  bool utc = false;
  if (format[0] == '!') {
    utc = true;
    ++format;
  }

  std::tm result{};
  if (utc) {
#if defined(_WIN32)
    gmtime_s(&result, &time_value);
#else
    gmtime_r(&time_value, &result);
#endif
  } else {
#if defined(_WIN32)
    localtime_s(&result, &time_value);
#else
    localtime_r(&time_value, &result);
#endif
  }

  if (std::string_view(format) == "*t") {
    lua_newtable(state);
    lua_pushinteger(state, result.tm_year + 1900);
    lua_setfield(state, -2, "year");
    lua_pushinteger(state, result.tm_mon + 1);
    lua_setfield(state, -2, "month");
    lua_pushinteger(state, result.tm_mday);
    lua_setfield(state, -2, "day");
    lua_pushinteger(state, result.tm_hour);
    lua_setfield(state, -2, "hour");
    lua_pushinteger(state, result.tm_min);
    lua_setfield(state, -2, "min");
    lua_pushinteger(state, result.tm_sec);
    lua_setfield(state, -2, "sec");
    lua_pushinteger(state, result.tm_wday + 1);
    lua_setfield(state, -2, "wday");
    lua_pushinteger(state, result.tm_yday + 1);
    lua_setfield(state, -2, "yday");
    lua_pushboolean(state, result.tm_isdst);
    lua_setfield(state, -2, "isdst");
    return 1;
  }

  char buffer[256]{};
  std::strftime(buffer, sizeof(buffer), format, &result);
  lua_pushstring(state, buffer);
  return 1;
}

int LuaTime(lua_State* state) {
  if (lua_istable(state, 1) == 0) {
    lua_pushnumber(state, static_cast<lua_Number>(std::time(nullptr)));
    return 1;
  }

  std::tm value{};
  lua_getfield(state, 1, "year");
  value.tm_year = static_cast<int>(lua_tointeger(state, -1)) - 1900;
  lua_pop(state, 1);
  lua_getfield(state, 1, "month");
  value.tm_mon = static_cast<int>(lua_tointeger(state, -1)) - 1;
  lua_pop(state, 1);
  lua_getfield(state, 1, "day");
  value.tm_mday = static_cast<int>(lua_tointeger(state, -1));
  lua_pop(state, 1);
  lua_getfield(state, 1, "hour");
  value.tm_hour = static_cast<int>(luaL_optinteger(state, -1, 12));
  lua_pop(state, 1);
  lua_getfield(state, 1, "min");
  value.tm_min = static_cast<int>(luaL_optinteger(state, -1, 0));
  lua_pop(state, 1);
  lua_getfield(state, 1, "sec");
  value.tm_sec = static_cast<int>(luaL_optinteger(state, -1, 0));
  lua_pop(state, 1);
  value.tm_isdst = -1;

  lua_pushnumber(state, static_cast<lua_Number>(std::mktime(&value)));
  return 1;
}

int LuaDiffTime(lua_State* state) {
  const auto t2 = static_cast<std::time_t>(luaL_optnumber(state, 2, 0.0));
  const auto t1 = static_cast<std::time_t>(luaL_checknumber(state, 1));
  lua_pushnumber(state, std::difftime(t1, t2));
  return 1;
}

int LuaCoroutineRunningWotlk(lua_State* state) {
  return lua_pushthread(state) != 0 ? 0 : 1;
}

double CheckDegrees(lua_State* state, const int index) {
  return luaL_checknumber(state, index);
}

int LuaAcosDegrees(lua_State* state) {
  lua_pushnumber(state, std::acos(luaL_checknumber(state, 1)) * kDegreesPerRadian);
  return 1;
}

int LuaAsinDegrees(lua_State* state) {
  lua_pushnumber(state, std::asin(luaL_checknumber(state, 1)) * kDegreesPerRadian);
  return 1;
}

int LuaAtanDegrees(lua_State* state) {
  lua_pushnumber(state, std::atan(luaL_checknumber(state, 1)) * kDegreesPerRadian);
  return 1;
}

int LuaAtan2Degrees(lua_State* state) {
  lua_pushnumber(state, std::atan2(luaL_checknumber(state, 1),
                                   luaL_checknumber(state, 2)) *
                            kDegreesPerRadian);
  return 1;
}

int LuaCosDegrees(lua_State* state) {
  lua_pushnumber(state, std::cos(CheckDegrees(state, 1) * kRadiansPerDegree));
  return 1;
}

int LuaSinDegrees(lua_State* state) {
  lua_pushnumber(state, std::sin(CheckDegrees(state, 1) * kRadiansPerDegree));
  return 1;
}

int LuaTanDegrees(lua_State* state) {
  lua_pushnumber(state, std::tan(CheckDegrees(state, 1) * kRadiansPerDegree));
  return 1;
}

int LuaMathTanh(lua_State* state) {
  lua_pushnumber(state, std::tanh(luaL_checknumber(state, 1)));
  return 1;
}

std::uint32_t CheckBitUnsigned(lua_State* state, const int index) {
  return static_cast<std::uint32_t>(
      static_cast<std::int64_t>(std::nearbyint(luaL_checknumber(state, index))));
}

std::int32_t CheckBitSigned(lua_State* state, const int index) {
  const std::uint32_t value = CheckBitUnsigned(state, index);
  if (value <= 0x7FFFFFFFu) {
    return static_cast<std::int32_t>(value);
  }
  return static_cast<std::int32_t>(
      static_cast<std::int64_t>(value) - 0x100000000LL);
}

void PushUnsigned32(lua_State* state, const std::uint32_t value) {
  lua_pushnumber(state, static_cast<lua_Number>(value));
}

int LuaBitNot(lua_State* state) {
  PushUnsigned32(state, ~CheckBitUnsigned(state, 1));
  return 1;
}

int LuaBitAnd(lua_State* state) {
  auto result = CheckBitUnsigned(state, 1);
  const int top = lua_gettop(state);
  for (int i = 2; i <= top; ++i) {
    result &= CheckBitUnsigned(state, i);
  }
  PushUnsigned32(state, result);
  return 1;
}

int LuaBitOr(lua_State* state) {
  auto result = CheckBitUnsigned(state, 1);
  const int top = lua_gettop(state);
  for (int i = 2; i <= top; ++i) {
    result |= CheckBitUnsigned(state, i);
  }
  PushUnsigned32(state, result);
  return 1;
}

int LuaBitXor(lua_State* state) {
  auto result = CheckBitUnsigned(state, 1);
  const int top = lua_gettop(state);
  for (int i = 2; i <= top; ++i) {
    result ^= CheckBitUnsigned(state, i);
  }
  PushUnsigned32(state, result);
  return 1;
}

int LuaBitLeftShift(lua_State* state) {
  const auto value = CheckBitUnsigned(state, 1);
  const auto shift = CheckBitUnsigned(state, 2) & 31u;
  PushUnsigned32(state, value << shift);
  return 1;
}

int LuaBitRightShift(lua_State* state) {
  const auto value = CheckBitUnsigned(state, 1);
  const auto shift = CheckBitUnsigned(state, 2) & 31u;
  PushUnsigned32(state, value >> shift);
  return 1;
}

int LuaBitArithmeticRightShift(lua_State* state) {
  const auto value = CheckBitSigned(state, 1);
  const auto shift = CheckBitUnsigned(state, 2) & 31u;
  lua_pushnumber(state, static_cast<lua_Number>(value >> shift));
  return 1;
}

int LuaBitMod(lua_State* state) {
  const auto dividend = CheckBitSigned(state, 1);
  const auto divisor = CheckBitSigned(state, 2);
  if (divisor == 0) {
    lua_pushnumber(state, 1.0 / luaL_checknumber(state, 2));
    return 1;
  }

  lua_pushnumber(state, static_cast<lua_Number>(dividend % divisor));
  return 1;
}

int LuaDebugBreak(lua_State*) {
  return 0;
}

void RegisterStringGlobals(lua_State* state) {
  AliasLibraryFunction(state, "string", "byte", "strbyte");
  AliasLibraryFunction(state, "string", "char", "strchar");
  AliasLibraryFunction(state, "string", "find", "strfind");
  AliasLibraryFunction(state, "string", "format", "format");
  AliasLibraryFunction(state, "string", "gmatch", "gmatch");
  AliasLibraryFunction(state, "string", "gsub", "gsub");
  AliasLibraryFunction(state, "string", "len", "strlen");
  AliasLibraryFunction(state, "string", "lower", "strlower");
  AliasLibraryFunction(state, "string", "match", "strmatch");
  AliasLibraryFunction(state, "string", "rep", "strrep");
  AliasLibraryFunction(state, "string", "reverse", "strrev");
  AliasLibraryFunction(state, "string", "sub", "strsub");
  AliasLibraryFunction(state, "string", "upper", "strupper");

  SetGlobalFunction(state, "strtrim", LuaStrTrim);
  SetGlobalFunction(state, "strsplit", LuaStrSplit);
  SetGlobalFunction(state, "strjoin", LuaStrJoin);
  SetGlobalFunction(state, "strreplace", LuaStrReplace);
  SetGlobalFunction(state, "strconcat", LuaStrConcat);
  SetGlobalFunction(state, "strlenutf8", LuaStrLenUtf8);

  AliasGlobalIntoLibrary(state, "strtrim", "string", "trim");
  AliasGlobalIntoLibrary(state, "strsplit", "string", "split");
  AliasGlobalIntoLibrary(state, "strjoin", "string", "join");
  AliasGlobalIntoLibrary(state, "strreplace", "string", "replace");
}

void RegisterTableGlobals(lua_State* state) {
  SetLibraryFunction(state, "table", "wipe", LuaWipe);
  SetLibraryFunction(state, "table", "getn", LuaGetN);
  SetLibraryFunction(state, "table", "foreachi", LuaForEachI);
  SetLibraryFunction(state, "table", "removemulti", LuaRemoveMulti);

  AliasLibraryFunction(state, "table", "foreach", "foreach");
  AliasLibraryFunction(state, "table", "foreachi", "foreachi");
  AliasLibraryFunction(state, "table", "getn", "getn");
  AliasLibraryFunction(state, "table", "insert", "tinsert");
  AliasLibraryFunction(state, "table", "remove", "tremove");
  AliasLibraryFunction(state, "table", "sort", "sort");
  AliasLibraryFunction(state, "table", "wipe", "wipe");
}

void RegisterMathGlobals(lua_State* state) {
  AliasLibraryFunction(state, "math", "abs", "abs");
  SetGlobalFunction(state, "acos", LuaAcosDegrees);
  SetGlobalFunction(state, "asin", LuaAsinDegrees);
  SetGlobalFunction(state, "atan", LuaAtanDegrees);
  SetGlobalFunction(state, "atan2", LuaAtan2Degrees);
  AliasLibraryFunction(state, "math", "ceil", "ceil");
  SetGlobalFunction(state, "cos", LuaCosDegrees);
  AliasLibraryFunction(state, "math", "deg", "deg");
  AliasLibraryFunction(state, "math", "exp", "exp");
  AliasLibraryFunction(state, "math", "floor", "floor");
  AliasLibraryFunction(state, "math", "frexp", "frexp");
  AliasLibraryFunction(state, "math", "ldexp", "ldexp");
  AliasLibraryFunction(state, "math", "log", "log");
  AliasLibraryFunction(state, "math", "log10", "log10");
  AliasLibraryFunction(state, "math", "max", "max");
  AliasLibraryFunction(state, "math", "min", "min");
  AliasLibraryFunction(state, "math", "fmod", "mod");
  AliasLibraryField(state, "math", "pi", "PI");
  AliasLibraryFunction(state, "math", "rad", "rad");
  AliasLibraryFunction(state, "math", "random", "random");
  SetGlobalFunction(state, "sin", LuaSinDegrees);
  AliasLibraryFunction(state, "math", "sqrt", "sqrt");
  SetGlobalFunction(state, "tan", LuaTanDegrees);

  SetLibraryFunction(state, "math", "tanh", LuaMathTanh);
}

void RegisterCoroutineGlobals(lua_State* state) {
  SetLibraryFunction(state, "coroutine", "running", LuaCoroutineRunningWotlk);
}

void RegisterBitwiseGlobals(lua_State* state) {
  lua_newtable(state);
  lua_pushcfunction(state, LuaBitNot);
  lua_setfield(state, -2, "bnot");
  lua_pushcfunction(state, LuaBitAnd);
  lua_setfield(state, -2, "band");
  lua_pushcfunction(state, LuaBitOr);
  lua_setfield(state, -2, "bor");
  lua_pushcfunction(state, LuaBitXor);
  lua_setfield(state, -2, "bxor");
  lua_pushcfunction(state, LuaBitLeftShift);
  lua_setfield(state, -2, "lshift");
  lua_pushcfunction(state, LuaBitRightShift);
  lua_setfield(state, -2, "rshift");
  lua_pushcfunction(state, LuaBitArithmeticRightShift);
  lua_setfield(state, -2, "arshift");
  lua_pushcfunction(state, LuaBitMod);
  lua_setfield(state, -2, "mod");
  ReplaceLuaGlobalValue(state, "bit", -1);
  lua_pop(state, 1);
}

}

void OpenFrameScriptRetailLibraries(lua_State* state) {
  if (state == nullptr) {
    return;
  }

  static constexpr luaL_Reg kRetailLibraries[] = {
      {"", luaopen_base},
      {LUA_TABLIBNAME, luaopen_table},
      {LUA_STRLIBNAME, luaopen_string},
      {LUA_MATHLIBNAME, luaopen_math},
      {nullptr, nullptr},
  };
  for (const luaL_Reg* library = kRetailLibraries;
       library->func != nullptr; ++library) {
    lua_pushcfunction(state, library->func);
    lua_pushstring(state, library->name);
    lua_call(state, 1, 0);
  }

  RegisterBitwiseGlobals(state);

  static constexpr const char* kRetailAbsentGlobals[] = {
      "dofile", "loadfile", "load", "print", "package", "require",
      "module", "io", "os", "debug",
  };
  for (const char* name : kRetailAbsentGlobals) {
    lua_pushnil(state);
    lua_setglobal(state, name);
  }
}

void RegisterFrameScriptStandardGlobals(lua_State* state,
                                        const FrameScriptGlobalProfile profile) {
  if (state == nullptr) {
    return;
  }

  SetGlobalFunction(state, "getglobal", LuaGetGlobal);
  SetGlobalFunction(state, "setglobal", LuaSetGlobal);

  RegisterTableGlobals(state);
  RegisterMathGlobals(state);
  RegisterStringGlobals(state);
  RegisterCoroutineGlobals(state);
  RegisterBitwiseGlobals(state);

  lua_pushinteger(state, static_cast<lua_Integer>(profile));
  lua_pushcclosure(state, LuaSetErrorHandler, 1);
  ReplaceLuaGlobalValue(state, "seterrorhandler", -1);
  lua_pop(state, 1);
  lua_pushinteger(state, static_cast<lua_Integer>(profile));
  lua_pushcclosure(state, LuaGetErrorHandler, 1);
  ReplaceLuaGlobalValue(state, "geterrorhandler", -1);
  lua_pop(state, 1);
  SetGlobalFunction(state, "debugstack", LuaDebugStack);
  SetGlobalFunction(state, "debuglocals", LuaDebugLocals);
  SetGlobalFunction(
      state, "debugprofilestart",
      runtime::diagnostics::LuaDebugProfileStart);
  SetGlobalFunction(
      state, "debugprofilestop",
      runtime::diagnostics::LuaDebugProfileStop);
  SetGlobalFunction(state, "scrub", openwow::ui::LuaScrub);
  SetGlobalFunction(state, "date", LuaDate);
  SetGlobalFunction(state, "time", LuaTime);
  SetGlobalFunction(state, "difftime", LuaDiffTime);

  SetGlobalFunction(state, "debugload", LuaDebugBreak);
  SetGlobalFunction(state, "debuginfo", LuaDebugBreak);
  SetGlobalFunction(state, "debugprint", LuaDebugBreak);
  SetGlobalFunction(state, "debugdump", LuaDebugBreak);
  SetGlobalFunction(state, "debugbreak", LuaDebugBreak);
  SetGlobalFunction(state, "debughook", LuaDebugBreak);
  SetGlobalFunction(state, "debugtimestamp", LuaDebugBreak);
}

}
