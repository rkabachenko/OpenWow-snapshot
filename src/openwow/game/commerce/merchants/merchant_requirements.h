#pragma once

#include <cstdint>
#include <functional>
#include <optional>

namespace openwow::data::dbc {
struct ItemExtendedCostEntry;
}

namespace openwow::game {

struct MerchantArenaTeamRating {
  std::uint8_t team_type = 0;
  std::uint32_t rating = 0;
};

using MerchantArenaTeamQuery =
    std::function<std::optional<MerchantArenaTeamRating>(std::uint8_t slot)>;

[[nodiscard]] bool MeetsMerchantArenaRatingRequirement(
    const openwow::data::dbc::ItemExtendedCostEntry& extended_cost,
    const MerchantArenaTeamQuery& team_query);

}
