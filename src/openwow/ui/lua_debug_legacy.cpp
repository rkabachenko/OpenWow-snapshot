#include "openwow/ui/lua_c_api_convenience.h"
extern "C" {
#include <lua.hpp>
#include <lua.hpp>
}

#include "openwow/ui/lua_debug_legacy.h"

#include <string>
#include <string_view>

namespace openwow::ui {
namespace {

constexpr std::size_t kLegacyDebugLocalsMaxLength = 4095;

void AppendIndent(std::string& out, const int depth) {
  out.append(static_cast<std::size_t>(depth), ' ');
}

void AppendNamedValuePrefix(std::string& out, const std::string_view name, const int depth) {
  AppendIndent(out, depth);
  out.append(name.data(), name.size());
  out.append(" = ");
}

std::string GetLegacyTableLabel(lua_State* state, int index) {
  index = lua_absindex(state, index);
  lua_getfield(state, index, "__ow_type");
  const char* type_name = lua_tostring(state, -1);
  std::string label =
      (type_name != nullptr && type_name[0] != '\0') ? std::string(type_name) : std::string("<table>");
  lua_pop(state, 1);
  return label;
}

void AppendLegacyDebugValue(lua_State* state, int value_index, std::string_view name, int depth,
                            std::string& out) {
  value_index = lua_absindex(state, value_index);

  switch (lua_type(state, value_index)) {
  case LUA_TNIL:
    AppendNamedValuePrefix(out, name, depth);
    out.append("nil\n");
    return;
  case LUA_TBOOLEAN:
    AppendNamedValuePrefix(out, name, depth);
    out.append(lua_toboolean(state, value_index) != 0 ? "true\n" : "false\n");
    return;
  case LUA_TLIGHTUSERDATA:
  case LUA_TUSERDATA:
    AppendNamedValuePrefix(out, name, depth);
    out.append("<userdata>\n");
    return;
  case LUA_TNUMBER: {
    const char* number_text = lua_tostring(state, value_index);
    AppendNamedValuePrefix(out, name, depth);
    out.append(number_text != nullptr ? number_text : "");
    out.push_back('\n');
    return;
  }
  case LUA_TSTRING: {
    const char* text = lua_tostring(state, value_index);
    AppendNamedValuePrefix(out, name, depth);
    out.push_back('"');
    if (text != nullptr) {
      out.append(text);
    }
    out.append("\"\n");
    return;
  }
  case LUA_TTABLE: {
    AppendNamedValuePrefix(out, name, depth);
    out.append(GetLegacyTableLabel(state, value_index));
    out.append(" {\n");

    if (depth == 0) {
      lua_pushnil(state);
      while (lua_next(state, value_index) != 0) {
        std::string child_name;
        if (lua_isnumber(state, -2) != 0) {
          child_name = std::to_string(static_cast<int>(lua_tonumber(state, -2)));
        } else {
          const char* key_text = lua_tostring(state, -2);
          if (key_text != nullptr) {
            child_name = key_text;
          }
        }
        AppendLegacyDebugValue(state, -1, child_name, 1, out);
        lua_pop(state, 1);
      }
    }

    AppendIndent(out, depth);
    out.append("}\n");
    return;
  }
  case LUA_TFUNCTION: {
    lua_pushvalue(state, value_index);
    lua_Debug info{};
    if (lua_getinfo(state, ">nS", &info) != 0) {
      AppendNamedValuePrefix(out, name, depth);
      if (info.name != nullptr && info.name[0] != '\0') {
        out.append(info.name);
        out.append("() defined ");
      } else {
        out.append("<function> defined ");
      }
      out.append(info.short_src[0] != '\0' ? info.short_src : "?");
      out.push_back(':');
      out.append(std::to_string(info.linedefined));
      out.push_back('\n');
      return;
    }

    AppendNamedValuePrefix(out, name, depth);
    out.append("<function>\n");
    return;
  }
  default:
    AppendNamedValuePrefix(out, name, depth);
    out.push_back('<');
    out.append(luaL_typename(state, value_index));
    out.append(">\n");
    return;
  }
}

void AppendActiveLocals(lua_State* state, lua_Debug& debug_info, std::string& out) {
  int local_index = 1;
  while (const char* local_name = lua_getlocal(state, &debug_info, local_index)) {
    AppendLegacyDebugValue(state, -1, local_name, 0, out);
    lua_pop(state, 1);
    ++local_index;
  }
}

void AppendFunctionUpvalues(lua_State* state, lua_Debug& debug_info, std::string& out) {
  if (lua_getinfo(state, "f", &debug_info) == 0) {
    return;
  }

  int upvalue_index = 1;
  while (const char* upvalue_name = lua_getupvalue(state, -1, upvalue_index)) {
    AppendLegacyDebugValue(state, -1, upvalue_name, 0, out);
    lua_pop(state, 1);
    ++upvalue_index;
  }

  lua_pop(state, 1);
}

}

int PushLegacyDebugStack(lua_State* state) {
  lua_State* inspected_state = state;
  int start = 1;
  int count1 = 12;
  int count2 = 10;
  int can_trim_middle = 1;

  if (lua_type(state, 1) == LUA_TTHREAD) {
    inspected_state = lua_tothread(state, 1);
    start = 0;
    lua_remove(state, 1);
  }

  if (lua_isnumber(state, 1) != 0) {
    start = static_cast<int>(lua_tointeger(state, 1));
    lua_remove(state, 1);
  }
  if (lua_isnumber(state, 1) != 0) {
    count1 = static_cast<int>(lua_tointeger(state, 1));
    lua_remove(state, 1);
  }
  if (lua_isnumber(state, 1) != 0) {
    count2 = static_cast<int>(lua_tointeger(state, 1));
    lua_remove(state, 1);
  }

  int level = start;
  if (lua_gettop(state) == 0) {
    lua_pushliteral(state, "");
  } else if (lua_isstring(state, 1) == 0) {
    return 1;
  }

  lua_Debug debug_info{};
  if (lua_getstack(inspected_state, start, &debug_info) != 0) {
    const int head_limit = start + count1;
    do {
      if (++level > head_limit && can_trim_middle != 0) {
        if (lua_getstack(inspected_state, level + count2, &debug_info) != 0) {
          lua_pushliteral(state, "...\n");
          do {
            ++level;
          } while (lua_getstack(inspected_state, level + count2, &debug_info) != 0);
          can_trim_middle = 0;
        } else {
          --level;
          can_trim_middle = 0;
        }
      } else {
        lua_getinfo(inspected_state, "Snl", &debug_info);
        lua_pushfstring(state, "%s:", debug_info.short_src);
        if (debug_info.currentline > 0) {
          lua_pushfstring(state, "%d:", debug_info.currentline);
        }

        const char name_what = debug_info.namewhat != nullptr ? debug_info.namewhat[0] : '\0';
        switch (name_what) {
        case 'f':
        case 'g':
        case 'l':
        case 'm':
          lua_pushfstring(state, " in function `%s'", debug_info.name);
          break;
        default: {
          const char what = debug_info.what != nullptr ? debug_info.what[0] : '\0';
          if (what == 'm') {
            lua_pushliteral(state, " in main chunk");
          } else if (what == 'C' || what == 't') {
            lua_pushliteral(state, " ?");
          } else {
            lua_pushfstring(state, " in function <%s:%d>", debug_info.short_src,
                            debug_info.linedefined);
          }
          break;
        }
        }

        lua_pushliteral(state, "\n");
        lua_concat(state, lua_gettop(state));
      }
    } while (lua_getstack(inspected_state, level, &debug_info) != 0);
  }

  lua_concat(state, lua_gettop(state));
  return 1;
}

std::string BuildLegacyDebugLocalsString(lua_State* state, const int level) {
  lua_Debug debug_info{};
  if (lua_getstack(state, level, &debug_info) == 0) {
    return {};
  }

  std::string out;
  AppendActiveLocals(state, debug_info, out);
  AppendFunctionUpvalues(state, debug_info, out);
  if (out.size() > kLegacyDebugLocalsMaxLength) {
    out.resize(kLegacyDebugLocalsMaxLength);
  }
  return out;
}

}
