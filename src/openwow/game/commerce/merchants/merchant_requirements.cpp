#include "openwow/game/commerce/merchants/merchant_requirements.h"

#include "openwow/data/formats/dbc/dbc_entries_gameplay.h"

namespace openwow::game {

bool MeetsMerchantArenaRatingRequirement(
    const openwow::data::dbc::ItemExtendedCostEntry& extended_cost,
    const MerchantArenaTeamQuery& team_query) {
  if (extended_cost.personal_arena_rating == 0) {
    return true;
  }
  if (!team_query) {
    return false;
  }

  const auto team_matches = [&](const std::uint8_t slot,
                                const std::uint8_t minimum_team_type) {
    const auto team = team_query(slot);
    return team.has_value() && team->team_type >= minimum_team_type &&
           team->rating >= extended_cost.personal_arena_rating;
  };

  switch (extended_cost.arena_slot) {
    case 0:
      return team_matches(0, 2) || team_matches(1, 3) ||
             team_matches(2, 5);
    case 1:
      return team_matches(1, 3);
    case 2:
      return team_matches(2, 5);
    default:
      return false;
  }
}

}
