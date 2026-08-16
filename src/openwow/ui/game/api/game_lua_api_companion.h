
#pragma once

#include <cstdint>
#include <vector>

struct lua_State;

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::game {
class WorldSession;
}

namespace openwow::ui::game::detail {

bool ResolveCompanionSpellCursorInfo(lua_State* L,
                                     std::uint32_t spell_id,
                                     std::uint32_t& out_index,
                                     const char*& out_type);
bool GetCompanionSpellByTypeAndIndex(const char* type,
                                     std::uint32_t zero_based_index,
                                     std::uint32_t& out_spell_id);

int LuaGetNumCompanions(lua_State* L);
int LuaGetCompanionInfo(lua_State* L);
int LuaGetCompanionCooldown(lua_State* L);
int LuaCallCompanion(lua_State* L);
int LuaDismissCompanion(lua_State* L);
int LuaSummonRandomCritter(lua_State* L);

int LuaGetNumStablePets(lua_State* L);
int LuaGetStablePetFoodTypes(lua_State* L);

int LuaPickupCompanion(lua_State* L);

void SetActiveCompanionSpellForType(const char* type, std::uint32_t spell_id);
void ClearActiveCompanionSpellForType(const char* type);
[[nodiscard]] bool IsCompanionSpellActive(std::uint32_t spell_id);
void HandleCritterCompanionEntryChanged(openwow::game::WorldSession& session,
                                        std::uint32_t current_entry,
                                        std::uint32_t previous_entry);

void RefreshCritterActionBarForDescriptorChange(
    openwow::game::WorldSession& session,
    std::uint32_t previous_entry,
    std::uint32_t current_entry);
void SetCompanionSpellListForTesting(const char* type,
                                     std::vector<std::uint32_t> spell_ids);
void ResetCompanionApiStateForTesting();

}
