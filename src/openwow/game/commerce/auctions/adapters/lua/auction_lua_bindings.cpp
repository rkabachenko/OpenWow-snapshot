#include "openwow/game/commerce/auctions/adapters/lua/auction_lua_bindings.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/runtime/lua/lua_composition.h"
#include "openwow/game/commerce/auctions/adapters/lua/auction_lua_adapter.h"
#include "openwow/game/commerce/auctions/auction_interaction.h"
#include "openwow/game/game_misc_utils.h"
#include "openwow/game/interaction_sender.h"
#include "openwow/game/inventory/adapters/ui/item_cursor_pickup_controller.h"
#include "openwow/game/inventory/items/item_use_requirements.h"
#include "openwow/game/inventory/items/item_acquisition_rules.h"
#include "openwow/game/session/handlers/commerce/auction_packets.h"
#include "openwow/game/session_handler.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/game_ui_core.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/game/secure_execution.h"
#include "openwow/ui/runtime/security/protected_action_gate.h"
#include "openwow/ui/surfaces/game/runtime/npc_interaction_controller.h"

extern "C" {
#include <lua.hpp>
}

#include <memory>
#include <utility>

namespace openwow::ui::game::detail {
int LuaGetOwnerAuctionItems(lua_State*);
int LuaGetBidderAuctionItems(lua_State*);
int LuaGetAuctionItemInfo(lua_State*);
int LuaGetAuctionItemLink(lua_State*);
int LuaGetAuctionItemSubClasses(lua_State*);
int LuaGetAuctionInvTypes(lua_State*);
int LuaGetAuctionItemClasses(lua_State*);
int LuaGetNumAuctionItems(lua_State*);
int LuaSortAuctionItems(lua_State*);
int LuaSortAuctionClearSort(lua_State*);
int LuaQueryAuctionItems(lua_State*);
int LuaCanSendAuctionQuery(lua_State*);
int LuaPlaceAuctionBid(lua_State*);
int LuaGetAuctionSellItemInfo(lua_State*);
int LuaCancelSell(lua_State*);
int LuaClickAuctionSellItemButton(lua_State*);
int LuaStartAuction(lua_State*);
int LuaCalculateAuctionDeposit(lua_State*);
int LuaCancelAuction(lua_State*);
int LuaSetSelectedAuctionItem(lua_State*);
int LuaGetSelectedAuctionItem(lua_State*);
int LuaCloseAuctionHouse(lua_State*);
int LuaCanCancelAuction(lua_State*);
int LuaGetAuctionHouseDepositRate(lua_State*);
int LuaGetAuctionItemTimeLeft(lua_State*);
int LuaGetAuctionSort(lua_State*);
int LuaIsAuctionSortReversed(lua_State*);
int LuaSetAuctionsTabShowing(lua_State*);
int LuaSortAuctionApplySort(lua_State*);
int LuaSortAuctionAddSort(lua_State*);
}

namespace openwow::ui::game {
namespace {

constexpr openwow::ui::LuaGlobalBinding kAuctionLuaBindings[] = {
    {"GetOwnerAuctionItems", detail::LuaGetOwnerAuctionItems},
    {"GetBidderAuctionItems", detail::LuaGetBidderAuctionItems},
    {"GetAuctionItemInfo", detail::LuaGetAuctionItemInfo},
    {"GetAuctionItemLink", detail::LuaGetAuctionItemLink},
    {"GetAuctionItemSubClasses", detail::LuaGetAuctionItemSubClasses},
    {"GetAuctionInvTypes", detail::LuaGetAuctionInvTypes},
    {"GetAuctionItemClasses", detail::LuaGetAuctionItemClasses},
    {"GetNumAuctionItems", detail::LuaGetNumAuctionItems},
    {"SortAuctionItems", detail::LuaSortAuctionItems},
    {"SortAuctionClearSort", detail::LuaSortAuctionClearSort},
    {"QueryAuctionItems", detail::LuaQueryAuctionItems},
    {"CanSendAuctionQuery", detail::LuaCanSendAuctionQuery},
    {"PlaceAuctionBid", detail::LuaPlaceAuctionBid},
    {"GetAuctionSellItemInfo", detail::LuaGetAuctionSellItemInfo},
    {"CancelSell", detail::LuaCancelSell},
    {"ClickAuctionSellItemButton", detail::LuaClickAuctionSellItemButton},
    {"StartAuction", detail::LuaStartAuction},
    {"CalculateAuctionDeposit", detail::LuaCalculateAuctionDeposit},
    {"CancelAuction", detail::LuaCancelAuction},
    {"SetSelectedAuctionItem", detail::LuaSetSelectedAuctionItem},
    {"GetSelectedAuctionItem", detail::LuaGetSelectedAuctionItem},
    {"CloseAuctionHouse", detail::LuaCloseAuctionHouse},
    {"CanCancelAuction", detail::LuaCanCancelAuction},
    {"GetAuctionHouseDepositRate", detail::LuaGetAuctionHouseDepositRate},
    {"GetAuctionItemTimeLeft", detail::LuaGetAuctionItemTimeLeft},
    {"GetAuctionSort", detail::LuaGetAuctionSort},
    {"IsAuctionSortReversed", detail::LuaIsAuctionSortReversed},
    {"SetAuctionsTabShowing", detail::LuaSetAuctionsTabShowing},
    {"SortAuctionApplySort", detail::LuaSortAuctionApplySort},

    {"SortAuctionSetSort", detail::LuaSortAuctionAddSort},
};

}

openwow::ui::lua::NativeBindingCatalog AuctionNativeBindingCatalog(
    std::shared_ptr<AuctionLuaAdapter> adapter) {
  auto catalog = openwow::ui::lua::NativeFunctionCatalog(
      "game.commerce.auctions", openwow::ui::lua::BindingScope::kWorld,
      kAuctionLuaBindings);
  catalog.lifecycle_context = std::move(adapter);
  return catalog;
}

void AuctionLuaAdapter::Bind(Dependencies dependencies) {
  dependencies_.emplace(dependencies);
}

bool AuctionLuaAdapter::bound() const noexcept {
  return dependencies_.has_value();
}

const AuctionLuaAdapter::Dependencies& AuctionLuaAdapter::deps() const {
  return *dependencies_;
}

openwow::game::AuctionInteraction& AuctionLuaAdapter::auction() const {
  return deps().auction;
}
const openwow::data::dbc::DbcLoader* AuctionLuaAdapter::dbc() const noexcept {
  return deps().dbc;
}
openwow::game::actions::held_cursor::HeldCursor*
AuctionLuaAdapter::held_cursor() const noexcept {
  return deps().held_cursor;
}
openwow::game::InteractionSender& AuctionLuaAdapter::interaction() const {
  return deps().interaction;
}
openwow::game::ItemDefinitions& AuctionLuaAdapter::item_definitions() const {
  return deps().items;
}
openwow::game::ObjectManager& AuctionLuaAdapter::objects() const {
  return deps().world_session.objects();
}
openwow::game::PlayerInventoryReplica& AuctionLuaAdapter::inventory() const {
  return deps().inventory;
}
openwow::game::QueryCache& AuctionLuaAdapter::query_cache() const {
  return deps().queries;
}
void AuctionLuaAdapter::PresentListChanged(
    const openwow::game::AuctionSelectionList list) const {
  auto& dispatch = deps().events;
  switch (list) {
    case openwow::game::AuctionSelectionList::kOwner:
      dispatch.FireAuctionOwnedListUpdate();
      break;
    case openwow::game::AuctionSelectionList::kBidder:
      dispatch.FireAuctionBidderListUpdate();
      break;
    case openwow::game::AuctionSelectionList::kList:
      dispatch.FireAuctionItemListUpdate();
      break;
  }
}
void AuctionLuaAdapter::PresentSellSelectionChanged() const {
  deps().events.FireEvent(events::NEW_AUCTION_UPDATE);
}
void AuctionLuaAdapter::PresentMultiSellStarted(
    const std::uint32_t stack_count) const {
  deps().events.FireAuctionMultiSellStart(static_cast<int>(stack_count));
}
void AuctionLuaAdapter::PresentMultiSellFailed() const {
  deps().events.FireAuctionMultiSellFailure();
}
void AuctionLuaAdapter::RequestOwnerRefresh() const {
  deps().packets.TrySendOwnerRefresh();
}
void AuctionLuaAdapter::RequestNameRefresh(
    const std::uint64_t guid,
    const openwow::game::AuctionSelectionList list) const {
  deps().packets.RequestListRefreshOnNameResolve(guid, list);
}
void AuctionLuaAdapter::CloseHouse() const {
  if (auction().auctioneer_guid() == 0) return;
  deps().events.FireAuctionHouseClosed();
  auto& state = auction().state();
  if (state.HasActiveMultiSell()) {
    const auto released_items = state.AbortMultiSell();
    for (const auto item_guid : released_items) {
      GameUI_OnMouseoverUnitLeave(item_guid);
    }
    if (!released_items.empty()) {
      deps().events.FireEvent(events::NEW_AUCTION_UPDATE);
    }

    deps().events.FireAuctionMultiSellFailure();
    state.SetAtAH(false);
    auction().CloseAuctionHouse();
    return;
  }

  const auto selected_item = state.TakeSellItemSelection();
  state.SetAtAH(false);
  auction().CloseAuctionHouse();
  if (selected_item.has_value()) {
    GameUI_OnMouseoverUnitLeave(*selected_item);
    deps().events.FireEvent(events::NEW_AUCTION_UPDATE);
  }
}
bool AuctionLuaAdapter::MeetsItemRequirements(
    const openwow::game::CGPlayer_C& player,
    const openwow::game::ItemUseRequirementView& requirements) const {
  return openwow::game::PlayerMeetsItemUseRequirements(
      player, requirements, deps().item_requirements,
      deps().session_state.GetProficiencyMask(
          static_cast<std::uint8_t>(requirements.item_class)));
}
bool AuctionLuaAdapter::CanAcquireItem(
    const openwow::game::ItemTemplate& item, const std::uint32_t count) const {
  return openwow::game::EvaluateItemAcquisition(
             inventory(), item_definitions(), dbc(), item, count)
      .allowed();
}
bool AuctionLuaAdapter::CanPerformProtectedAction(
    const int action_kind) const {
  return openwow::ui::game::GameUI_CanPerformProtectedAction(action_kind) != 0;
}
void AuctionLuaAdapter::ReturnSellItemToCursor(
    const std::uint64_t item_guid, const std::uint64_t container_guid,
    const std::int32_t slot) const {
  auto* cursor = deps().held_cursor;
  if (cursor == nullptr) return;
  (void)openwow::game::inventory::ui::PickupItemCursor(
      *cursor, inventory(), item_definitions(), dbc(),
      objects().GetActivePlayerGuid(), openwow::game::ObjectGuid(item_guid),
      {
          .source = {
              .container = openwow::game::ObjectGuid(container_guid),
              .slot = slot,
          },
          .source_policy =
              openwow::game::inventory::ui::ItemCursorSourcePolicy::
                  kProvidedOrigin,
          .leave_events =
              openwow::game::inventory::ui::ItemCursorLeaveEvents::kPreserve,
      });
}
void AuctionLuaAdapter::SellItems(
    const std::uint64_t auctioneer,
    std::vector<std::pair<std::uint64_t, std::uint32_t>> items,
    const std::uint32_t min_bid, const std::uint32_t buyout,
    const std::uint32_t duration_minutes) const {
  interaction().SendAuctionSellItems(
      auctioneer, items, min_bid, buyout, duration_minutes);
}

AuctionLuaAdapter& RequireAuctionLuaAdapter(lua_State* state) {
  auto* adapter = static_cast<AuctionLuaAdapter*>(
      openwow::ui::lua::detail::ActiveBindingAdapter(state));
  if (adapter == nullptr) {

    adapter = static_cast<AuctionLuaAdapter*>(
        openwow::ui::lua::detail::GlobalBindingAdapter(state, "GetOwnerAuctionItems"));
  }
  if (adapter == nullptr || !adapter->bound()) {
    luaL_error(state, "auction Lua API is not bound");
  }
  return *adapter;
}

}
