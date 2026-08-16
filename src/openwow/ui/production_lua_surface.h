#pragma once

#include <memory>

namespace openwow::ui::lua {
class LuaBindingComposition;
class LuaVm;
}

namespace openwow::ui::display {
class ProductionDisplaySettingsRuntime;
}
namespace openwow::ui::game {
class EventDispatcher;
class ItemLuaAdapter;
class LootLuaAdapter;
class AuctionLuaAdapter;
class MailLuaAdapter;
class MerchantLuaAdapter;
class TradeLuaAdapter;
}

namespace openwow::game {
class CursorSurface;
class MailStationeryChoices;
class WorldSession;
}

namespace openwow::ui {

struct ProductionWorldLuaAdapters {
  std::shared_ptr<game::ItemLuaAdapter> item;
  std::shared_ptr<game::LootLuaAdapter> loot;
  std::shared_ptr<game::AuctionLuaAdapter> auction;
  std::shared_ptr<game::MailLuaAdapter> mail;
  std::shared_ptr<game::MerchantLuaAdapter> merchant;
  std::shared_ptr<game::TradeLuaAdapter> trade;
};

[[nodiscard]] ProductionWorldLuaAdapters
CreateProductionWorldLuaAdapters();
void BindProductionWorldLuaAdapters(
    ProductionWorldLuaAdapters& adapters,
    openwow::game::WorldSession& session,
    openwow::game::CursorSurface& cursor,
    game::EventDispatcher& events,
    openwow::game::MailStationeryChoices& stationery);
[[nodiscard]] std::unique_ptr<lua::LuaBindingComposition>
CreateProductionGlueLuaBindings(
    display::ProductionDisplaySettingsRuntime& display_settings);
[[nodiscard]] std::unique_ptr<lua::LuaBindingComposition>
CreateProductionWorldLuaBindings(
    display::ProductionDisplaySettingsRuntime& display_settings,
    std::shared_ptr<game::ItemLuaAdapter> item_adapter,
    std::shared_ptr<game::LootLuaAdapter> loot_adapter,
    std::shared_ptr<game::AuctionLuaAdapter> auction_adapter,
    std::shared_ptr<game::MailLuaAdapter> mail_adapter,
    std::shared_ptr<game::MerchantLuaAdapter> merchant_adapter,
    std::shared_ptr<game::TradeLuaAdapter> trade_adapter);
bool InitializeProductionGlueLuaVm(lua::LuaVm& vm);
bool InitializeProductionWorldLuaVm(lua::LuaVm& vm);

}
