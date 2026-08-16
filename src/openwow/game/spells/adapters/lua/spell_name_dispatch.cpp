#include "openwow/game/spells/adapters/lua/spell_name_dispatch.h"

#include "openwow/ui/game/api/game_lua_api_internal.h"

#include <utility>

namespace openwow::game::spells::adapters::lua {

std::optional<ResolvedSpellName> ResolveSpellName(
    lua_State* state, std::string query) {
  const auto resolved =
      openwow::ui::game::detail::ResolveSpellQueryFromLooseNameQuery(
          state, std::move(query));
  if (!resolved) {
    return std::nullopt;
  }
  return ResolvedSpellName{
      .spell_id = resolved->spell_id,
      .from_pet_book = resolved->from_pet_book,
  };
}

void DispatchResolvedSpell(
    lua_State* state, const ResolvedSpellName spell,
    const std::uint64_t target_guid) {
  openwow::ui::game::detail::DispatchResolvedSpellNameQuery(
      state,
      {
          .spell_id = spell.spell_id,
          .from_pet_book = spell.from_pet_book,
      },
      target_guid);
}

}
