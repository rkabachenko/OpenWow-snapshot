#pragma once

#include <cstdint>
#include <concepts>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

struct lua_State;

namespace openwow::ui::lua {

struct LuaNil final {};
struct LuaTruthy final {
  bool value{false};
  [[nodiscard]] explicit operator bool() const noexcept { return value; }
};
struct LuaBoolean final {
  bool value{false};
  [[nodiscard]] explicit operator bool() const noexcept { return value; }
};
struct NoLuaResults final {};

template <typename... Values>
class LuaReturns final {
 public:
  explicit LuaReturns(Values... values)
      : values_(std::move(values)...) {}

  [[nodiscard]] const std::tuple<Values...>& values() const noexcept {
    return values_;
  }

 private:
  std::tuple<Values...> values_;
};

template <>
class LuaReturns<> final {
 public:
  [[nodiscard]] static constexpr int arity() noexcept { return 0; }
};

class LuaCall final {
 public:
  explicit LuaCall(lua_State* state) noexcept : state_(state) {}

  [[nodiscard]] int ArgumentCount() const noexcept;
  [[nodiscard]] bool IsMissing(int index) const noexcept;
  [[nodiscard]] bool IsNil(int index) const noexcept;
  [[nodiscard]] bool IsNumber(int index) const noexcept;
  [[nodiscard]] bool IsString(int index) const noexcept;
  [[nodiscard]] bool IsBoolean(int index) const noexcept;
  [[nodiscard]] bool IsTable(int index) const noexcept;
  [[nodiscard]] double Number(int index) const noexcept;
  [[nodiscard]] LuaBoolean Boolean(int index) const noexcept;
  [[nodiscard]] LuaTruthy Truthiness(int index) const noexcept;
  [[nodiscard]] std::string String(int index) const;
  [[nodiscard]] std::optional<std::string> OptionalString(int index) const;

  [[nodiscard]] double RequireNumber(int index, const char* usage) const;
  [[nodiscard]] std::string RequireString(int index, const char* usage) const;
  [[noreturn]] void UsageError(const char* usage) const;

  LuaCall& PushNumber(double value);
  template <std::integral T>
    requires(!std::same_as<T, bool>)
  LuaCall& PushNumber(const T value) {
    return PushNumber(static_cast<double>(value));
  }
  LuaCall& PushBoolean(bool value);
  LuaCall& PushString(std::string_view value);
  LuaCall& PushStringArgument(int index);
  LuaCall& PushNil();
  [[nodiscard]] int ResultCount() const noexcept { return result_count_; }

  [[nodiscard]] int PushNumberArrayTable(
      std::span<const std::uint32_t> values);
  [[nodiscard]] int PushBooleanSetTable(
      std::span<const std::uint32_t> values);
  [[nodiscard]] std::string ReadGlobalString(std::string_view key);

  [[nodiscard]] lua_State* UnsafeCompatibilityState() const noexcept {
    return state_;
  }

 private:
  lua_State* state_;
  int result_count_{0};
};

}
