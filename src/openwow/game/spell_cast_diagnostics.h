#pragma once

#include "openwow/game/spell_runtime_values.h"

#include <cstdint>
#include <optional>

namespace openwow::game {

struct SpellActionInvocation;

class SpellCastDiagnostics final {
 public:
  static SpellCastDiagnostics& Get();

  std::uint32_t last_cast_spell_id{0};
  std::uint32_t last_cast_failure_reason{187};
  std::uint32_t previous_cast_time{0};
  std::optional<SpellActionInvocation> last_action_invocation;

 private:
  SpellCastDiagnostics() = default;
};

}
