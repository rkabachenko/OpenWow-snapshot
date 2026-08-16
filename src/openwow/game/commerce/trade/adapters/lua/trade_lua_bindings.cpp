#include "openwow/game/commerce/trade/adapters/lua/trade_lua_bindings.h"
#include "openwow/game/commerce/trade/adapters/lua/trade_lua_api.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/runtime/lua/lua_composition.h"
#include "openwow/game/commerce/trade/adapters/lua/trade_lua_adapter.h"
#include "openwow/data/formats/dbc/dbc_enums.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/actions/held_cursor/held_cursor.h"
#include "openwow/game/battlefield_info.h"
#include "openwow/game/group_system.h"
#include "openwow/game/instance_handler.h"
#include "openwow/game/interaction_sender.h"
#include "openwow/game/inventory/adapters/ui/item_cursor_pickup_controller.h"
#include "openwow/game/inventory/adapters/ui/item_spell_target_controller.h"
#include "openwow/game/inventory/adapters/ui/item_target_cursor_presenter.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/items/item_interactions.h"
#include "openwow/game/inventory/items/item_icon_resolver.h"
#include "openwow/game/inventory/items/item_use_requirements.h"
#include "openwow/game/localization.h"
#include "openwow/game/misc_handler.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/session_handler.h"
#include "openwow/game/spell_cast_runtime.h"
#include "openwow/game/commerce/trade/trade_interaction.h"
#include "openwow/game/session/unit_token_resolver.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/game_ui_core.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/game/secure_execution.h"
#include "openwow/ui/runtime/security/protected_action_gate.h"
#include "openwow/ui/game/ui_error_manager.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/game/localization.h"
#include "openwow/ui/runtime/lua/lua_binding.h"

extern "C" {
#include <lua.hpp>
}

#include <memory>
#include <utility>

namespace openwow::ui::game {

using namespace detail;

namespace {

constexpr openwow::ui::LuaGlobalBinding kTradeLuaBindings[] = {
    {"GetTradePlayerItemInfo", LuaGetTradePlayerItemInfo},
    {"GetTradePlayerItemLink", LuaGetTradePlayerItemLink},
    {"GetTradeTargetItemInfo", LuaGetTradeTargetItemInfo},
    {"GetTradeTargetItemLink", LuaGetTradeTargetItemLink},
    {"GetPlayerTradeMoney", LuaGetPlayerTradeMoney},
    {"SetTradeMoney", LuaSetTradeMoney},
    {"GetTargetTradeMoney", LuaGetTargetTradeMoney},
    {"ClickTradeButton", LuaClickTradeButton},
    {"AcceptTrade", LuaAcceptTrade},
    {"CancelTradeAccept", LuaCancelTradeAccept},
    {"CancelTrade", LuaCancelTrade},
    {"CloseTrade", LuaCloseTrade},
    {"AddTradeMoney", LuaAddTradeMoney},
    {"InitiateTrade", LuaInitiateTrade},
    {"BeginTrade", LuaBeginTrade},
    {"PickupTradeMoney", LuaPickupTradeMoney},
    {"ReplaceTradeEnchant", LuaReplaceTradeEnchant},
    {"ClickTargetTradeButton", LuaClickTargetTradeButton},
    {"EndBoundTradeable", LuaEndBoundTradeable},
    {"BindEnchant", LuaBindEnchant},
    {"ReplaceEnchant", LuaReplaceEnchant},
};

}

openwow::ui::lua::NativeBindingCatalog TradeNativeBindingCatalog(
    std::shared_ptr<TradeLuaAdapter> adapter) {
  auto catalog = openwow::ui::lua::NativeFunctionCatalog(
      "game.commerce.trade", openwow::ui::lua::BindingScope::kWorld, kTradeLuaBindings);
  catalog.lifecycle_context = std::move(adapter);
  return catalog;
}

void TradeLuaAdapter::Bind(Dependencies dependencies) {
  dependencies_.emplace(dependencies);
}

bool TradeLuaAdapter::bound() const noexcept {
  return dependencies_.has_value();
}

const TradeLuaAdapter::Dependencies& TradeLuaAdapter::deps() const {
  return *dependencies_;
}

openwow::game::TradeInteraction& TradeLuaAdapter::trade() const {
  return deps().trade;
}
openwow::game::InteractionSender& TradeLuaAdapter::interaction() const {
  return deps().interaction;
}
openwow::game::ObjectManager& TradeLuaAdapter::objects() const {
  return deps().world_session.objects();
}
openwow::game::PlayerInventoryReplica& TradeLuaAdapter::inventory() const {
  return deps().inventory;
}
openwow::game::ItemDefinitions& TradeLuaAdapter::items() const {
  return deps().items;
}
openwow::game::Localization& TradeLuaAdapter::localization() const {
  return deps().localization;
}
openwow::game::QueryCache& TradeLuaAdapter::queries() const {
  return deps().queries;
}
openwow::game::actions::held_cursor::HeldCursor* TradeLuaAdapter::cursor()
    const noexcept {
  return deps().cursor;
}
const openwow::data::dbc::DbcLoader* TradeLuaAdapter::dbc() const noexcept {
  return deps().dbc;
}
void TradeLuaAdapter::ShowSystemMessage(const int message) const {
  DisplaySystemMessage(message);
}
void TradeLuaAdapter::Present(
    const TradeLuaEvent event, const int first, const int second) const {
  auto& dispatch = deps().events;
  switch (event) {
    case TradeLuaEvent::kAcceptChanged:
      dispatch.FireTradeAcceptUpdate(first, second);
      break;
    case TradeLuaEvent::kPlayerItemChanged:
      dispatch.FireTradePlayerItemChanged(first);
      break;
    case TradeLuaEvent::kPlayerMoneyChanged:
      dispatch.FirePlayerTradeMoney();
      break;
    case TradeLuaEvent::kClosed:
      dispatch.FireTradeClosed();
      break;
  }
}
void TradeLuaAdapter::LeaveItemMouseover(const std::uint64_t guid) const {
  GameUI_OnMouseoverUnitLeave(guid);
}
void TradeLuaAdapter::HoldTradeItem(
    const openwow::game::ItemInstance& item, const std::uint8_t source_bag,
    const std::uint8_t source_slot) const {
  auto* held = deps().cursor;
  if (held == nullptr) return;
  held->Clear({
      .release_source_lease = true,
      .publish_money_owner_update = false,
  });
  const auto* definition = items().GetItem(item.entry);
  const auto display_id = definition != nullptr ? definition->display_id : 0;
  namespace cursor = openwow::game::actions::held_cursor;
  held->HoldLiveItem(
      cursor::LiveItem{
          .item = item,
          .source_bag = source_bag,
          .source_slot = source_slot,
      },
      cursor::Presentation{
          .texture_path =
              openwow::game::ResolveItemInventoryIconTexturePath(
                  dbc(), display_id),
          .texture_mode = cursor::TextureMode::HeldTexture,
          .sound = cursor::Sound::CursorGrabObject,

          .grid = openwow::game::inventory::ui::ResolveItemCursorGrid(
              items(), item.entry),
      });
}
std::uint64_t TradeLuaAdapter::ResolveTradeTarget(
    const std::string_view token) const {
  return openwow::game::ResolveUnitToken(
             objects(), deps().group, deps().battlefield,
             deps().instance, token)
      .GetRawValue();
}
std::uint32_t TradeLuaAdapter::CurrentPlayedTime() const {
  return deps().play_time.current_total_played_time();
}
bool TradeLuaAdapter::CanPerformProtectedAction(const int action_kind) const {
  return openwow::ui::game::GameUI_CanPerformProtectedAction(action_kind) != 0;
}
bool TradeLuaAdapter::ReplayPendingItemCast(const bool require_live_item) const {
  const auto item_guid =
      deps().item_interactions.pending_modification().GetRawValue();
  if (item_guid == 0 ||
      (require_live_item &&
       objects().GetItem(openwow::game::ObjectGuid(item_guid)) == nullptr)) {
    return false;
  }
  auto& spells = deps().spells;
  auto& targeting = spells.GetTargeting();
  constexpr std::uint32_t kItemTargetMask = 0x4010;
  const auto spell_id = targeting.GetSpellId();
  if (!targeting.IsTargeting() ||
      (targeting.GetTargetMask() & kItemTargetMask) == 0 ||
      spell_id == 0 ||
      spells.GetSlot(openwow::game::SpellSlotType::kCurrent).spell_id !=
          spell_id) {
    return false;
  }
  spells.SetItemTarget(
      openwow::game::SpellSlotType::kCurrent,
      openwow::game::ObjectGuid(item_guid));
  interaction().SendCastSpellOnItem(spell_id, 0, item_guid);
  openwow::game::inventory::ui::ClearItemTargetCursor(targeting);
  targeting.CancelTargeting();
  return true;
}
bool TradeLuaAdapter::ConfirmTradeEnchant() const {
  auto& targeting = deps().spells.GetTargeting();
  constexpr std::uint32_t kItemTargetMask = 0x4010;
  if (!targeting.IsTargeting() ||
      (targeting.GetTargetMask() & kItemTargetMask) == 0 ||
      !trade().is_open() || targeting.GetSpellId() == 0) {
    return false;
  }
  interaction().SendCastSpellOnTradeEnchantSlot(targeting.GetSpellId(), 0);
  openwow::game::inventory::ui::ClearItemTargetCursor(targeting);
  targeting.CancelTargeting();
  return true;
}
bool TradeLuaAdapter::SendSocketingGems() const {
  const auto& socket = deps().item_interactions.socket();
  if (!socket.has_value() ||
      (socket->gems[0].IsEmpty() && socket->gems[1].IsEmpty() &&
       socket->gems[2].IsEmpty())) {
    return false;
  }
  interaction().SendSocketGems(
      socket->item.GetRawValue(), socket->gems[0].GetRawValue(),
      socket->gems[1].GetRawValue(), socket->gems[2].GetRawValue());
  return true;
}
void TradeLuaAdapter::ClickTargetEnchantSlot() const {
  auto& targeting = deps().spells.GetTargeting();
  const auto spell_id = targeting.GetSpellId();
  const auto* spell =
      dbc() != nullptr && spell_id != 0
          ? dbc()->spell().LookupEntry(spell_id)
          : nullptr;
  if (!trade().is_open() || !targeting.IsTargeting() || spell == nullptr) {
    return;
  }

  constexpr std::uint32_t kEnchantItemPermanentEffect = 53;

  constexpr unsigned int kTradeEnchantSlot = 6;
  for (int effect_index = 0; effect_index < openwow::data::dbc::kMaxSpellEffects;
       ++effect_index) {
    if (spell->effect[effect_index] != kEnchantItemPermanentEffect) {
      continue;
    }

    const auto existing_enchant_id =
        trade().GetTargetPermanentEnchantId(kTradeEnchantSlot);
    const auto prospective_enchant_id =
        static_cast<std::uint32_t>(spell->effect_misc_value[effect_index]);
    if (existing_enchant_id != 0 &&
        existing_enchant_id != prospective_enchant_id) {
      if (dbc() != nullptr) {
        const auto* const old_enchant =
            dbc()->spell_item_enchantment().LookupEntry(existing_enchant_id);
        const auto* const new_enchant =
            dbc()->spell_item_enchantment().LookupEntry(prospective_enchant_id);
        ui::game::ScriptEventDispatch::Get().FireEventArgs(
            ui::game::events::TRADE_REPLACE_ENCHANT,
            {ui::game::EventArg{old_enchant != nullptr
                                     ? std::string(old_enchant->description)
                                     : std::string()},
             ui::game::EventArg{new_enchant != nullptr
                                     ? std::string(new_enchant->description)
                                     : std::string()}});
      }
      return;
    }

    (void)ConfirmTradeEnchant();
    return;
  }
}
bool TradeLuaAdapter::MeetsItemRequirements(
    const openwow::game::CGPlayer_C& player,
    const openwow::game::ItemUseRequirementView& requirements) const {
  return openwow::game::PlayerMeetsItemUseRequirements(
      player, requirements, deps().item_requirements,
      deps().session_state.GetProficiencyMask(
          static_cast<std::uint8_t>(requirements.item_class)));
}
void TradeLuaAdapter::EndBoundItemEnchant() const {
  const auto item =
      deps().item_interactions.pending_modification();
  if (!item.IsEmpty()) {
    openwow::game::inventory::ui::ProcessItemSpellTarget(
        dbc(), inventory(), queries(), deps().item_interactions,
        objects(), interaction(), deps().spells,
        deps().localization, deps().ui_errors, deps().events,
        item,
        openwow::game::inventory::ui::ItemTargetConfirmation::kConfirmed);
  }
}
void TradeLuaAdapter::EndBoundSpellEnchant() const {
  (void)ReplayPendingItemCast(false);
}

TradeLuaAdapter& RequireTradeLuaAdapter(lua_State* state) {
  auto* adapter = static_cast<TradeLuaAdapter*>(
      openwow::ui::lua::detail::ActiveBindingAdapter(state));
  if (adapter == nullptr) {

    adapter = static_cast<TradeLuaAdapter*>(
        openwow::ui::lua::detail::GlobalBindingAdapter(state, "GetTradePlayerItemInfo"));
  }
  if (!adapter || !adapter->bound()) {
    luaL_error(state, "trade Lua API is not bound");
  }
  return *adapter;
}

}
