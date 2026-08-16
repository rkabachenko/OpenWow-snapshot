#include "openwow/game/commerce/trade/adapters/lua/trade_lua_adapter.h"
#include "openwow/ui/runtime/security/protected_action_gate.h"
#include "openwow/game/commerce/trade/adapters/lua/trade_lua_offer_support.h"

#include "openwow/game/actions/held_cursor/held_cursor.h"
#include "openwow/game/commerce/adapters/ui/money_cursor_controller.h"
#include "openwow/game/commerce/trade/trade_interaction.h"
#include "openwow/game/interaction_sender.h"
#include "openwow/game/object_guid.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/ui/lua_numeric.h"

namespace openwow::ui::game::detail {
namespace {

void PresentAcceptTransition(
    TradeLuaAdapter& adapter,
    const openwow::game::TradeAcceptTransition& transition) {
  for (const auto& event : transition.events) {
    adapter.Present(
        TradeLuaEvent::kAcceptChanged, event.player_accepted,
        event.trader_accepted);
  }
}

}

int LuaAcceptTrade(lua_State* state) {
  auto& adapter = RequireTradeLuaAdapter(state);

  if (!adapter.CanPerformProtectedAction(
          openwow::ui::game::protected_action_kind::kTrade)) {
    return 0;
  }

  auto& trade = adapter.trade();
  const auto transition = trade.SetPlayerAcceptedState(true);
  PresentAcceptTransition(adapter, transition);
  if (transition.HasEvents()) {
    adapter.interaction().SendAcceptTrade(trade.accept_state_index());
  }
  return 0;
}

int LuaCancelTradeAccept(lua_State* state) {
  auto& adapter = RequireTradeLuaAdapter(state);

  if (!adapter.CanPerformProtectedAction(
          openwow::ui::game::protected_action_kind::kTrade)) {
    return 0;
  }

  const auto transition =
      adapter.trade().SetPlayerAcceptedState(false);
  PresentAcceptTransition(adapter, transition);
  if (transition.HasEvents()) {
    adapter.interaction().SendUnacceptTrade();
  }
  return 0;
}

int LuaCancelTrade(lua_State* state) {
  RequireTradeLuaAdapter(state).interaction().SendCancelTrade();
  return 0;
}

int LuaCloseTrade(lua_State* state) {
  auto& adapter = RequireTradeLuaAdapter(state);
  auto& trade = adapter.trade();
  if (trade.ShouldFirePlayerTradeMoneyOnScriptClose()) {
    adapter.Present(TradeLuaEvent::kPlayerMoneyChanged);
  }
  (void)trade.CloseFromScript();
  const auto changes = trade.TakeChanges();
  for (const auto guid : changes.released_item_guids) {
    adapter.LeaveItemMouseover(guid);
  }
  if (changes.closed) {
    adapter.Present(TradeLuaEvent::kClosed);
  }
  adapter.interaction().SendCancelTrade();
  return 0;
}

int LuaInitiateTrade(lua_State* state) {
  if (!lua_isstring(state, 1)) {
    return luaL_error(state, "Usage: InitiateTrade(\"unit\")");
  }

  auto& adapter = RequireTradeLuaAdapter(state);
  const openwow::game::ObjectGuid target_guid(
      adapter.ResolveTradeTarget(lua_tostring(state, 1)));
  if (target_guid.IsEmpty()) {
    return 0;
  }

  const auto player_guid = adapter.objects().GetActivePlayerGuid();
  const auto* player = adapter.objects().GetUnit(player_guid);
  const auto* target = adapter.objects().GetPlayer(target_guid);
  if (!player || !target) {
    return 0;
  }
  if (player->State().IsDead()) {
    adapter.ShowSystemMessage(135);
    return 0;
  }
  if (player_guid == target_guid) {
    adapter.ShowSystemMessage(624);
    return 0;
  }
  if (!player->Interaction().IsFriendlyTo(*target)) {
    adapter.ShowSystemMessage(269);
    return 0;
  }

  adapter.interaction().SendInitiateTrade(target_guid.GetRawValue());
  return 0;
}

int LuaBeginTrade(lua_State* state) {

  RequireTradeLuaAdapter(state).interaction().SendBeginTrade();
  return 0;
}

int LuaPickupTradeMoney(lua_State* state) {
  if (!lua_isnumber(state, 1)) {
    return luaL_error(state, "Usage: PickupTradeMoney(amount)");
  }

  const auto signed_amount =
      openwow::ui::TruncateLuaNumberToI32(lua_tonumber(state, 1));
  if (signed_amount <= 0) {
    return 0;
  }

  auto& adapter = RequireTradeLuaAdapter(state);
  auto& trade = adapter.trade();
  const auto amount = static_cast<std::uint32_t>(signed_amount);
  const auto current_offer = GetOwnTradeGold(trade);
  auto* cursor = adapter.cursor();
  if (amount > current_offer || cursor == nullptr) {
    return 0;
  }

  if (TrySetOwnTradeGold(adapter, trade, current_offer - amount)) {
    cursor->Clear();
    static_cast<void>(openwow::game::commerce::ui::PickupMoneyCursor(
        *cursor, openwow::game::commerce::CopperAmount(amount),
        openwow::game::commerce::ui::MoneyCursorKind::kPlayerMoney));
  }
  return 0;
}

}
