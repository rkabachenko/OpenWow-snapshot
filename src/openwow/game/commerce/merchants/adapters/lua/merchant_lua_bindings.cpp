#include "openwow/game/commerce/merchants/adapters/lua/merchant_lua_bindings.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/runtime/lua/lua_composition.h"
#include "openwow/game/commerce/merchants/adapters/lua/merchant_lua_adapter.h"
#include "openwow/game/actions/held_cursor/held_cursor.h"
#include "openwow/game/gossip_manager.h"
#include "openwow/game/inventory/items/item_use_requirements.h"
#include "openwow/game/localization.h"
#include "openwow/game/misc_handler.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/session_handler.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/merchant_repair_cost.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/game/tooltip_system.h"

extern "C" {
#include <lua.hpp>
}

#include <memory>
#include <utility>

namespace openwow::ui::game::detail {

int LuaPickupMerchantItem(lua_State* L);
int LuaGetMerchantNumItems(lua_State* L);
int LuaGetMerchantItemInfo(lua_State* L);
int LuaGetMerchantItemLink(lua_State* L);
int LuaGetMerchantItemMaxStack(lua_State* L);
int LuaGetMerchantItemCostInfo(lua_State* L);
int LuaGetMerchantItemCostItem(lua_State* L);
int LuaBuyMerchantItem(lua_State* L);
int LuaGetBuybackItemInfo(lua_State* L);
int LuaGetBuybackItemLink(lua_State* L);
int LuaBuybackItem(lua_State* L);
int LuaRepairAllItems(lua_State* L);
int LuaCanMerchantRepair(lua_State* L);
int LuaCloseMerchant(lua_State* L);
int LuaGetNumBuybackItems(lua_State* L);
int LuaShowBuybackSellCursor(lua_State* L);
int LuaShowContainerSellCursor(lua_State* L);
int LuaShowInventorySellCursor(lua_State* L);
int LuaShowMerchantSellCursor(lua_State* L);
int LuaGetContainerItemPurchaseInfo(lua_State* L);
int LuaContainerItemPurchaseItem(lua_State* L);
int LuaContainerRefundItemPurchase(lua_State* L);

}

namespace openwow::ui::game {

using namespace detail;

namespace {

constexpr openwow::ui::LuaGlobalBinding kMerchantLuaBindings[] = {
    {"PickupMerchantItem", LuaPickupMerchantItem},
    {"GetMerchantNumItems", LuaGetMerchantNumItems},
    {"GetMerchantItemInfo", LuaGetMerchantItemInfo},
    {"GetMerchantItemLink", LuaGetMerchantItemLink},
    {"GetMerchantItemMaxStack", LuaGetMerchantItemMaxStack},
    {"GetMerchantItemCostInfo", LuaGetMerchantItemCostInfo},
    {"GetMerchantItemCostItem", LuaGetMerchantItemCostItem},
    {"BuyMerchantItem", LuaBuyMerchantItem},
    {"GetBuybackItemInfo", LuaGetBuybackItemInfo},
    {"GetBuybackItemLink", LuaGetBuybackItemLink},
    {"BuybackItem", LuaBuybackItem},
    {"RepairAllItems", LuaRepairAllItems},
    {"CanMerchantRepair", LuaCanMerchantRepair},
    {"CloseMerchant", LuaCloseMerchant},
    {"GetNumBuybackItems", LuaGetNumBuybackItems},
    {"ShowBuybackSellCursor", LuaShowBuybackSellCursor},
    {"ShowContainerSellCursor", LuaShowContainerSellCursor},
    {"ShowInventorySellCursor", LuaShowInventorySellCursor},
    {"ShowMerchantSellCursor", LuaShowMerchantSellCursor},
    {"GetContainerItemPurchaseInfo", LuaGetContainerItemPurchaseInfo},

    {"GetContainerItemPurchaseItem", LuaContainerItemPurchaseItem},
    {"ContainerRefundItemPurchase", LuaContainerRefundItemPurchase},
};

}

openwow::ui::lua::NativeBindingCatalog MerchantNativeBindingCatalog(
    std::shared_ptr<MerchantLuaAdapter> adapter) {
  auto catalog = openwow::ui::lua::NativeFunctionCatalog(
      "game.commerce.merchants", openwow::ui::lua::BindingScope::kWorld, kMerchantLuaBindings);
  catalog.lifecycle_context = std::move(adapter);
  return catalog;
}

void MerchantLuaAdapter::Bind(Dependencies dependencies) {
  dependencies_.emplace(dependencies);
}

bool MerchantLuaAdapter::bound() const noexcept {
  return dependencies_.has_value();
}

const MerchantLuaAdapter::Dependencies& MerchantLuaAdapter::deps() const {
  return *dependencies_;
}

const openwow::data::dbc::DbcLoader* MerchantLuaAdapter::dbc() const noexcept {
  return deps().dbc;
}

openwow::game::CursorSurface* MerchantLuaAdapter::cursor() const noexcept {
  return deps().cursor;
}

openwow::game::GossipManager& MerchantLuaAdapter::gossip() const {
  return deps().gossip;
}

openwow::game::actions::held_cursor::HeldCursor*
MerchantLuaAdapter::held_cursor() const noexcept {
  return deps().held_cursor;
}

openwow::game::InteractionSender& MerchantLuaAdapter::interaction() const {
  return deps().interaction;
}

openwow::game::ItemInteractionSession&
MerchantLuaAdapter::item_interactions() const {
  return deps().item_interactions;
}

openwow::game::LootInteraction& MerchantLuaAdapter::loot() const {
  return deps().loot;
}
openwow::game::Localization& MerchantLuaAdapter::localization() const {
  return deps().localization;
}

const openwow::game::MerchantArenaTeamQuery&
MerchantLuaAdapter::arena_team_query() const {
  return deps().arena_team_query;
}

openwow::game::ObjectManager& MerchantLuaAdapter::objects() const {
  return deps().world_session.objects();
}

openwow::game::PlayerInventoryReplica& MerchantLuaAdapter::inventory() const {
  return deps().inventory;
}

openwow::game::QueryCache& MerchantLuaAdapter::query_cache() const {
  return deps().queries;
}

openwow::game::WorldSession& MerchantLuaAdapter::world_session() const {
  return deps().world_session;
}

std::uint32_t MerchantLuaAdapter::total_played_time() const {
  return deps().play_time.current_total_played_time();
}

bool MerchantLuaAdapter::CanUseItem(
    const openwow::game::ItemTemplate& item_template) const {
  const auto* player = objects().GetLocalPlayerTyped();
  if (player == nullptr) return true;
  const auto requirements =
      openwow::game::BuildItemUseRequirementView(item_template);
  return openwow::game::PlayerMeetsItemUseRequirements(
      *player, requirements, deps().item_requirements,
      deps().session_state.GetProficiencyMask(
          static_cast<std::uint8_t>(requirements.item_class)));
}

std::optional<MerchantLuaAdapter::RepairQuote>
MerchantLuaAdapter::repair_quote() const {
  const auto* vendor = ResolveMerchantRepairVendor(gossip(), objects());
  if (vendor == nullptr) return std::nullopt;
  const auto summary = CalculateMerchantRepairSummary(
      inventory(), query_cache(), objects(), deps().reputation, dbc(),
      *vendor);
  return RepairQuote{
      .vendor_guid = vendor->GetGuid().GetRawValue(),
      .total_cost = summary.total_cost,
  };
}

void MerchantLuaAdapter::CloseMerchant() const {
  if (!gossip().merchant().active()) return;
  gossip().DismissAll();
  deps().events.FireMerchantClosed();
  if (auto* held = held_cursor();
      held != nullptr &&
      held->kind() ==
          openwow::game::actions::held_cursor::Kind::MerchantItem) {
    held->Clear();
  }
}

void MerchantLuaAdapter::PresentMerchantUpdated() const {
  deps().events.FireMerchantUpdate();
}

void MerchantLuaAdapter::ShowItemTooltip(
    const std::uint32_t item_id) const {
  deps().tooltip.SetItemFromLoot(item_id, 0, 0);
}

MerchantLuaAdapter* TryMerchantLuaAdapter(lua_State* state) noexcept {
  auto* adapter = static_cast<MerchantLuaAdapter*>(
      openwow::ui::lua::detail::ActiveBindingAdapter(state));
  if (adapter == nullptr) {

    adapter = static_cast<MerchantLuaAdapter*>(
        openwow::ui::lua::detail::GlobalBindingAdapter(state,
                                                       "GetMerchantNumItems"));
  }
  return adapter != nullptr && adapter->bound() ? adapter : nullptr;
}

MerchantLuaAdapter& RequireMerchantLuaAdapter(lua_State* state) {
  auto* adapter = TryMerchantLuaAdapter(state);
  if (adapter == nullptr) {
    luaL_error(state, "merchant Lua API is not bound");
  }
  return *adapter;
}

}
