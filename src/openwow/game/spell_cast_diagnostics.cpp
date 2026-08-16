#include "openwow/game/spell_cast_diagnostics.h"

namespace openwow::game {

SpellCastDiagnostics& SpellCastDiagnostics::Get() {
  static SpellCastDiagnostics state;
  return state;
}

}
