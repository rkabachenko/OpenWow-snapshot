#pragma once

#include "openwow/data/formats/dbc/dbc_loader.h"

#include <cstdint>

namespace openwow::game {

class QuestXPCalc {
 public:

  [[nodiscard]] static std::uint32_t CalculateRewardXP(
      const openwow::data::dbc::DbcLoader& dbc,
      std::int32_t quest_level,
      std::uint32_t player_level,
      std::uint32_t xp_group_id);
};

}
