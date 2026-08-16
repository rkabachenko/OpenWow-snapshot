#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::game {
class QueryCache;
class CGPlayer_C;
class WorldSession;
struct ItemTemplate;
}

namespace openwow::game {

struct AuraFilter {
    int index = -1;
    const char* name = nullptr;
    const char* rank = nullptr;
    uint8_t flags = 1;

};

bool ParseAuraFilter(void* lua_state, int stack_index, AuraFilter* out);
std::uint8_t ParseAuraFilterFlags(std::string_view filter,
                                  std::uint8_t initial_flags = 1);
std::string AuraFilterFlagsToFilterString(std::uint8_t flags);

using ItemStatTable = std::array<float, 75>;

void ClearItemStatTable(ItemStatTable& stat_values);
bool BuildItemStatTableFromTemplateInfo(const ItemTemplate& item_template,
                                        std::int32_t random_property_id,
                                        std::uint32_t suffix_factor,
                                        const openwow::data::dbc::DbcLoader* dbc,
                                        std::uint32_t scaling_level,
                                        const CGPlayer_C& active_player,
                                        ItemStatTable& stat_values);
bool BuildItemStatTableFromItemLink(const char* item_link,
                                    const QueryCache& query_cache,
                                    const openwow::data::dbc::DbcLoader* dbc,
                                    const CGPlayer_C& active_player,
                                    ItemStatTable& stat_values);
void BuildItemStatDelta(const ItemStatTable& left, const ItemStatTable& right,
                        ItemStatTable& out_delta);
int PushItemStatFields(const ItemStatTable& stat_values, void* lua_state);

void InitWorldEventNames();

bool ScriptEvents_ResolveUnitName(WorldSession& session,
                                  const char* unit_id, const char** out_name,
                                  bool allow_enemy, bool include_realm);

uint32_t ScriptEvents_GetUnitXP(void* unit_obj);

uint32_t ScriptEvents_GetUnitNextLevelXP(void* unit_obj);

bool ScriptEvents_ResolveUnitObject(WorldSession& session,
                                    const char* unit_id, void** out_unit,
                                    bool allow_empty);

void* ScriptEvents_GetUnit(WorldSession& session, const char* unit_id);

int ScriptEvents_GetGuildBankTabIcon(void* lua_state);

struct PendingUnitEvent {
    uint64_t guid = 0;
    uint32_t event_id = 0;
    uint32_t _pad = 0;
};

struct UnitSpellcastScriptEventPayload {
    std::string_view spell_name;
    std::string_view spell_rank;
    std::uint8_t cast_id = 0;
};

void ScriptEvents_FireUnitEvent(uint64_t guid, uint32_t event_id);

void ScriptEvents_FireUnitSpellcastEvent(
    uint64_t guid,
    uint32_t event_id,
    const UnitSpellcastScriptEventPayload* payload);

void ScriptEvents_QueueUnitEvent(uint64_t guid, uint32_t event_id);

void ScriptEvents_QueueGlobalEvent(uint32_t event_id);

void ScriptEvents_QueueEventForAllUnits(uint32_t event_id);

void ScriptEvents_FlushPendingUnitEvents();

void ScriptEvents_ResetWorldEventFlag();

int PushUnitAuraQueryResult(WorldSession& session, std::uint64_t guid,
                            const AuraFilter& filter, void* lua_state);

}
