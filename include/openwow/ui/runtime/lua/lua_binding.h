#pragma once

#include "openwow/ui/runtime/lua/lua_call.h"
#include "openwow/ui/runtime/lua/lua_vm.h"

#include <array>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

extern "C" {
#include <lua.hpp>
}

namespace openwow::ui::lua {

enum class BindingScope : std::uint8_t { kShared, kGlue, kWorld };
enum class BindingKind : std::uint8_t {
  kGlobalFunction,
  kConstant,
  kWidgetType,
  kWidgetMethod,
};

struct BindingDestination final {
  BindingKind kind{BindingKind::kGlobalFunction};
  std::optional<std::string> table_name;
  bool consume_after_create{false};

  [[nodiscard]] static BindingDestination Global() { return {}; }
  [[nodiscard]] static BindingDestination WidgetType(
      std::string type_name, bool consume_after_create = false) {
    return {BindingKind::kWidgetType, std::move(type_name),
            consume_after_create};
  }
  [[nodiscard]] static BindingDestination WidgetMethod(
      std::string owner_type) {
    return {BindingKind::kWidgetMethod, std::move(owner_type), false};
  }
};
enum class CollisionPolicy : std::uint8_t {
  kReject,
  kReplaceExisting,
  kKeepExisting,
};

struct BindingDescriptor final {
  std::string public_name;
  BindingScope scope{BindingScope::kShared};
  std::string owner;
  BindingKind kind{BindingKind::kGlobalFunction};
  CollisionPolicy collision{CollisionPolicy::kReject};
  std::optional<std::string> destination_table;
  bool consume_after_create{false};
  lua_CFunction trampoline{nullptr};
  bool installed{false};
};

enum class IntegralConversion : std::uint8_t {
  kUnspecified,
  kExact,
  kTruncate,
};

struct ConversionPolicy final {
  IntegralConversion integral{IntegralConversion::kUnspecified};
  bool accept_numeric_strings{false};
  bool allow_non_finite{false};
  bool ignore_extra_arguments{false};
  bool accept_truthy{false};
  bool accept_number_strings{false};

  constexpr bool operator==(const ConversionPolicy&) const = default;
};

inline constexpr ConversionPolicy kStrictConversion{
    IntegralConversion::kExact, false, false, false};
inline constexpr ConversionPolicy kWowTruthy{
    IntegralConversion::kExact, false, false, false, true, false};
inline constexpr ConversionPolicy kWowString{
    IntegralConversion::kExact, false, false, false, false, true};
inline constexpr ConversionPolicy kWowNumber{
    IntegralConversion::kExact, true, false, false};
inline constexpr ConversionPolicy kLuaExactInteger{
    IntegralConversion::kExact, true, false, false};
inline constexpr ConversionPolicy kLuaTruncatingInteger{
    IntegralConversion::kTruncate, true, false, false};

class LuaStackRestore final {
 public:
  explicit LuaStackRestore(lua_State* state) noexcept;
  ~LuaStackRestore() noexcept;

  void Dismiss() noexcept { state_ = nullptr; }
  void Restore() noexcept;

  LuaStackRestore(const LuaStackRestore&) = delete;
  LuaStackRestore& operator=(const LuaStackRestore&) = delete;

 private:
  lua_State* state_;
  int top_;
};

[[nodiscard]] int AbsoluteIndex(lua_State* state, int index) noexcept;

[[nodiscard]] bool CheckBoolean(lua_State* state, int argument);
[[nodiscard]] lua_Integer CheckInteger(lua_State* state, int argument);
[[nodiscard]] lua_Number CheckNumber(lua_State* state, int argument);
[[nodiscard]] std::string_view CheckString(lua_State* state, int argument);
[[nodiscard]] std::optional<bool> CheckOptionalBoolean(lua_State* state,
                                                       int argument);
[[nodiscard]] std::optional<lua_Integer> CheckOptionalInteger(lua_State* state,
                                                              int argument);
[[nodiscard]] std::optional<lua_Number> CheckOptionalNumber(lua_State* state,
                                                            int argument);
[[nodiscard]] std::optional<std::string_view> CheckOptionalString(
    lua_State* state, int argument);

class RawLuaState final {
 public:
  explicit RawLuaState(lua_State* state) noexcept : state_(state) {}
  [[nodiscard]] lua_State* get() const noexcept { return state_; }

 private:
  lua_State* state_;
};

struct RawLuaResults final {
  int count{0};
};

struct LuaUsageError final {
  std::string message;
};

template <typename Value>
class LuaVariableReturns final {
 public:
  explicit LuaVariableReturns(std::vector<Value> values)
      : values_(std::move(values)) {}

  [[nodiscard]] const std::vector<Value>& values() const noexcept {
    return values_;
  }

 private:
  std::vector<Value> values_;
};

template <typename T>
struct LuaRegistryContext {
  static constexpr std::string_view key{};
};

struct LuaFrameMethodReceiverPolicy {
  static void* Identity(lua_State* state, int index) noexcept {
    if (state == nullptr || lua_istable(state, index) == 0) {
      return nullptr;
    }
    lua_rawgeti(state, index, 0);
    void* identity = lua_type(state, -1) == LUA_TLIGHTUSERDATA
                         ? lua_touserdata(state, -1)
                         : nullptr;
    lua_pop(state, 1);
    return identity;
  }
};

template <typename T>
struct LuaMethodReceiverPolicy {
  static constexpr bool enabled = false;
};

template <typename T>
inline constexpr unsigned char kLuaMethodReceiverTypeToken = 0;

inline int Push(lua_State*, const NoLuaResults&) { return 0; }
inline int Push(lua_State*, const LuaReturns<>&) { return 0; }
inline int Push(lua_State* state, const LuaNil&) {
  lua_pushnil(state);
  return 1;
}
inline int Push(lua_State* state, const LuaBoolean value) {
  lua_pushboolean(state, value.value ? 1 : 0);
  return 1;
}
inline int Push(lua_State* state, const LuaTruthy value) {
  if (value.value) {
    lua_pushnumber(state, 1.0);
  } else {
    lua_pushnil(state);
  }
  return 1;
}
inline int Push(lua_State* state, const bool value) {
  lua_pushboolean(state, value ? 1 : 0);
  return 1;
}
inline int Push(lua_State* state, const char* value) {
  lua_pushstring(state, value != nullptr ? value : "");
  return 1;
}
inline int Push(lua_State* state, const std::string& value) {
  lua_pushlstring(state, value.data(), value.size());
  return 1;
}
inline int Push(lua_State* state, const std::string_view value) {
  lua_pushlstring(state, value.empty() ? "" : value.data(), value.size());
  return 1;
}

template <std::integral T>
  requires(!std::same_as<T, bool>)
int Push(lua_State* state, const T value) {
  lua_pushnumber(state, static_cast<lua_Number>(value));
  return 1;
}

template <std::floating_point T>
int Push(lua_State* state, const T value) {
  lua_pushnumber(state, static_cast<lua_Number>(value));
  return 1;
}

template <typename T>
  requires std::is_enum_v<T>
int Push(lua_State* state, const T value) {
  return Push(state, static_cast<std::underlying_type_t<T>>(value));
}

template <typename T>
int Push(lua_State* state, const std::optional<T>& value) {
  return value ? Push(state, *value) : Push(state, LuaNil{});
}

template <typename... T>
int Push(lua_State* state, const std::variant<T...>& value) {
  return std::visit([state](const auto& current) { return Push(state, current); },
                    value);
}

template <typename T>
int Push(lua_State* state, const LuaVariableReturns<T>& values) {

  const std::size_t result_count = values.values().size();
  if (result_count >
          static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
      lua_checkstack(state, static_cast<int>(result_count)) == 0) {
    return luaL_error(state, "Lua stack capacity exceeded");
  }

  int pushed = 0;
  for (const auto& value : values.values()) {
    pushed += Push(state, value);
  }
  return pushed;
}

template <typename Product>
  requires requires { typename std::tuple_size<std::remove_cvref_t<Product>>::type; }
int PushProduct(lua_State* state, const Product& values) {
  return std::apply(
      [state](const auto&... value) {
        int pushed = 0;
        ((pushed += Push(state, value)), ...);
        return pushed;
      },
      values);
}

template <typename... T>
int Push(lua_State* state, const LuaReturns<T...>& values) {
  return PushProduct(state, values.values());
}

template <typename Product>
  requires requires { typename std::tuple_size<std::remove_cvref_t<Product>>::type; }
int Push(lua_State* state, const Product& values) {
  return PushProduct(state, values);
}

void PushTableField(lua_State* state, int table_index, std::string_view field);

template <typename T>
  requires LuaMethodReceiverPolicy<std::remove_cv_t<T>>::enabled
bool PublishMethodReceiver(lua_State* state, int receiver_index,
                           int owner_index, T* value) {
  using Value = std::remove_cv_t<T>;
  if (state == nullptr || value == nullptr ||
      lua_istable(state, receiver_index) == 0) {
    return false;
  }
  LuaStackRestore restore(state);
  receiver_index = AbsoluteIndex(state, receiver_index);
  owner_index = AbsoluteIndex(state, owner_index);
  void* identity = LuaMethodReceiverPolicy<Value>::Identity(
      state, receiver_index);
  if (identity == nullptr) {
    return false;
  }

  lua_pushlightuserdata(state,
                        const_cast<unsigned char*>(
                            &kLuaMethodReceiverTypeToken<Value>));
  lua_rawget(state, LUA_REGISTRYINDEX);
  if (lua_istable(state, -1) == 0) {
    lua_pop(state, 1);
    lua_newtable(state);
    lua_newtable(state);
    lua_pushliteral(state, "v");
    lua_setfield(state, -2, "__mode");
    lua_setmetatable(state, -2);
    lua_pushlightuserdata(state,
                          const_cast<unsigned char*>(
                              &kLuaMethodReceiverTypeToken<Value>));
    lua_pushvalue(state, -2);
    lua_rawset(state, LUA_REGISTRYINDEX);
  }
  const int receivers = AbsoluteIndex(state, -1);

  lua_newtable(state);
  const int anchor = AbsoluteIndex(state, -1);
  lua_pushlightuserdata(state, const_cast<Value*>(value));
  lua_rawseti(state, anchor, 1);
  lua_pushvalue(state, owner_index);
  lua_rawseti(state, anchor, 2);

  lua_pushlightuserdata(state, identity);
  lua_pushvalue(state, anchor);
  lua_rawset(state, receivers);
  lua_pushlightuserdata(state,
                        const_cast<unsigned char*>(
                            &kLuaMethodReceiverTypeToken<Value>));
  lua_pushvalue(state, anchor);
  lua_rawset(state, receiver_index);
  return true;
}

template <typename T>
[[nodiscard]] T* RegistryContext(lua_State* state,
                                 const std::string_view key) noexcept {
  if (state == nullptr || key.empty()) {
    return nullptr;
  }
  LuaStackRestore restore(state);
  PushTableField(state, LUA_REGISTRYINDEX, key);
  return static_cast<T*>(lua_touserdata(state, -1));
}

template <typename T, ConversionPolicy Policy, typename = void>
struct LuaConverter;

template <std::floating_point T, ConversionPolicy Policy>
struct LuaConverter<T, Policy> {
  using Storage = T;
  static bool Valid(lua_State* state, int index) noexcept {
    if (Policy.accept_numeric_strings ? lua_isnumber(state, index) == 0
                                      : lua_type(state, index) != LUA_TNUMBER) {
      return false;
    }
    const T value = static_cast<T>(lua_tonumber(state, index));
    return Policy.allow_non_finite || std::isfinite(value);
  }
  static Storage Read(lua_State* state, int index) noexcept {
    return static_cast<T>(lua_tonumber(state, index));
  }
  static T Argument(const Storage value) noexcept { return value; }
};

template <ConversionPolicy Policy>
struct LuaConverter<bool, Policy> {
  using Storage = bool;
  static bool Valid(lua_State* state, int index) noexcept {
    return Policy.accept_truthy || lua_type(state, index) == LUA_TBOOLEAN;
  }
  static Storage Read(lua_State* state, int index) noexcept {
    return lua_toboolean(state, index) != 0;
  }
  static bool Argument(const Storage& value) noexcept { return value; }
};

template <ConversionPolicy Policy>
struct LuaConverter<LuaBoolean, Policy> : LuaConverter<bool, Policy> {
  static LuaBoolean Argument(const bool value) noexcept { return {value}; }
};

template <ConversionPolicy Policy>
struct LuaConverter<LuaTruthy, Policy> {
  using Storage = bool;
  static bool Valid(lua_State*, int) noexcept { return true; }
  static Storage Read(lua_State* state, int index) noexcept {
    return lua_toboolean(state, index) != 0;
  }
  static LuaTruthy Argument(const Storage value) noexcept { return {value}; }
};

template <ConversionPolicy Policy>
struct LuaConverter<std::string, Policy> {
  using Storage = std::string;
  static bool Valid(lua_State* state, int index) noexcept {
    return Policy.accept_number_strings ? lua_isstring(state, index) != 0
                                        : lua_type(state, index) == LUA_TSTRING;
  }
  static Storage Read(lua_State* state, int index) {
    std::size_t size = 0;
    const char* value = lua_tolstring(state, index, &size);
    return std::string(value, size);
  }
  static const std::string& Argument(const Storage& value) noexcept {
    return value;
  }
};

template <ConversionPolicy Policy>
struct LuaConverter<std::string_view, Policy> {
  using Storage = std::string_view;
  static bool Valid(lua_State* state, int index) noexcept {
    return Policy.accept_number_strings ? lua_isstring(state, index) != 0
                                        : lua_type(state, index) == LUA_TSTRING;
  }
  static Storage Read(lua_State* state, int index) noexcept {
    std::size_t size = 0;
    const char* value = lua_tolstring(state, index, &size);
    return {value, size};
  }
  static std::string_view Argument(const Storage value) noexcept {
    return value;
  }
};

template <std::integral T, ConversionPolicy Policy>
  requires(!std::same_as<T, bool>)
struct LuaConverter<T, Policy> {
  static_assert(Policy.integral != IntegralConversion::kUnspecified,
                "integral Lua arguments require an explicit conversion policy");
  using Storage = T;
  static bool Valid(lua_State* state, int index) noexcept {
    if (Policy.accept_numeric_strings ? lua_isnumber(state, index) == 0
                                      : lua_type(state, index) != LUA_TNUMBER) {
      return false;
    }
    const double value = lua_tonumber(state, index);
    if (!std::isfinite(value)) {
      return false;
    }
    const double truncated = std::trunc(value);
    const double upper_exclusive =
        std::ldexp(1.0, std::numeric_limits<T>::digits);
    const double lower_inclusive = std::is_signed_v<T>
                                       ? -upper_exclusive
                                       : 0.0;
    return (Policy.integral != IntegralConversion::kExact ||
            truncated == value) &&
           truncated >= lower_inclusive && truncated < upper_exclusive;
  }
  static Storage Read(lua_State* state, int index) noexcept {

    return static_cast<T>(std::trunc(lua_tonumber(state, index)));
  }
  static T Argument(const Storage value) noexcept { return value; }
};

template <typename T, ConversionPolicy Policy>
  requires std::is_enum_v<T>
struct LuaConverter<T, Policy> {
  using Underlying = std::underlying_type_t<T>;
  using Base = LuaConverter<Underlying, Policy>;
  using Storage = typename Base::Storage;
  static bool Valid(lua_State* state, int index) noexcept {
    return Base::Valid(state, index);
  }
  static Storage Read(lua_State* state, int index) noexcept {
    return Base::Read(state, index);
  }
  static T Argument(const Storage value) noexcept {
    return static_cast<T>(value);
  }
};

template <typename T, ConversionPolicy Policy>
struct LuaConverter<std::optional<T>, Policy> {
  using ValueConverter = LuaConverter<T, Policy>;
  using Storage = std::optional<typename ValueConverter::Storage>;
  static bool Valid(lua_State* state, int index) noexcept {
    return index > lua_gettop(state) || lua_isnil(state, index) != 0 ||
           ValueConverter::Valid(state, index);
  }
  static Storage Read(lua_State* state, int index) {
    if (index > lua_gettop(state) || lua_isnil(state, index) != 0) {
      return std::nullopt;
    }
    return ValueConverter::Read(state, index);
  }
  static std::optional<T> Argument(const Storage& value) {
    if (!value) {
      return std::nullopt;
    }
    return ValueConverter::Argument(*value);
  }
};

namespace detail {

[[nodiscard]] void* ActiveBindingAdapter(lua_State* state) noexcept;
[[nodiscard]] const char* ActiveBindingName(lua_State* state) noexcept;
[[nodiscard]] void* GlobalBindingAdapter(
    lua_State* state, std::string_view global_name) noexcept;

template <typename>
struct MemberTraits;

template <typename Class, typename Result, typename... Arguments>
struct MemberTraits<Result (Class::*)(Arguments...)> {
  using ClassType = Class;
  using ResultType = Result;
  using ArgumentsTuple = std::tuple<Arguments...>;
  static constexpr bool is_const = false;
};

template <typename Class, typename Result, typename... Arguments>
struct MemberTraits<Result (Class::*)(Arguments...) const> {
  using ClassType = Class;
  using ResultType = Result;
  using ArgumentsTuple = std::tuple<Arguments...>;
  static constexpr bool is_const = true;
};

template <typename Class, typename Result, typename... Arguments>
struct MemberTraits<Result (Class::*)(Arguments...) noexcept>
    : MemberTraits<Result (Class::*)(Arguments...)> {};

template <typename Class, typename Result, typename... Arguments>
struct MemberTraits<Result (Class::*)(Arguments...) const noexcept>
    : MemberTraits<Result (Class::*)(Arguments...) const> {};

template <typename>
struct CallableTraits;

template <typename Result, typename... Arguments>
struct CallableTraits<Result (*)(Arguments...)> {
  using ResultType = Result;
  using ArgumentsTuple = std::tuple<Arguments...>;
  static constexpr bool is_member = false;
};

template <typename Result, typename... Arguments>
struct CallableTraits<Result (*)(Arguments...) noexcept>
    : CallableTraits<Result (*)(Arguments...)> {};

template <typename Class, typename Result, typename... Arguments>
struct CallableTraits<Result (Class::*)(Arguments...)>
    : MemberTraits<Result (Class::*)(Arguments...)> {
  static constexpr bool is_member = true;
};

template <typename Class, typename Result, typename... Arguments>
struct CallableTraits<Result (Class::*)(Arguments...) const>
    : MemberTraits<Result (Class::*)(Arguments...) const> {
  static constexpr bool is_member = true;
};

template <typename Class, typename Result, typename... Arguments>
struct CallableTraits<Result (Class::*)(Arguments...) noexcept>
    : CallableTraits<Result (Class::*)(Arguments...)> {};

template <typename Class, typename Result, typename... Arguments>
struct CallableTraits<Result (Class::*)(Arguments...) const noexcept>
    : CallableTraits<Result (Class::*)(Arguments...) const> {};

template <typename Callable>
struct CallableTraits : CallableTraits<decltype(&Callable::operator())> {
  static constexpr bool is_member = false;
};

template <typename T>
struct IsLuaReturns : std::false_type {};

template <typename... T>
struct IsLuaReturns<LuaReturns<T...>> : std::true_type {};

template <typename T>
struct IsOptional : std::false_type {};

template <typename T>
struct IsOptional<std::optional<T>> : std::true_type {};

template <typename T>
struct IsVariant : std::false_type {};

template <typename... T>
struct IsVariant<std::variant<T...>> : std::true_type {};

template <std::size_t Index, ConversionPolicy... Policies>
consteval ConversionPolicy ParameterPolicy() {
  if constexpr (sizeof...(Policies) == 0) {
    return kStrictConversion;
  } else if constexpr (sizeof...(Policies) == 1) {
    return std::array{Policies...}[0];
  } else {
    return std::array{Policies...}[Index];
  }
}

template <ConversionPolicy... Policies>
consteval bool IgnoreExtraArguments() {
  if constexpr (sizeof...(Policies) == 0) {
    return false;
  } else {
    return std::array{Policies...}[0].ignore_extra_arguments;
  }
}

template <typename T>
using ContextValue = std::remove_cv_t<std::remove_pointer_t<
    std::remove_reference_t<T>>>;

template <typename T>
inline constexpr bool IsRawState =
    std::same_as<std::remove_cvref_t<T>, RawLuaState>;

template <typename T>
inline constexpr bool IsInjectedContext =
    !IsRawState<T> &&
    (std::is_pointer_v<std::remove_reference_t<T>> ||
     std::is_reference_v<T>) &&
    !LuaRegistryContext<ContextValue<T>>::key.empty();

template <typename T>
inline constexpr bool IsMethodReceiver =
    !IsRawState<T> &&
    (std::is_pointer_v<std::remove_reference_t<T>> ||
     std::is_reference_v<T>) &&
    LuaMethodReceiverPolicy<ContextValue<T>>::enabled;

template <typename T>
inline constexpr bool ConsumesLuaArgument =
    !IsRawState<T> && !IsInjectedContext<T> && !IsMethodReceiver<T>;

template <typename Tuple, std::size_t... Index>
consteval bool HasRawState(std::index_sequence<Index...>) {
  return (IsRawState<std::tuple_element_t<Index, Tuple>> || ... || false);
}

template <typename Argument, ConversionPolicy Policy,
           bool Raw = IsRawState<Argument>,
           bool Context = IsInjectedContext<Argument>,
           bool Receiver = IsMethodReceiver<Argument>>
struct BoundArgument;

template <typename Argument, ConversionPolicy Policy>
struct BoundArgument<Argument, Policy, false, false, false> {
  using Value = std::remove_cvref_t<Argument>;
  using Converter = LuaConverter<Value, Policy>;
  using Storage = typename Converter::Storage;
  static constexpr bool consumes = true;
  static constexpr bool optional = IsOptional<Value>::value;

  static bool Valid(lua_State* state, const int index) noexcept {
    return Converter::Valid(state, index);
  }
  static Storage Read(lua_State* state, const int index) {
    return Converter::Read(state, index);
  }
  static decltype(auto) ArgumentValue(Storage& value) {
    return Converter::Argument(value);
  }
};

template <typename Argument, ConversionPolicy Policy>
struct BoundArgument<Argument, Policy, true, false, false> {
  using Storage = RawLuaState;
  static constexpr bool consumes = false;
  static constexpr bool optional = false;

  static bool Valid(lua_State*, int) noexcept { return true; }
  static Storage Read(lua_State* state, int) noexcept {
    return RawLuaState(state);
  }
  static RawLuaState ArgumentValue(const Storage value) noexcept {
    return value;
  }
};

template <typename Argument, ConversionPolicy Policy>
struct BoundArgument<Argument, Policy, false, true, false> {
  using Value = ContextValue<Argument>;
  using Storage = Value*;
  static constexpr bool consumes = false;
  static constexpr bool optional = false;

  static bool Valid(lua_State* state, int) noexcept {
    return std::is_pointer_v<std::remove_reference_t<Argument>> ||
           RegistryContext<Value>(state, LuaRegistryContext<Value>::key) !=
               nullptr;
  }
  static Storage Read(lua_State* state, int) noexcept {
    return RegistryContext<Value>(state, LuaRegistryContext<Value>::key);
  }
  static decltype(auto) ArgumentValue(const Storage value) noexcept {
    if constexpr (std::is_pointer_v<std::remove_reference_t<Argument>>) {
      return value;
    } else {
      return static_cast<Argument>(*value);
    }
  }
};

template <typename Argument, ConversionPolicy Policy>
struct BoundArgument<Argument, Policy, false, false, true> {
  using Value = ContextValue<Argument>;
  using Storage = Value*;
  static constexpr bool consumes = false;
  static constexpr bool optional = false;

  static Storage Resolve(lua_State* state, int index) noexcept {
    if (state == nullptr || lua_istable(state, index) == 0) {
      return nullptr;
    }
    LuaStackRestore restore(state);
    void* identity = LuaMethodReceiverPolicy<Value>::Identity(state, index);
    if (identity == nullptr) {
      return nullptr;
    }
    lua_pushlightuserdata(
        state,
        const_cast<unsigned char*>(&kLuaMethodReceiverTypeToken<Value>));
    lua_rawget(state, LUA_REGISTRYINDEX);
    if (lua_istable(state, -1) == 0) {
      return nullptr;
    }
    lua_pushlightuserdata(state, identity);
    lua_rawget(state, -2);
    if (lua_istable(state, -1) == 0) {
      return nullptr;
    }
    lua_rawgeti(state, -1, 1);
    return static_cast<Value*>(lua_touserdata(state, -1));
  }
  static bool Valid(lua_State* state, int index) noexcept {
    return Resolve(state, index) != nullptr;
  }
  static Storage Read(lua_State* state, int index) noexcept {
    return Resolve(state, index);
  }
  static decltype(auto) ArgumentValue(const Storage value) noexcept {
    if constexpr (std::is_pointer_v<std::remove_reference_t<Argument>>) {
      return value;
    } else {
      return static_cast<Argument>(*value);
    }
  }
};

template <typename Tuple, std::size_t... Index>
consteval std::size_t MethodReceiverCount(std::index_sequence<Index...>) {
  return (static_cast<std::size_t>(
              IsMethodReceiver<std::tuple_element_t<Index, Tuple>>) + ... +
          0u);
}

template <typename Tuple, std::size_t End, std::size_t... Index>
consteval std::size_t PositionalIndexImpl(std::index_sequence<Index...>) {
  return 1u + MethodReceiverCount<Tuple>(
                  std::make_index_sequence<std::tuple_size_v<Tuple>>{}) +
         (static_cast<std::size_t>(ConsumesLuaArgument<
              std::tuple_element_t<Index, Tuple>>) + ... + 0u);
}

template <typename Tuple, std::size_t Index>
inline constexpr int PositionalIndex = static_cast<int>(
    PositionalIndexImpl<Tuple, Index>(std::make_index_sequence<Index>{}));

template <typename Tuple, std::size_t Index>
inline constexpr int BindingArgumentIndex =
    IsMethodReceiver<std::tuple_element_t<Index, Tuple>>
        ? 1
        : PositionalIndex<Tuple, Index>;

template <typename Tuple, std::size_t... Index>
consteval std::size_t RequiredArgumentCount(std::index_sequence<Index...>) {
  return (static_cast<std::size_t>(
              ConsumesLuaArgument<std::tuple_element_t<Index, Tuple>> &&
              !IsOptional<std::remove_cvref_t<
                  std::tuple_element_t<Index, Tuple>>>::value) + ... + 0u);
}

template <typename Tuple, std::size_t... Index>
consteval std::size_t PositionalArgumentCount(std::index_sequence<Index...>) {
  return (static_cast<std::size_t>(ConsumesLuaArgument<
              std::tuple_element_t<Index, Tuple>>) + ... + 0u);
}

inline const std::string* UsageErrorMessage(const LuaUsageError& error) {
  return &error.message;
}

template <typename Result>
const std::string* UsageErrorMessage(const Result& result) {
  if constexpr (IsVariant<Result>::value) {
    return std::visit(
        [](const auto& value) { return UsageErrorMessage(value); }, result);
  } else {
    return nullptr;
  }
}

template <typename Result>
int PushResult(lua_State* state, const Result& result) {
  if constexpr (std::same_as<Result, LuaUsageError>) {
    return 0;
  } else if constexpr (IsVariant<Result>::value) {
    return std::visit(
        [state](const auto& value) { return PushResult(state, value); }, result);
  } else {
    return Push(state, result);
  }
}

template <typename Result>
int ProtectedResultPusher(lua_State* state) {
  const auto* result = static_cast<const Result*>(lua_touserdata(state, 1));
  int pushed = 0;
  bool failed = false;
  try {
    pushed = PushResult(state, *result);
  } catch (...) {
    failed = true;
  }
  if (failed) {
    return luaL_error(state, "result emission failed");
  }
  return pushed;
}

template <typename Tuple, std::size_t Index = 0,
          ConversionPolicy... Policies>
int InvalidArgument(lua_State* state) {
  if constexpr (Index == std::tuple_size_v<Tuple>) {
    return 0;
  } else {
    using Argument = std::tuple_element_t<Index, Tuple>;
    constexpr auto policy = ParameterPolicy<Index, Policies...>();
    using Codec = BoundArgument<Argument, policy>;
    constexpr int position = BindingArgumentIndex<Tuple, Index>;
    if (!Codec::Valid(state, position)) {
      return Codec::consumes ? position : static_cast<int>(Index + 1);
    }
    return InvalidArgument<Tuple, Index + 1, Policies...>(state);
  }
}

template <typename Tuple, ConversionPolicy... Policies, std::size_t... Index>
auto DecodeArguments(lua_State* state, std::index_sequence<Index...>) {
  return std::tuple<typename BoundArgument<
      std::tuple_element_t<Index, Tuple>,
      ParameterPolicy<Index, Policies...>()>::Storage...>{
      BoundArgument<std::tuple_element_t<Index, Tuple>,
                     ParameterPolicy<Index, Policies...>()>::Read(
          state, BindingArgumentIndex<Tuple, Index>)...};
}

template <auto Callable, ConversionPolicy... Policies, typename Storage,
            std::size_t... Index>
decltype(auto) InvokeDecoded(lua_State* state, Storage& storage,
                             std::index_sequence<Index...>) {
  using Traits = CallableTraits<std::remove_cvref_t<decltype(Callable)>>;
  using Arguments = typename Traits::ArgumentsTuple;
  if constexpr (Traits::is_member) {
    using Class = typename Traits::ClassType;
    auto* adapter = static_cast<Class*>(ActiveBindingAdapter(state));
    return std::invoke(
        Callable, *adapter,
        BoundArgument<std::tuple_element_t<Index, Arguments>,
                      ParameterPolicy<Index, Policies...>()>::ArgumentValue(
            std::get<Index>(storage))...);
  } else {
    return std::invoke(
        Callable,
        BoundArgument<std::tuple_element_t<Index, Arguments>,
                      ParameterPolicy<Index, Policies...>()>::ArgumentValue(
            std::get<Index>(storage))...);
  }
}

inline int BindingError(lua_State* state, LuaStackRestore& restore,
                        const char* function, const int argument,
                        const char* reason) {
  restore.Restore();
  if (argument > 0) {
    lua_pushfstring(state, "%s: argument %d %s", function, argument, reason);
  } else {
    lua_pushfstring(state, "%s: %s", function, reason);
  }
  return lua_error(state);
}

template <auto Callable, ConversionPolicy... Policies>
int DirectTrampoline(lua_State* state) {
  using Traits = CallableTraits<std::remove_cvref_t<decltype(Callable)>>;
  using Result = typename Traits::ResultType;
  using Arguments = typename Traits::ArgumentsTuple;
  constexpr std::size_t count = std::tuple_size_v<Arguments>;
  static_assert(sizeof...(Policies) <= 1 || sizeof...(Policies) == count,
                "provide one conversion policy or one per parameter");
  constexpr std::size_t positional = PositionalArgumentCount<Arguments>(
      std::make_index_sequence<count>{});
  constexpr std::size_t receivers = MethodReceiverCount<Arguments>(
      std::make_index_sequence<count>{});
  static_assert(receivers <= 1, "bindings support one method receiver");
  constexpr std::size_t required = RequiredArgumentCount<Arguments>(
      std::make_index_sequence<count>{});
  const char* function = ActiveBindingName(state);
  LuaStackRestore restore(state);

  if constexpr (Traits::is_member) {
    if (ActiveBindingAdapter(state) == nullptr) {
      return BindingError(state, restore, function, 0, "binding retired");
    }
  }
  const int argument_count = lua_gettop(state);
  if (argument_count < static_cast<int>(required + receivers)) {
    return BindingError(state, restore, function, argument_count + 1,
                        "is required");
  }
  if (!IgnoreExtraArguments<Policies...>() &&
      argument_count > static_cast<int>(positional + receivers)) {
    return BindingError(state, restore, function,
                         static_cast<int>(positional + receivers + 1),
                         "is unexpected");
  }
  const int invalid = InvalidArgument<Arguments, 0, Policies...>(state);
  if (invalid != 0) {
    return BindingError(state, restore, function, invalid,
                        "has invalid type or range");
  }
  if (lua_checkstack(state, 3) == 0) {
    return BindingError(state, restore, function, 0,
                        "Lua stack capacity exceeded");
  }

  bool invocation_failed = false;
  int push_status = 0;
  int pushed = 0;
  if constexpr (std::is_void_v<Result>) {
    try {
      auto decoded = DecodeArguments<Arguments, Policies...>(
          state, std::make_index_sequence<count>{});
      InvokeDecoded<Callable, Policies...>(
          state, decoded, std::make_index_sequence<count>{});
    } catch (...) {
      invocation_failed = true;
    }
  } else if constexpr (std::same_as<Result, RawLuaResults>) {
    static_assert(HasRawState<Arguments>(std::make_index_sequence<count>{}),
                  "RawLuaResults requires RawLuaState argument");
    try {
      auto decoded = DecodeArguments<Arguments, Policies...>(
          state, std::make_index_sequence<count>{});
      pushed = InvokeDecoded<Callable, Policies...>(
                   state, decoded, std::make_index_sequence<count>{})
                   .count;
      if (pushed < 0 || pushed > lua_gettop(state)) {
        invocation_failed = true;
      }
    } catch (...) {
      invocation_failed = true;
    }
  } else {
    std::optional<std::string> usage_error;
    {
      std::optional<Result> result;
      try {
        auto decoded = DecodeArguments<Arguments, Policies...>(
            state, std::make_index_sequence<count>{});
        result.emplace(InvokeDecoded<Callable, Policies...>(
            state, decoded, std::make_index_sequence<count>{}));
      } catch (...) {
        invocation_failed = true;
      }
      if (!invocation_failed) {
        if (const auto* message = UsageErrorMessage(*result)) {
          usage_error = *message;
        } else {
          lua_pushcfunction(state, &ProtectedResultPusher<Result>);
          lua_pushlightuserdata(state, &*result);
          push_status = lua_pcall(state, 1, LUA_MULTRET, 0);
          if (push_status == 0) {
            pushed = lua_gettop(state) - argument_count;
          }
        }
      }
    }
    if (usage_error) {
      restore.Restore();
      lua_pushlstring(state, usage_error->data(), usage_error->size());
      usage_error.reset();
      return lua_error(state);
    }
  }

  if (invocation_failed) {
    return BindingError(state, restore, function, 0, "handler failed");
  }
  if (push_status != 0) {
    return BindingError(state, restore, function, 0,
                        "result emission failed");
  }
  restore.Dismiss();
  return pushed;
}

template <auto Method>
int CustomTrampoline(lua_State* state) {
  using Traits = MemberTraits<decltype(Method)>;
  using Class = typename Traits::ClassType;
  using Result = typename Traits::ResultType;
  using Arguments = typename Traits::ArgumentsTuple;
  static_assert(std::tuple_size_v<Arguments> == 1 &&
                    std::same_as<std::remove_cvref_t<std::tuple_element_t<0, Arguments>>,
                                 LuaCall>,
                "custom Lua methods must accept exactly LuaCall&");

  auto* adapter = static_cast<Class*>(ActiveBindingAdapter(state));
  if (adapter == nullptr) {
    return luaL_error(state, "internal Lua binding error");
  }
  LuaCall call(state);
  int pushed = 0;
  bool failed = false;
  try {
    if constexpr (std::same_as<Result, int>) {
      pushed = std::invoke(Method, *adapter, call);
    } else if constexpr (std::same_as<Result, NoLuaResults> ||
                         std::same_as<Result, LuaReturns<>>) {
      (void)std::invoke(Method, *adapter, call);
    } else {
      static_assert(
          std::same_as<Result, int>,
          "custom Lua methods return int, NoLuaResults, or LuaReturns<>");
    }
  } catch (...) {
    failed = true;
  }
  if (failed) {
    return luaL_error(state, "internal Lua binding error");
  }
  return pushed;
}

}

template <auto Method, ConversionPolicy... Policies>
struct DirectBinding final {
  std::string_view name;
  CollisionPolicy collision{CollisionPolicy::kReject};
  static constexpr lua_CFunction trampoline =
      &detail::DirectTrampoline<Method, Policies...>;
  static constexpr lua_CFunction handler = trampoline;
  using Result = typename detail::CallableTraits<
      std::remove_cvref_t<decltype(Method)>>::ResultType;
  static constexpr lua_CFunction result_pusher = [] {
    if constexpr (std::is_void_v<Result> ||
                  std::same_as<Result, RawLuaResults>) {
      return static_cast<lua_CFunction>(nullptr);
    } else {
      return &detail::ProtectedResultPusher<Result>;
    }
  }();
};

template <auto Method>
struct CustomBinding final {
  std::string_view name;
  CollisionPolicy collision{CollisionPolicy::kReject};
  static constexpr lua_CFunction trampoline = &detail::CustomTrampoline<Method>;
};

using BindingConstantValue = std::variant<double, bool, std::string>;

struct ConstantBinding final {
  std::string_view name;
  BindingConstantValue value;
  CollisionPolicy collision{CollisionPolicy::kReject};
};

struct RawFunctionBinding final {
  std::string_view name;
  lua_CFunction handler{nullptr};
  void* adapter{nullptr};
  CollisionPolicy collision{CollisionPolicy::kReject};
};

inline RawFunctionBinding raw_function(
    std::string_view name, lua_CFunction handler,
    CollisionPolicy collision = CollisionPolicy::kReject,
    void* adapter = nullptr) {
  return {name, handler, adapter, collision};
}

template <auto Method, ConversionPolicy... Policies>
constexpr auto bind(std::string_view name,
                     CollisionPolicy collision = CollisionPolicy::kReject) {
  return DirectBinding<Method, Policies...>{name, collision};
}

template <auto Method>
constexpr auto custom(std::string_view name,
                      CollisionPolicy collision = CollisionPolicy::kReject) {
  return CustomBinding<Method>{name, collision};
}

inline ConstantBinding constant(
    std::string_view name, BindingConstantValue value,
    CollisionPolicy collision = CollisionPolicy::kReject) {
  return {name, std::move(value), collision};
}

template <typename... Bindings>
constexpr auto bindings(Bindings... values) {
  return std::tuple<Bindings...>{std::move(values)...};
}

class BindingSet final {
 public:
  explicit BindingSet(
      std::string owner, BindingScope scope = BindingScope::kShared,
      BindingDestination destination = BindingDestination::Global());
  ~BindingSet();

  BindingSet(const BindingSet&) = delete;
  BindingSet& operator=(const BindingSet&) = delete;
  BindingSet(BindingSet&&) noexcept;
  BindingSet& operator=(BindingSet&&) noexcept;

  template <typename Adapter, typename... Definitions>
  bool Install(LuaVm& vm, Adapter& adapter,
               const std::tuple<Definitions...>& definitions) {
    if (!CanInstallInto(vm)) {
      return false;
    }
    const bool can_publish = std::apply(
        [&](const auto&... definition) {
          const std::array<BindingCandidate, sizeof...(Definitions)> candidates{
              CandidateFor(definition)...};
          return CanPublishBatch(vm, candidates);
        },
        definitions);
    if (!can_publish) {
      return false;
    }
    bool succeeded = true;
    std::apply(
        [&](const auto&... definition) {
          ((succeeded = InstallOne(vm, adapter, definition) && succeeded), ...);
        },
        definitions);
    if (!succeeded) {
      Uninstall();
    }
    return succeeded;
  }

  bool InstallConstants(LuaVm& vm,
                        const std::vector<ConstantBinding>& constants);
  bool InstallRawFunctions(LuaVm& vm,
                           const std::vector<RawFunctionBinding>& functions);

  bool ClaimWidgetMethods(LuaVm& vm,
                          const std::vector<std::string_view>& methods);
  bool InstallWidgetType(LuaVm& vm);
  bool ConsumeWidgetType(LuaVm& vm);
  void Uninstall() noexcept;

  [[nodiscard]] const std::vector<BindingDescriptor>& descriptors() const {
    return descriptors_;
  }

 private:
  struct BindingCandidate final {
    std::string_view name;
    CollisionPolicy collision;
    BindingKind kind;
  };

  template <auto Method, ConversionPolicy... Policies>
  [[nodiscard]] BindingCandidate CandidateFor(
      const DirectBinding<Method, Policies...>& definition) const noexcept {
    return {definition.name, definition.collision,
            destination_.kind};
  }

  template <auto Method>
  [[nodiscard]] BindingCandidate CandidateFor(
      const CustomBinding<Method>& definition) const noexcept {
    return {definition.name, definition.collision,
            destination_.kind};
  }

  [[nodiscard]] BindingCandidate CandidateFor(
      const ConstantBinding& definition) const noexcept {
    return {definition.name, definition.collision, BindingKind::kConstant};
  }

  [[nodiscard]] BindingCandidate CandidateFor(
      const RawFunctionBinding& definition) const noexcept {
    return {definition.name, definition.collision, destination_.kind};
  }

  template <typename Adapter, auto Method, ConversionPolicy... Policies>
  bool InstallOne(LuaVm& vm, Adapter& adapter,
                   const DirectBinding<Method, Policies...>& definition) {
    using Traits = detail::CallableTraits<
        std::remove_cvref_t<decltype(Method)>>;
    if constexpr (Traits::is_member) {
      using Expected = typename Traits::ClassType;
      static_assert(std::same_as<std::remove_cvref_t<Adapter>, Expected>,
                    "binding adapter does not own the selected member method");
      return InstallFunction(vm, definition.name, definition.collision,
                             &adapter, definition.trampoline,
                             definition.result_pusher, nullptr);
    } else {
      return InstallFunction(vm, definition.name, definition.collision,
                             nullptr, definition.trampoline,
                             definition.result_pusher, nullptr);
    }
  }

  template <typename Adapter, auto Method>
  bool InstallOne(LuaVm& vm, Adapter& adapter,
                  const CustomBinding<Method>& definition) {
    using Expected = typename detail::MemberTraits<decltype(Method)>::ClassType;
    static_assert(std::same_as<std::remove_cvref_t<Adapter>, Expected>,
                  "binding adapter does not own the selected custom method");
    return InstallFunction(vm, definition.name, definition.collision,
                           &adapter, definition.trampoline, nullptr, nullptr);
  }

  template <typename Adapter>
  bool InstallOne(LuaVm& vm, Adapter&, const ConstantBinding& definition) {
    return InstallConstant(vm, definition);
  }

  bool InstallFunction(LuaVm& vm, std::string_view name,
                       CollisionPolicy collision, void* adapter,
                       lua_CFunction trampoline,
                       lua_CFunction result_pusher,
                       lua_CFunction raw_handler);
  bool InstallConstant(LuaVm& vm, const ConstantBinding& definition);
  [[nodiscard]] bool CanInstallInto(const LuaVm& vm) const noexcept;
  [[nodiscard]] bool CanPublishBatch(
      const LuaVm& vm, std::span<const BindingCandidate> candidates) const;

  std::string owner_name_;
  BindingScope scope_;
  BindingDestination destination_;
  std::uint64_t owner_id_{0};
  std::weak_ptr<detail::LuaOwnerToken> owner_;
  std::weak_ptr<detail::LuaBindingRegistry> registry_;
  std::uint64_t generation_{0};
  std::vector<std::shared_ptr<detail::LuaBindingLease>> owned_leases_;
  std::vector<BindingDescriptor> descriptors_;
};

}
