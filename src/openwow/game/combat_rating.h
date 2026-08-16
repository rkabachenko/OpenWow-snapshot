#pragma once

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/game_table.h"
#include "openwow/game/objects/cgplayer.h"

#include <cstdint>

namespace openwow::game {

[[nodiscard]] inline float ComputeCombatRatingBonus(
    const CGPlayer_C &player,
    const openwow::data::dbc::DbcLoader &dbc,
    const std::uint8_t rating_index) {
  const auto rating = static_cast<float>(player.GetCombatRating(rating_index));

  const auto *const rating_table =
      GameTable_ResolveName(dbc.game_tables(), "CombatRatings");
  if (rating_table == nullptr || rating_table->num_columns == 0u ||
      player.State().GetLevel() == 0u) {
    return 0.0f;
  }
  const auto rating_row = static_cast<int>(
      rating_index * rating_table->num_columns + player.State().GetLevel() - 1u);
  const auto *const rating_entry =
      dbc.gt_combat_ratings().LookupEntryByRowIndex(rating_row);

  const auto *const scalar_table =
      GameTable_ResolveName(dbc.game_tables(), "OCTClassCombatRatingScalar");
  if (scalar_table == nullptr || scalar_table->num_columns == 0u ||
      player.State().GetClass() == 0u) {
    return 0.0f;
  }
  const auto scalar_row = static_cast<int>(
      (player.State().GetClass() - 1u) * scalar_table->num_columns +
      rating_index + 1u);
  const auto *const scalar_entry =
      dbc.gt_oct_class_combat_rating_scalar().LookupEntryByRowIndex(scalar_row);

  const float divisor = rating_entry != nullptr ? rating_entry->value : 0.0f;
  const float class_scalar =
      scalar_entry != nullptr ? scalar_entry->value : 0.0f;
  if (divisor == 0.0f || class_scalar == 0.0f) {
    return 0.0f;
  }
  return rating * class_scalar / divisor;
}

}
