#pragma once

#include <cstdint>
#include <optional>

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::game {

class SpellBook;
struct PetBarState;

struct SpellCooldownState {
  double start_time_s{0.0};
  double duration_s{0.0};
  double enabled{1.0};

  [[nodiscard]] double ExpiresAt() const {
    return start_time_s + duration_s;
  }
};

[[nodiscard]] std::optional<SpellCooldownState> ResolveSpellbookCooldown(
    const SpellBook &spell_book, std::uint32_t spell_id);

[[nodiscard]] std::optional<SpellCooldownState> ResolvePetBarSpellCooldown(
    const PetBarState &pet_bar, std::uint32_t spell_id,
    const data::dbc::DbcLoader *dbc);

[[nodiscard]] int RemainingCooldownMilliseconds(const SpellCooldownState &cooldown);

}
