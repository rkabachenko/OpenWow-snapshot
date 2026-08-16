#pragma once

#include "openwow/game/commerce/trade/adapters/lua/trade_lua_adapter.h"
#include "openwow/game/commerce/trade/trade_interaction.h"
#include "openwow/game/interaction_sender.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgplayer.h"

#include <cstdint>

namespace openwow::ui::game::detail {

inline std::uint32_t GetOwnTradeGold(
    const openwow::game::TradeInteraction& trade) {
  return trade.is_open() ? trade.own_gold() : 0;
}

inline bool TrySetOwnTradeGold(
    TradeLuaAdapter& adapter, openwow::game::TradeInteraction& trade,
    const std::uint32_t gold) {
  const auto* player = adapter.objects().GetLocalPlayerTyped();
  if (!player || player->GetMoney() < gold) {
    return false;
  }

  trade.SetPendingOwnGold(gold);
  adapter.interaction().SendSetTradeGold(gold);
  return true;
}

}
