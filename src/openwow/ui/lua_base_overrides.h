#pragma once

#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/lua_c_api_convenience.h"

#include "openwow/core/storm_string.h"

extern "C" {
#include <lua.hpp>
}

#include "openwow/ui/game/lua_addon_memory_tracker.h"
#include "openwow/foundation/text/ascii.h"

#include <cctype>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::ui {

namespace detail {

inline constexpr char kLuaBaseOverridesInstalledKey[] = "openwow.lua_base_overrides.installed";
inline constexpr char kLuaUnnamedChunk[] = "?";

inline void TranslateTransparentWrapperStackLevel(lua_State* state,
                                                  const bool default_level_one) {
  if (lua_gettop(state) == 0 && default_level_one) {
    lua_pushinteger(state, 2);
    return;
  }
  if (lua_isnumber(state, 1) == 0) {
    return;
  }
  const lua_Integer level = lua_tointeger(state, 1);
  if (level <= 0) {
    return;
  }

  lua_pushinteger(state, level + 1);
  lua_replace(state, 1);
}

inline int LuaBaseGetfenvWithEnvironmentProxy(lua_State* state) {
  TranslateTransparentWrapperStackLevel(state, true);
  lua_pushvalue(state, lua_upvalueindex(1));
  lua_insert(state, 1);
  lua_call(state, lua_gettop(state) - 1, 1);

  if (lua_getmetatable(state, -1) == 0) {
    return 1;
  }

  lua_pushliteral(state, "__environment");
  lua_rawget(state, -2);
  if (lua_isnil(state, -1) != 0) {
    lua_pop(state, 2);
    return 1;
  }

  lua_replace(state, -3);
  lua_pop(state, 1);
  return 1;
}

inline const char* NormalizeLuaChunkName(const char* chunk_name) {
  return chunk_name != nullptr ? chunk_name : kLuaUnnamedChunk;
}

inline bool LuaIdentChar(const char ch) {
  const auto uch = static_cast<unsigned char>(ch);
  return std::isalnum(uch) != 0 || ch == '_';
}

inline bool LuaIdentStart(const char ch) {
  const auto uch = static_cast<unsigned char>(ch);
  return std::isalpha(uch) != 0 || ch == '_';
}

inline bool LuaTokenAt(std::string_view text, const std::size_t pos,
                       std::string_view token) {
  if (pos + token.size() > text.size() || text.substr(pos, token.size()) != token) {
    return false;
  }
  if (pos > 0 && LuaIdentChar(text[pos - 1])) {
    return false;
  }
  const std::size_t end = pos + token.size();
  return end >= text.size() || !LuaIdentChar(text[end]);
}

inline std::size_t SkipLuaSpace(std::string_view text, std::size_t pos) {
  while (pos < text.size()) {
    const auto ch = static_cast<unsigned char>(text[pos]);
    if (std::isspace(ch) == 0) {
      break;
    }
    ++pos;
  }
  return pos;
}

inline std::size_t LuaLongBracketEquals(std::string_view text, std::size_t pos) {
  if (pos >= text.size() || text[pos] != '[') {
    return std::string_view::npos;
  }
  std::size_t scan = pos + 1;
  while (scan < text.size() && text[scan] == '=') {
    ++scan;
  }
  return scan < text.size() && text[scan] == '[' ? scan - pos - 1
                                                 : std::string_view::npos;
}

inline std::size_t SkipLuaLongBracket(std::string_view text, std::size_t pos,
                                      const std::size_t equals_count) {
  pos += equals_count + 2u;
  while (pos < text.size()) {
    if (text[pos] == ']') {
      std::size_t scan = pos + 1u;
      std::size_t equals_seen = 0u;
      while (scan < text.size() && text[scan] == '=') {
        ++scan;
        ++equals_seen;
      }
      if (equals_seen == equals_count && scan < text.size() && text[scan] == ']') {
        return scan + 1u;
      }
    }
    ++pos;
  }
  return text.size();
}

inline std::size_t SkipLuaQuotedString(std::string_view text, std::size_t pos) {
  const char quote = text[pos++];
  while (pos < text.size()) {
    const char ch = text[pos++];
    if (ch == '\\' && pos < text.size()) {
      ++pos;
      continue;
    }
    if (ch == quote) {
      break;
    }
  }
  return pos;
}

inline std::size_t SkipLuaComment(std::string_view text, std::size_t pos) {
  pos += 2u;
  if (const std::size_t equals = LuaLongBracketEquals(text, pos);
      equals != std::string_view::npos) {
    return SkipLuaLongBracket(text, pos, equals);
  }
  while (pos < text.size() && text[pos] != '\n') {
    ++pos;
  }
  return pos;
}

inline std::optional<std::size_t> FindLuaDoToken(std::string_view text,
                                                 std::size_t pos) {
  while (pos < text.size()) {
    if (text[pos] == '-' && pos + 1u < text.size() && text[pos + 1u] == '-') {
      pos = SkipLuaComment(text, pos);
      continue;
    }
    if (text[pos] == '\'' || text[pos] == '"') {
      pos = SkipLuaQuotedString(text, pos);
      continue;
    }
    if (const std::size_t equals = LuaLongBracketEquals(text, pos);
        equals != std::string_view::npos) {
      pos = SkipLuaLongBracket(text, pos, equals);
      continue;
    }
    if (LuaTokenAt(text, pos, "do")) {
      return pos;
    }
    ++pos;
  }
  return std::nullopt;
}

inline std::optional<std::string_view> ReadLuaIdentifier(std::string_view text,
                                                         std::size_t* pos) {
  std::size_t scan = SkipLuaSpace(text, *pos);
  if (scan >= text.size() || !LuaIdentStart(text[scan])) {
    return std::nullopt;
  }
  const std::size_t start = scan++;
  while (scan < text.size() && LuaIdentChar(text[scan])) {
    ++scan;
  }
  *pos = scan;
  return text.substr(start, scan - start);
}

inline std::string JoinLuaIdentifiers(const std::vector<std::string>& names) {
  std::string joined;
  for (std::size_t i = 0; i < names.size(); ++i) {
    if (i != 0) {
      joined.append(", ");
    }
    joined.append(names[i]);
  }
  return joined;
}

struct LuaSourceEdit {
  std::size_t begin = 0;
  std::size_t end = 0;
  std::string replacement;
};

inline std::optional<std::string> RewriteLua51MutableGenericForVariables(
    std::string_view chunk) {
#if LUA_VERSION_NUM >= 504
  std::vector<LuaSourceEdit> edits;
  std::size_t loop_index = 0;

  for (std::size_t pos = 0; pos < chunk.size();) {
    if (chunk[pos] == '-' && pos + 1u < chunk.size() && chunk[pos + 1u] == '-') {
      pos = SkipLuaComment(chunk, pos);
      continue;
    }
    if (chunk[pos] == '\'' || chunk[pos] == '"') {
      pos = SkipLuaQuotedString(chunk, pos);
      continue;
    }
    if (const std::size_t equals = LuaLongBracketEquals(chunk, pos);
        equals != std::string_view::npos) {
      pos = SkipLuaLongBracket(chunk, pos, equals);
      continue;
    }
    if (!LuaTokenAt(chunk, pos, "for")) {
      ++pos;
      continue;
    }

    std::size_t cursor = pos + 3u;
    cursor = SkipLuaSpace(chunk, cursor);
    const std::size_t vars_begin = cursor;
    std::vector<std::string> vars;
    while (true) {
      auto ident = ReadLuaIdentifier(chunk, &cursor);
      if (!ident.has_value()) {
        break;
      }
      vars.emplace_back(*ident);
      cursor = SkipLuaSpace(chunk, cursor);
      if (cursor >= chunk.size() || chunk[cursor] != ',') {
        break;
      }
      ++cursor;
    }
    const std::size_t vars_end = cursor;
    cursor = SkipLuaSpace(chunk, cursor);
    if (vars.empty() || !LuaTokenAt(chunk, cursor, "in")) {
      ++pos;
      continue;
    }

    const auto do_pos = FindLuaDoToken(chunk, cursor + 2u);
    if (!do_pos.has_value()) {
      ++pos;
      continue;
    }

    std::vector<std::string> shadows;
    shadows.reserve(vars.size());
    for (const auto& var : vars) {
      shadows.push_back("__ow_lua51_for_" + std::to_string(loop_index) + "_" + var);
    }

    edits.push_back({
        .begin = vars_begin,
        .end = vars_end,
        .replacement = JoinLuaIdentifiers(shadows) + " ",
    });
    edits.push_back({
        .begin = *do_pos + 2u,
        .end = *do_pos + 2u,
        .replacement = " local " + JoinLuaIdentifiers(vars) + " = " +
                       JoinLuaIdentifiers(shadows) + ";",
    });

    ++loop_index;
    pos = *do_pos + 2u;
  }

  if (edits.empty()) {
    return std::nullopt;
  }

  std::string rewritten;
  rewritten.reserve(chunk.size() + edits.size() * 32u);
  std::size_t cursor = 0;
  for (const auto& edit : edits) {
    if (edit.begin < cursor) {
      continue;
    }
    rewritten.append(chunk.substr(cursor, edit.begin - cursor));
    rewritten.append(edit.replacement);
    cursor = edit.end;
  }
  rewritten.append(chunk.substr(cursor));
  return rewritten;
#else
  (void)chunk;
  return std::nullopt;
#endif
}

inline int LuaBaseLoadString(lua_State* state) {
  std::size_t code_length = 0;
  const char* code = luaL_checklstring(state, 1, &code_length);

  const char* chunk_name = code;
  if (lua_type(state, 2) > LUA_TNIL) {
    chunk_name = luaL_checkstring(state, 2);
  }

  std::string_view chunk(code, code_length);
  chunk = openwow::text::StripUtf8Bom(chunk);
  if (luaL_loadbuffer(state, chunk.data(), chunk.size(), NormalizeLuaChunkName(chunk_name)) != 0) {
    lua_pushnil(state);
    lua_insert(state, -2);
    return 2;
  }

  return 1;
}

inline int LuaBaseSetfenvWithMemoryTracking(lua_State* state) {
  TranslateTransparentWrapperStackLevel(state, false);
  lua_pushvalue(state, lua_upvalueindex(1));
  lua_insert(state, 1);
  lua_call(state, lua_gettop(state) - 1, LUA_MULTRET);

  const int result_count = lua_gettop(state);
  if (result_count == 1 && lua_type(state, 1) == LUA_TFUNCTION &&
      lua_iscfunction(state, 1) == 0) {
    openwow::ui::game::MarkLuaClosureEnvironmentSidecar(
        state, lua_topointer(state, 1));
  }
  return result_count;
}

inline std::uint32_t LuaBaseUppercaseLegacyStringCodepoint(const std::int32_t codepoint) {
  const auto value = static_cast<std::uint32_t>(codepoint);
  if ((value >= 0x61u && value <= 0x7Au) || (value >= 0xE0u && value <= 0xFEu) ||
      (value >= 0x430u && value <= 0x44Fu)) {
    return value - 0x20u;
  }
  if (value == 0x451u) {
    return 0x401u;
  }
  return value;
}

inline std::uint32_t LuaBaseLowercaseLegacyStringCodepoint(const std::int32_t codepoint) {
  const auto value = static_cast<std::uint32_t>(codepoint);
  if ((value >= 0x41u && value <= 0x5Au) || (value >= 0xC0u && value <= 0xDEu) ||
      (value >= 0x410u && value <= 0x42Fu)) {
    return value + 0x20u;
  }
  if (value == 0x401u) {
    return 0x451u;
  }
  return value;
}

inline int LuaBaseTransformLegacyStringCase(lua_State* state,
                                            const bool uppercase) {
  std::size_t text_length = 0;
  const char* text = luaL_checklstring(state, 1, &text_length);

  luaL_Buffer buffer;
  luaL_buffinit(state, &buffer);

  std::size_t offset = 0;
  while (offset < text_length) {
    std::uint32_t bytes_consumed = 0;
    const std::int32_t codepoint =
        openwow::core::DecodeNextLegacyUtf8Codepoint(text + offset, &bytes_consumed);
    if (bytes_consumed == 0) {
      break;
    }
    offset += bytes_consumed;

    const std::uint32_t transformed =
        uppercase ? LuaBaseUppercaseLegacyStringCodepoint(codepoint)
                  : LuaBaseLowercaseLegacyStringCodepoint(codepoint);

    char encoded[8];
    char* encoded_end = openwow::core::EncodeLegacyUtf8Codepoint(transformed, encoded);
    luaL_addlstring(&buffer, encoded, static_cast<std::size_t>(encoded_end - encoded));
  }

  luaL_pushresult(&buffer);
  return 1;
}

inline int LuaBaseStringLower(lua_State* state) {
  return LuaBaseTransformLegacyStringCase(state, false);
}

inline int LuaBaseStringUpper(lua_State* state) {
  return LuaBaseTransformLegacyStringCase(state, true);
}

inline int LuaBaseStringGfindDeprecated(lua_State* state) {
  return luaL_error(state, "'string.gfind' was renamed to 'string.gmatch'");
}

inline int LuaBaseAssert(lua_State* state) {
  luaL_checkany(state, 1);
  if (lua_toboolean(state, 1) == 0) {
    const char* message = luaL_optstring(state, 2, "assertion failed!");
    return luaL_error(state, "%s", message);
  }
  return lua_gettop(state);
}

inline int LuaBaseCheckIntegerLikeFrameScript(lua_State* state, const int index) {
  const int value = static_cast<int>(lua_tointeger(state, index));
  if (value == 0 && lua_isnumber(state, index) == 0) {
    luaL_checknumber(state, index);
  }
  return value;
}

inline int LuaBaseUnpack(lua_State* state) {
  luaL_checktype(state, 1, LUA_TTABLE);

  const int i_start = (lua_type(state, 2) > LUA_TNIL)
                          ? LuaBaseCheckIntegerLikeFrameScript(state, 2)
                          : 1;

  const int i_end = (lua_type(state, 3) > LUA_TNIL)
                        ? LuaBaseCheckIntegerLikeFrameScript(state, 3)
                        : static_cast<int>(lua_rawlen(state, 1));

  const std::int64_t count = static_cast<std::int64_t>(i_end) -
                             static_cast<std::int64_t>(i_start) + 1;
  if (count <= 0) {
    return 0;
  }
  if (count > std::numeric_limits<int>::max()) {
    return luaL_error(state, "table too big to unpack");
  }
  const int n = static_cast<int>(count);

  luaL_checkstack(state, n, "table too big to unpack");

  for (std::int64_t offset = 0; offset < count; ++offset) {
    lua_rawgeti(state, 1, static_cast<int>(
                              static_cast<std::int64_t>(i_start) + offset));
  }

  return n;
}

}

inline int LoadClientLuaChunk(lua_State* state, std::string_view chunk, const char* chunk_name) {

  chunk = openwow::text::StripUtf8Bom(chunk);
  if (const auto lua51_chunk = detail::RewriteLua51MutableGenericForVariables(chunk);
      lua51_chunk.has_value()) {
    return luaL_loadbuffer(state, lua51_chunk->data(), lua51_chunk->size(),
                           detail::NormalizeLuaChunkName(chunk_name));
  }
  return luaL_loadbuffer(state, chunk.data(), chunk.size(),
                         detail::NormalizeLuaChunkName(chunk_name));
}

inline void InstallLuaBaseOverrides(lua_State* state) {
  if (state == nullptr) {
    return;
  }

  lua_getfield(state, LUA_REGISTRYINDEX, detail::kLuaBaseOverridesInstalledKey);
  const bool installed = lua_toboolean(state, -1) != 0;
  lua_pop(state, 1);
  if (installed) {
    return;
  }

  lua_getglobal(state, "getfenv");
  if (lua_isfunction(state, -1) != 0) {
    lua_pushcclosure(state, detail::LuaBaseGetfenvWithEnvironmentProxy, 1);
    ReplaceLuaGlobalValue(state, "getfenv", -1);
    lua_pop(state, 1);
  } else {
    lua_pop(state, 1);
  }

  lua_getglobal(state, "setfenv");
  if (lua_isfunction(state, -1) != 0) {
    lua_pushcclosure(state, detail::LuaBaseSetfenvWithMemoryTracking, 1);
    ReplaceLuaGlobalValue(state, "setfenv", -1);
    lua_pop(state, 1);
  } else {
    lua_pop(state, 1);
  }

  ReplaceLuaGlobal(state, "loadstring", detail::LuaBaseLoadString);

  ReplaceLuaGlobal(state, "assert", detail::LuaBaseAssert);

  ReplaceLuaGlobal(state, "unpack", detail::LuaBaseUnpack);

  lua_getglobal(state, "string");
  if (lua_istable(state, -1) != 0) {
    lua_pushcfunction(state, detail::LuaBaseStringLower);
    lua_setfield(state, -2, "lower");
    lua_pushcfunction(state, detail::LuaBaseStringUpper);
    lua_setfield(state, -2, "upper");

    lua_pushcfunction(state, detail::LuaBaseStringGfindDeprecated);
    lua_setfield(state, -2, "gfind");
  }
  lua_pop(state, 1);

  lua_pushboolean(state, 1);
  lua_setfield(state, LUA_REGISTRYINDEX, detail::kLuaBaseOverridesInstalledKey);
}

}
