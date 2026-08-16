#include "openwow/game/spell_cooldown_state.h"

#include "openwow/runtime/time/game_clock.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/pet_manager.h"
#include "openwow/game/spell_book.h"

#include <algorithm>
#include <cmath>

namespace openwow::game {

std::optional<SpellCooldownState> ResolveSpellbookCooldown(
    const SpellBook &spell_book, const std::uint32_t spell_id) {
  const auto current_time_s = core::GameClock::GetTickCountSeconds();
  std::optional<SpellCooldownState> best;
  const auto consider = [&](const SpellCooldown& cooldown) {
    const auto category_ms =
        cooldown.category_cooldown_ms & 0x7FFFFFFFu;
    const auto enabled =
        (cooldown.category_cooldown_ms & 0x80000000u) == 0 ? 1.0 : 0.0;
    const auto duration_ms = std::max(cooldown.cooldown_ms, category_ms);
    if (duration_ms == 0u && enabled != 0.0) return;
    const SpellCooldownState candidate{
        .start_time_s = enabled != 0.0 ? cooldown.start_time_s
                                       : current_time_s,
        .duration_s = enabled != 0.0
                          ? static_cast<double>(duration_ms) / 1000.0
                          : 0.001,
        .enabled = enabled,
    };
    if (candidate.enabled != 0.0 &&
        candidate.ExpiresAt() <= current_time_s) {
      return;
    }
    if (!best.has_value() || candidate.ExpiresAt() > best->ExpiresAt()) {
      best = candidate;
    }
  };

  if (const auto it = spell_book.cooldowns().find(spell_id);
      it != spell_book.cooldowns().end()) {
    consider(it->second);
  }

  const auto* const dbc = spell_book.dbc_loader();
  const auto* const spell =
      dbc != nullptr ? dbc->spell().LookupEntry(spell_id) : nullptr;
  if (spell != nullptr) {
    if (const auto category =
            spell_book.category_cooldowns().find(spell->category);
        spell->category != 0u &&
        category != spell_book.category_cooldowns().end()) {
      consider(category->second);
    }

    constexpr std::uint32_t kSpellCategoryFlagBlocksEverySpell = 0x2u;
    for (const auto& [category_id, window] : spell_book.category_cooldowns()) {
      if (category_id == spell->category) {
        continue;
      }
      const auto* const category_record =
          dbc->spell_category().LookupEntry(category_id);
      if (category_record != nullptr &&
          (category_record->flags & kSpellCategoryFlagBlocksEverySpell) != 0u) {
        consider(window);
      }
    }

    if (const auto global =
            spell_book.global_cooldowns().find(spell->start_recovery_category);
        global != spell_book.global_cooldowns().end()) {
      consider(global->second);
    }
  }

  return best;
}

std::optional<SpellCooldownState> ResolvePetBarSpellCooldown(
    const PetBarState &pet_bar, const std::uint32_t spell_id,
    const data::dbc::DbcLoader *const dbc) {
  const auto current_time_s = core::GameClock::GetTickCountSeconds();

  double best_expiration_time_s = current_time_s;
  std::optional<SpellCooldownState> best_cooldown;

  const auto *const spell =
      dbc != nullptr ? dbc->spell().LookupEntry(spell_id) : nullptr;

  constexpr std::uint32_t kSpellCategoryFlagBlocksEverySpell = 0x2u;

  const auto consider = [&](const std::uint32_t duration_ms,
                            const double start_time_s, const bool enabled) {
    if (duration_ms == 0u && enabled) {
      return;
    }
    const SpellCooldownState candidate{
        .start_time_s = enabled ? start_time_s : current_time_s,
        .duration_s =
            enabled ? static_cast<double>(duration_ms) / 1000.0 : 0.001,
        .enabled = enabled ? 1.0 : 0.0,
    };
    const auto expiration_time_s = candidate.ExpiresAt();
    if (expiration_time_s < best_expiration_time_s) {
      return;
    }
    best_expiration_time_s = expiration_time_s;
    best_cooldown = candidate;
  };

  for (const auto &node : pet_bar.cooldowns) {

    if (node.spell_id == spell_id) {
      consider(node.cooldown_ms, node.start_time_s, node.enabled);
    }

    if (node.category_cooldown_ms != 0u) {
      const auto *const category =
          dbc != nullptr ? dbc->spell_category().LookupEntry(node.category)
                         : nullptr;
      const bool category_matches =
          (node.category != 0u && spell != nullptr &&
           spell->category == node.category) ||
          (category != nullptr &&
           (category->flags & kSpellCategoryFlagBlocksEverySpell) != 0u);
      if (category_matches) {
        consider(node.category_cooldown_ms, node.start_time_s, node.enabled);
      }
    }

    if (spell != nullptr && node.gcd_duration_ms != 0u &&
        node.gcd_category == spell->start_recovery_category) {
      consider(node.gcd_duration_ms, node.start_time_s, true);
    }
  }

  return best_cooldown;
}

int RemainingCooldownMilliseconds(const SpellCooldownState &cooldown) {
  if (cooldown.enabled == 0.0 || cooldown.duration_s <= 0.0) {
    return 0;
  }

  const auto remaining_s =
      cooldown.ExpiresAt() - core::GameClock::GetTickCountSeconds();
  if (remaining_s <= 0.0) {
    return 0;
  }

  return static_cast<int>(std::ceil(remaining_s * 1000.0));
}

}
