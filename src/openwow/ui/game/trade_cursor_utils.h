#pragma once

#include "openwow/game/commerce/trade/trade_interaction.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"

#include <cstdint>

namespace openwow::ui::game::detail {

inline bool BlockTradeItemInteraction(
    ::openwow::game::WorldSession& session,
    const std::uint64_t item_guid) {
  if (!session.trade().IsLocalPlayerTradeItemGuid(item_guid)) {
    return false;
  }

  DisplaySystemMessage(473);
  return true;
}

}
