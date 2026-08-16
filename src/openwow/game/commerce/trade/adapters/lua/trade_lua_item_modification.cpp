#include "openwow/game/commerce/trade/adapters/lua/trade_lua_adapter.h"
#include "openwow/game/commerce/trade/adapters/lua/trade_lua_offer_support.h"

#include "openwow/game/actions/held_cursor/held_cursor.h"
#include "openwow/ui/lua_numeric.h"

#include <string_view>

namespace openwow::ui::game::detail {

int LuaReplaceTradeEnchant(lua_State* state) {
  auto& adapter = RequireTradeLuaAdapter(state);
  static_cast<void>(adapter.ConfirmTradeEnchant());
  return 0;
}

int LuaBindEnchant(lua_State* state) {
  auto& adapter = RequireTradeLuaAdapter(state);

  adapter.EndBoundItemEnchant();
  return 0;
}

int LuaReplaceEnchant(lua_State* state) {
  auto& adapter = RequireTradeLuaAdapter(state);
  static_cast<void>(adapter.ReplayPendingItemCast(true));
  return 0;
}

int LuaClickTargetTradeButton(lua_State* state) {
  if (!lua_isnumber(state, 1)) {
    return luaL_error(state, "Usage: ClickTargetTradeButton(index)");
  }

  auto& adapter = RequireTradeLuaAdapter(state);
  auto* cursor = adapter.cursor();
  const auto* money =
      cursor != nullptr
          ? cursor->get_if<
                openwow::game::actions::held_cursor::PlayerMoney>()
          : nullptr;
  if (money != nullptr) {
    auto& trade = adapter.trade();
    if (TrySetOwnTradeGold(
            adapter, trade, GetOwnTradeGold(trade) + money->amount)) {
      cursor->Clear({
          .release_source_lease = true,
          .publish_money_owner_update = false,
      });
    }
  } else if (openwow::ui::TruncateLuaNumberToI32(
                 lua_tonumber(state, 1)) == 7) {
    adapter.ClickTargetEnchantSlot();
  }
  return 0;
}

int LuaEndBoundTradeable(lua_State* state) {
  if (!lua_isstring(state, 1)) {
    return luaL_error(state, "Usage: EndBoundTradeable(\"type\")");
  }

  auto& adapter = RequireTradeLuaAdapter(state);
  const std::string_view type = lua_tostring(state, 1);
  if (type == "itemenchant") {
    adapter.EndBoundItemEnchant();
  } else if (type == "gem") {
    static_cast<void>(adapter.SendSocketingGems());
  } else if (type == "spellenchant") {
    adapter.EndBoundSpellEnchant();
  }
  return 0;
}

}
