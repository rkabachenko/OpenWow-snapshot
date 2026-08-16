#include "openwow/ui/runtime/lua/lua_call.h"

#include <cstdlib>
#include <type_traits>

extern "C" {
#include <lua.hpp>
}

namespace openwow::ui::lua {

static_assert(std::is_trivially_destructible_v<LuaCall>);

int LuaCall::ArgumentCount() const noexcept {
  return lua_gettop(state_);
}

bool LuaCall::IsMissing(const int index) const noexcept {
  return index > 0 && index > lua_gettop(state_);
}

bool LuaCall::IsNil(const int index) const noexcept {
  return lua_isnil(state_, index) != 0;
}

bool LuaCall::IsNumber(const int index) const noexcept {
  return lua_isnumber(state_, index) != 0;
}

bool LuaCall::IsString(const int index) const noexcept {
  return lua_isstring(state_, index) != 0;
}

bool LuaCall::IsBoolean(const int index) const noexcept {
  return lua_type(state_, index) == LUA_TBOOLEAN;
}

bool LuaCall::IsTable(const int index) const noexcept {
  return lua_istable(state_, index) != 0;
}

double LuaCall::Number(const int index) const noexcept {
  return static_cast<double>(lua_tonumber(state_, index));
}

LuaBoolean LuaCall::Boolean(const int index) const noexcept {
  return {IsBoolean(index) && lua_toboolean(state_, index) != 0};
}

LuaTruthy LuaCall::Truthiness(const int index) const noexcept {
  return {lua_toboolean(state_, index) != 0};
}

std::string LuaCall::String(const int index) const {
  std::size_t size = 0;
  const char* value = lua_tolstring(state_, index, &size);
  return value == nullptr ? std::string{} : std::string(value, size);
}

std::optional<std::string> LuaCall::OptionalString(const int index) const {
  if (IsMissing(index) || IsNil(index)) {
    return std::nullopt;
  }
  return String(index);
}

double LuaCall::RequireNumber(const int index, const char* usage) const {
  if (!IsNumber(index)) {
    UsageError(usage);
  }
  return Number(index);
}

std::string LuaCall::RequireString(const int index, const char* usage) const {
  if (!IsString(index)) {
    UsageError(usage);
  }
  return String(index);
}

void LuaCall::UsageError(const char* usage) const {
  luaL_error(state_, "%s", usage);
  std::abort();
}

LuaCall& LuaCall::PushNumber(const double value) {
  lua_pushnumber(state_, static_cast<lua_Number>(value));
  ++result_count_;
  return *this;
}

LuaCall& LuaCall::PushBoolean(const bool value) {
  lua_pushboolean(state_, value ? 1 : 0);
  ++result_count_;
  return *this;
}

LuaCall& LuaCall::PushString(const std::string_view value) {
  lua_pushlstring(state_, value.empty() ? "" : value.data(), value.size());
  ++result_count_;
  return *this;
}

LuaCall& LuaCall::PushStringArgument(const int index) {
  std::size_t size = 0;
  const char* value = lua_tolstring(state_, index, &size);
  lua_pushlstring(state_, value == nullptr ? "" : value, size);
  ++result_count_;
  return *this;
}

LuaCall& LuaCall::PushNil() {
  lua_pushnil(state_);
  ++result_count_;
  return *this;
}

int LuaCall::PushNumberArrayTable(
    const std::span<const std::uint32_t> values) {
  const bool reuse_table = IsTable(1);
  if (!reuse_table) {
    lua_createtable(state_, static_cast<int>(values.size()), 0);
  }

  const int table_index = reuse_table ? 1 : lua_gettop(state_);
  int output_index = 1;
  for (const std::uint32_t value : values) {
    lua_pushnumber(state_, static_cast<lua_Number>(value));
    lua_rawseti(state_, table_index, output_index++);
  }
  if (reuse_table) {
    lua_pushvalue(state_, table_index);
  }
  ++result_count_;
  return 1;
}

int LuaCall::PushBooleanSetTable(
    const std::span<const std::uint32_t> values) {
  const bool reuse_table = IsTable(1);
  if (!reuse_table) {
    lua_newtable(state_);
  }

  const int table_index = reuse_table ? 1 : lua_gettop(state_);
  for (const std::uint32_t value : values) {
    lua_pushnumber(state_, static_cast<lua_Number>(value));
    lua_pushboolean(state_, 1);
    lua_rawset(state_, table_index);
  }
  if (reuse_table) {
    lua_pushvalue(state_, table_index);
  }
  ++result_count_;
  return 1;
}

std::string LuaCall::ReadGlobalString(const std::string_view key) {
  lua_pushlstring(state_, key.data(), key.size());
  lua_gettable(state_, LUA_GLOBALSINDEX);

  std::size_t size = 0;
  const char* value = lua_tolstring(state_, -1, &size);
  std::string result = value == nullptr ? std::string{} : std::string(value, size);
  lua_pop(state_, 1);
  return result;
}

}
