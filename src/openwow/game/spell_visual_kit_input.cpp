
#include "openwow/game/spell_visual_kit_input.h"

namespace openwow::game {

void SpellVisualKitInput::Init(std::uint32_t spell_rec,
                               std::uint32_t kit_rec,
                               std::uint32_t type) {
  spell_record      = spell_rec;
  visual_kit_record = kit_rec;
  visual_type       = type;
  position_ptr      = 0;
  target_guid_low   = 0;
  target_guid_high  = 0;
  auto_start        = 1;
  harmful           = 0;
  is_persistent     = 0;
  delay             = -1;
  source_guid_low   = 0;
  source_guid_high  = 0;
  timestamp         = 0;
}

}
