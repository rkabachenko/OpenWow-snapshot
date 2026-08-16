
#include "openwow/game/interaction_predicate.h"

#include "openwow/data/formats/dbc/dbc_enums.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/interaction_range.h"
#include "openwow/game/object_types.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/objects/cgunit.h"

#include <cstdint>

namespace openwow::game {

namespace {

constexpr double kNonUnitInteractionRangeSquared = 25.0;

[[nodiscard]] bool ShapeshiftFormBlocksInteraction(const CGPlayer_C& player) {
  const auto form_id = player.State().SuppressesCurrentFormSpellQueries()
                           ? std::uint8_t{0}
                           : player.Animation().GetShapeshiftForm();
  if (form_id == 0u) {
    return false;
  }

  const auto* const dbc = player.dbc_loader();
  const auto* const form =
      dbc != nullptr ? dbc->spell_shapeshift_form().LookupEntry(form_id)
                     : nullptr;
  if (form == nullptr) {
    return false;
  }

  if ((form->flags & data::dbc::kShapeshiftFormFlagIsStance) != 0u) {
    return false;
  }

  return (form->flags & data::dbc::kShapeshiftFormFlagBlocksAutoCancel) != 0u;
}

}

bool CanInteractWithTarget(const CGPlayer_C& active_player,
                           const CGObject_C& target) {

  const bool ignore_range =
      active_player.AutoInteractSuppressesInteractionRange();

  if (ShapeshiftFormBlocksInteraction(active_player)) {
    return false;
  }

  if (!ignore_range) {

    double budget_squared = kNonUnitInteractionRangeSquared;
    if (target.IsUnit()) {
      const auto& unit = static_cast<const CGUnit_C&>(target);
      budget_squared =
          static_cast<double>(interaction_range::ComputeUnitInteractionRangeSquared(
              active_player.State().GetCombatReach(),
              unit.State().GetCombatReach()));
    }

    if (active_player.GetSquaredDistanceToPosition(target.GetPosition()) >
        budget_squared) {
      return false;
    }
  }

  if (active_player.Animation().StandSelectionInteractionTargetGuid() != 0u) {
    return false;
  }

  if (active_player.State().IsDead()) {
    return false;
  }

  if ((active_player.GetMovementInfo().flags & kMoveFlagFalling) != 0u) {
    return false;
  }

  return !active_player.State().IsStunned() &&
         active_player.Movement().CanControlCharacter();
}

}
