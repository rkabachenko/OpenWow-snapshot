#pragma once

#include "openwow/debug/control/debug_control_server.h"

namespace openwow::debug::control {

class BoostJsonDebugControlCodec final : public DebugControlCodec {
 public:
  ~BoostJsonDebugControlCodec() override = default;

  [[nodiscard]] DebugControlDecodeResult DecodeRequest(
      std::string_view frame,
      const DebugControlServerLimits& limits) const override;

  [[nodiscard]] CapabilityResult<std::string> EncodeResponse(
      const DebugControlResponse& response,
      const DebugControlServerLimits& limits) const override;
};

}
