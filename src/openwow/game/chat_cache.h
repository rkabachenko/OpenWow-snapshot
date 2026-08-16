
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::game {

class WorldSession;

[[nodiscard]] std::string SerializeChatCache(
    const data::dbc::DbcLoader* dbc, std::uint32_t current_zone_id);
void ApplyChatCachePayload(WorldSession& session, std::string_view data);
void TrySyncLoadedChatChannels(WorldSession& session);
void SetGuildRecruitmentChannelAutoJoin(WorldSession& session, bool enabled,
                                        bool suppress_leave_on_disable = false);
void ResetChatCacheRuntimeState();

}
