#pragma once

#include <cstdint>
#include <optional>
#include <string>

struct lua_State;

namespace openwow::game::spells::adapters::lua {

struct ResolvedSpellName {
  std::uint32_t spell_id{0};
  bool from_pet_book{false};
};

[[nodiscard]] std::optional<ResolvedSpellName> ResolveSpellName(
    lua_State* state, std::string query);
void DispatchResolvedSpell(
    lua_State* state, ResolvedSpellName spell, std::uint64_t target_guid);

}
