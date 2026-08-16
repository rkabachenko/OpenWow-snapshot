#pragma once

#include <optional>
#include <string>

namespace openwow::data::dbc {
struct ChatChannelsEntry;
}

namespace openwow::game {

class WorldSession;

[[nodiscard]] std::optional<std::string> ResolveBuiltinChatChannelName(
    const WorldSession& session,
    const data::dbc::ChatChannelsEntry& definition);

}
