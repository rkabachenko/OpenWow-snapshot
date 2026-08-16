
#include "openwow/net/wotlk/spells.h"

namespace openwow::net::wotlk {

SpellCastRequest BuildSpellCastRequest(std::uint32_t spell_id) {
  return {.spell_id = spell_id};
}

SpellCastTargetedRequest BuildSpellCastTargetedRequest(
    std::uint32_t spell_id,
    std::uint64_t target_guid,
    std::uint32_t flags) {
  SpellCastTargetedRequest r;
  r.spell_id = spell_id;
  r.target_guid = target_guid;
  r.target_flags = flags;
  r.dest_x = 0.0f;
  r.dest_y = 0.0f;
  r.dest_z = 0.0f;
  return r;
}

SpellCastTargetedRequest BuildSpellCastAtLocationRequest(
    std::uint32_t spell_id, float x, float y, float z) {
  SpellCastTargetedRequest r;
  r.spell_id = spell_id;
  r.target_guid = 0;
  r.target_flags = SpellTargetFlag::DestLoc;
  r.dest_x = x;
  r.dest_y = y;
  r.dest_z = z;
  return r;
}

SpellStopRequest BuildSpellStopRequest(std::uint32_t spell_id,
                                        std::uint32_t cast_id) {
  SpellStopRequest r;
  r.spell_id = spell_id;
  r.cast_id = cast_id;
  return r;
}

CancelAuraRequest BuildCancelAuraRequest(std::uint32_t spell_id) {
  return {.spell_id = spell_id};
}

CancelChannellingRequest BuildCancelChannellingRequest(
    std::uint32_t spell_id) {
  return {.spell_id = spell_id};
}

UseItemRequest BuildUseItemRequest(std::uint64_t item_guid,
                                    std::uint8_t bag, std::uint8_t slot,
                                    std::uint32_t spell_id,
                                    std::uint64_t target) {
  UseItemRequest r;
  r.item_guid = item_guid;
  r.bag_index = bag;
  r.slot_index = slot;
  r.spell_id = spell_id;
  r.target_guid = target;
  return r;
}

}
