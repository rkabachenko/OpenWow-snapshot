#include "openwow/game/spell_effective_variant.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/group_system.h"
#include "openwow/game/world_session.h"

#include <algorithm>
#include <cstddef>

namespace openwow::game {
namespace {

constexpr std::uint32_t kDungeonCreatureType = 1u;
constexpr std::uint32_t kRaidCreatureType = 2u;
constexpr std::uint32_t kShapeshiftFormRaidMapFlag = 0x100u;
constexpr std::size_t kSpellDifficultyVariantCount = 4u;

std::uint8_t ResolveDungeonDifficultyIndex(const WorldSession& session) {

  if (session.scene_state().IsInInstance()) {
    return static_cast<std::uint8_t>(
        std::min<std::uint32_t>(session.instance_difficulty().difficulty_index,
                                kSpellDifficultyVariantCount - 1u));
  }
  return static_cast<std::uint8_t>(GroupSystem::Get().GetDungeonDifficulty());
}

std::uint8_t ResolveRaidDifficultyIndex(
    const WorldSession& session, const data::dbc::SpellShapeshiftFormEntry& form) {
  const auto active = session.scene_state().IsInInstance()
                          ? static_cast<std::uint8_t>(
                                std::min<std::uint32_t>(
                                    session.instance_difficulty().difficulty_index,
                                    kSpellDifficultyVariantCount - 1u))
                          : static_cast<std::uint8_t>(
                                GroupSystem::Get().GetRaidDifficulty());
  if ((form.flags & kShapeshiftFormRaidMapFlag) == 0u) {
    return active;
  }

  return static_cast<std::uint8_t>(
      (active & 0x1u) +
      (static_cast<std::uint32_t>(GroupSystem::Get().GetPlayerDifficultyIndex())
       << 1u));
}

std::uint8_t ResolveDifficultyIndex(
    const WorldSession& session,
    const data::dbc::SpellShapeshiftFormEntry& form) {
  switch (form.creature_type) {
    case kDungeonCreatureType:
      return ResolveDungeonDifficultyIndex(session);
    case kRaidCreatureType:
      return ResolveRaidDifficultyIndex(session, form);
    default:
      return static_cast<std::uint8_t>(kSpellDifficultyVariantCount);
  }
}

}

std::uint32_t ResolveEffectiveSpellId(const WorldSession& session,
                                      const std::uint32_t spell_id) {
  if (spell_id == 0u) {
    return 0u;
  }

  const auto* const dbc = session.GetDbcLoader();
  const auto* const spell = dbc != nullptr ? dbc->spell().LookupEntry(spell_id)
                                            : nullptr;
  if (spell == nullptr || spell->spell_difficulty_id == 0u) {
    return spell_id;
  }

  const auto* const player = session.objects().GetLocalPlayerTyped();
  if (player == nullptr) {
    return spell_id;
  }

  const auto form_id = static_cast<std::uint32_t>(
      player->Animation().GetShapeshiftForm());
  if (form_id == 0u || dbc == nullptr) {
    return spell_id;
  }
  const auto* const form = dbc->spell_shapeshift_form().LookupEntry(form_id);
  if (form == nullptr) {
    return spell_id;
  }

  const auto difficulty_index = ResolveDifficultyIndex(session, *form);
  if (difficulty_index >= kSpellDifficultyVariantCount) {
    return spell_id;
  }

  const auto* const difficulty =
      dbc->spell_difficulty().LookupEntry(spell->spell_difficulty_id);
  if (difficulty == nullptr) {
    return spell_id;
  }

  std::uint32_t resolved_id = difficulty->spell_id[difficulty_index];
  if (resolved_id == 0u && difficulty_index == 3u) {
    resolved_id = difficulty->spell_id[1];
  }
  if (resolved_id == 0u) {
    resolved_id = difficulty->spell_id[0];
  }
  return resolved_id != 0u ? resolved_id : spell_id;
}

const data::dbc::SpellEntry* ResolveEffectiveSpell(
    const WorldSession& session, const std::uint32_t spell_id) {
  const auto* const dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    return nullptr;
  }
  return dbc->spell().LookupEntry(ResolveEffectiveSpellId(session, spell_id));
}

}
