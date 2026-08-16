#include "openwow/ui/runtime/diagnostics/frame_script_profiling.h"

#include "openwow/runtime/time/game_clock.h"

extern "C" {
#include <lua.hpp>
}

#include <cstdint>

namespace openwow::ui::runtime::diagnostics {
namespace {

thread_local std::uint64_t g_debug_profile_start_counter = 0;

}

int LuaDebugProfileStart([[maybe_unused]] lua_State* state) {
  g_debug_profile_start_counter =
      openwow::core::GameClock::GetRawTimingCounter();
  return 0;
}

int LuaDebugProfileStop(lua_State* state) {
  const std::uint64_t current_counter =
      openwow::core::GameClock::GetRawTimingCounter();
  const std::uint64_t counter_delta =
      current_counter - g_debug_profile_start_counter;
  const double milliseconds =
      openwow::core::detail::ConvertTimingCounterDeltaToMilliseconds(
          counter_delta,
          openwow::core::GameClock::GetTimingCounterFrequencyHz());
  lua_pushnumber(state, milliseconds);
  return 1;
}

}
