#pragma once

#include <cstdint>

namespace openwow::client {

struct GlueFrameDeltaState {
  std::uint32_t previous_now_ms{0};
  bool has_previous_now_ms{false};
};

[[nodiscard]] inline std::uint32_t ResolveGlueFrameDeltaMs(
    GlueFrameDeltaState& state,
    const std::uint32_t now_ms,
    const std::uint32_t explicit_delta_ms) {
  std::uint32_t resolved_delta_ms = explicit_delta_ms;
  if (resolved_delta_ms == 0u && state.has_previous_now_ms) {
    resolved_delta_ms = now_ms - state.previous_now_ms;
  }
  state.previous_now_ms = now_ms;
  state.has_previous_now_ms = true;
  return resolved_delta_ms;
}

}
