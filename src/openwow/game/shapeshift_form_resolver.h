#pragma once

#include <cstdint>
#include <vector>

namespace openwow::data::dbc {
struct SpellEntry;
}

namespace openwow::game {

class WorldSession;

[[nodiscard]] std::vector<std::uint32_t> ResolveShapeshiftFormSpellIds(
    const WorldSession *session);

[[nodiscard]] std::uint32_t ResolveShapeshiftFormIdFromSpell(
    const WorldSession &session, std::uint32_t spell_id);

[[nodiscard]] bool IsShapeshiftFormSpell(
    const data::dbc::SpellEntry &spell);

}
