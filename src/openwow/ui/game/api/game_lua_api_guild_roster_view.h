#pragma once

#include <cstdint>
#include <string>
#include <string_view>

struct lua_State;

namespace openwow::ui::game {
class CVarSystem;
}

namespace openwow::game {
struct GuildMember;
class WorldSession;
}

namespace openwow::ui::game::detail {

[[nodiscard]] const openwow::game::GuildMember* GetGuildRosterMemberByDisplayIndex(
    lua_State* L, int one_based_index);
[[nodiscard]] bool GuildRosterDisplayContainsGuid(
    const openwow::game::WorldSession& session, std::uint64_t raw_guid);
[[nodiscard]] int GetGuildRosterVisibleMemberCount(lua_State* L);
[[nodiscard]] int GetGuildRosterTotalMemberCount(lua_State* L);
[[nodiscard]] bool GetGuildRosterShowOfflineState();
void EnsureGuildRosterShowOfflineCVarBehavior(openwow::ui::game::CVarSystem& cvars);
void SetGuildRosterShowOfflineState(bool show);
void SetGuildRosterShowOfflineState(lua_State* L, bool show);
void ApplyGuildRosterSort(lua_State* L, std::string_view sort_type);
void SetGuildRosterSelectionByDisplayIndex(lua_State* L, int one_based_index);
[[nodiscard]] int GetGuildRosterSelectionDisplayIndex(lua_State* L);
[[nodiscard]] std::string BuildGuildRosterExportContents(
    const openwow::game::WorldSession& session);
void RequestGuildRosterExport(lua_State* L);
void TryCompletePendingGuildRosterExport(
    const openwow::game::WorldSession& session);
void ResetGuildRosterViewState();

}
