#pragma once

#include <cstdint>

namespace openwow::data::dbc {
struct SpellEntry;
}

namespace openwow::game {

class WorldSession;

[[nodiscard]] std::uint32_t ResolveEffectiveSpellId(
    const WorldSession& session, std::uint32_t spell_id);

[[nodiscard]] const data::dbc::SpellEntry* ResolveEffectiveSpell(
    const WorldSession& session, std::uint32_t spell_id);

}
