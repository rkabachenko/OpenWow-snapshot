#pragma once

#include "openwow/ui/glue/glue_game_state.h"

#include <optional>
#include <span>

namespace openwow::ui::glue::detail {
class LegacyAdlerRandom;
}

namespace openwow::ui::glue {

[[nodiscard]] bool IsWotlkRaceClassCombination(int race_id, int class_id);

[[nodiscard]] std::optional<int> PickRandomAllowedClass(
    std::span<const int> allowed_class_ids, detail::LegacyAdlerRandom &random);

void SubmitCharacterCreation(GlueGameState &state, const char *name);

}
