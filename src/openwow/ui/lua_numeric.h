#pragma once

extern "C" {
#include <lua.hpp>
}

#include <cmath>
#include <cstdint>
#include <limits>

namespace openwow::ui {

inline std::uint32_t SaturateLuaNumberToU32(lua_Number value) {
  if (std::isnan(value)) {
    return 0x80000000u;
  }
  if (value <= 0.0) {
    return 0u;
  }

  constexpr lua_Number kMaxU32 =
      static_cast<lua_Number>(std::numeric_limits<std::uint32_t>::max());
  if (value >= kMaxU32) {
    return std::numeric_limits<std::uint32_t>::max();
  }

  return static_cast<std::uint32_t>(std::trunc(value));
}

inline std::uint32_t ClampLuaNumberToU32(lua_Number value) {

  if (std::isnan(value)) {
    return std::numeric_limits<std::uint32_t>::max();
  }
  if (value <= 0.0) {
    return 0;
  }

  constexpr lua_Number kMaxU32 =
      static_cast<lua_Number>(std::numeric_limits<std::uint32_t>::max());
  if (value >= kMaxU32) {
    return std::numeric_limits<std::uint32_t>::max();
  }

  return static_cast<std::uint32_t>(std::trunc(value));
}

inline std::int32_t TruncateLuaNumberToI32(lua_Number value) {

  const lua_Number truncated = std::trunc(value);
  constexpr lua_Number kMinI32 =
      static_cast<lua_Number>(std::numeric_limits<std::int32_t>::min());
  constexpr lua_Number kMaxI32 =
      static_cast<lua_Number>(std::numeric_limits<std::int32_t>::max());
  if (!std::isfinite(truncated) || truncated < kMinI32 || truncated > kMaxI32) {
    return std::numeric_limits<std::int32_t>::min();
  }
  return static_cast<std::int32_t>(truncated);
}

inline lua_Number LuaToNumberOrZero(lua_State *state, const int index) {
  return lua_tonumber(state, index);
}

inline constexpr std::int32_t SignedI32FromU32Bits(const std::uint32_t value) noexcept {
  if (value <= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
    return static_cast<std::int32_t>(value);
  }
  return std::numeric_limits<std::int32_t>::min() +
         static_cast<std::int32_t>(value - 0x80000000u);
}

}
