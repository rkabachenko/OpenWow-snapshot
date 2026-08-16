#include "openwow/ui/production_lua_surface.h"
#include "openwow/game/achievements/adapters/lua/achievement_lua_bindings.h"
#include "openwow/game/actions/adapters/lua/action_lua_bindings.h"
#include "openwow/game/activities/instances/adapters/lua/instance_lua_bindings.h"
#include "openwow/game/actors/pets/adapters/lua/pet_lua_bindings.h"
#include "openwow/game/actors/totems/adapters/lua/totem_lua_api.h"
#include "openwow/game/actors/units/adapters/lua/unit_lua_bindings.h"
#include "openwow/game/actors/vehicles/adapters/lua/vehicle_lua_bindings.h"
#include "openwow/game/calendar/composition/calendar_lua_registration.h"
#include "openwow/game/character/professions/adapters/lua/profession_lua_bindings.h"
#include "openwow/game/combat/adapters/lua/combat_lua_bindings.h"
#include "openwow/game/combat/logs/adapters/lua/combat_log_lua_api.h"
#include "openwow/game/commerce/auctions/adapters/lua/auction_lua_bindings.h"
#include "openwow/game/commerce/mail/adapters/lua/mail_lua_bindings.h"
#include "openwow/game/commerce/merchants/adapters/lua/merchant_lua_bindings.h"
#include "openwow/game/commerce/trade/adapters/lua/trade_lua_bindings.h"
#include "openwow/game/inventory/items/adapters/lua/item_lua_adapter.h"
#include "openwow/game/inventory/items/adapters/lua/item_lua_bindings.h"
#include "openwow/game/inventory/items/adapters/lua/socket_lua_bindings.h"
#include "openwow/game/inventory/loot/adapters/lua/loot_lua_adapter.h"
#include "openwow/game/inventory/loot/adapters/lua/loot_lua_bindings.h"
#include "openwow/game/movement/adapters/lua/movement_lua_bindings.h"
#include "openwow/game/movement/taxis/adapters/lua/taxi_lua_bindings.h"
#include "openwow/game/quests/adapters/lua/quest_lua_bindings.h"
#include "openwow/game/social/chat/adapters/lua/chat_lua_bindings.h"
#include "openwow/game/social/contacts/adapters/lua/social_lua_bindings.h"
#include "openwow/game/social/gossip/adapters/lua/gossip_lua_bindings.h"
#include "openwow/game/social/guilds/adapters/lua/guild_lua_bindings.h"
#include "openwow/game/social/petitions/adapters/lua/petition_lua_bindings.h"
#include "openwow/game/spells/adapters/lua/spell_lua_bindings.h"
#include "openwow/game/spells/adapters/lua/spell_power_lua_api.h"
#include "openwow/game/spells/glyphs/adapters/lua/glyph_lua_api.h"
#include "openwow/game/spells/glyphs/adapters/lua/glyph_lua_bindings.h"
#include "openwow/game/spells/spellbook/adapters/lua/spellbook_lua_api.h"
#include "openwow/game/spells/talents/adapters/lua/talent_lua_bindings.h"
#include "openwow/game/support/knowledge_base/adapters/lua/knowledge_base_lua_api.h"
#include "openwow/game/world_state/factions/adapters/lua/faction_lua_bindings.h"
#include "openwow/game/world_state/maps/adapters/lua/map_lua_bindings.h"
#include "openwow/game/world_state/tracking/adapters/lua/tracking_lua_bindings.h"
#include "openwow/world/presentation/adapters/lua/camera_lua_bindings.h"
#include "openwow/game/commerce/auctions/adapters/lua/auction_lua_adapter.h"
#include "openwow/game/commerce/mail/adapters/lua/mail_lua_adapter.h"
#include "openwow/game/commerce/mail/adapters/ui/mail_stationery_choices.h"
#include "openwow/game/commerce/merchants/adapters/lua/merchant_lua_adapter.h"
#include "openwow/game/commerce/trade/adapters/lua/trade_lua_adapter.h"
#include "openwow/game/actions/held_cursor/held_cursor.h"
#include "openwow/game/actions/held_cursor/adapters/platform/cursor_surface.h"
#include "openwow/game/battlefield_info.h"
#include "openwow/game/cooldown_tracker.h"
#include "openwow/game/group_system.h"
#include "openwow/game/guild_system.h"
#include "openwow/game/localization.h"
#include "openwow/game/reputation_info.h"
#include "openwow/game/spell_cast_runtime.h"
#include "openwow/game/world_session.h"

#include "openwow/audio/integration/game/game_audio_lua_bindings.h"
#include "openwow/ui/game/api/framexml_native_bindings.h"
#include "openwow/ui/game/api/framexml_core_native_bindings.h"
#include "openwow/ui/display/settings/adapters/lua/display_settings_lua.h"
#include "openwow/ui/display/settings/adapters/production_display_settings_runtime.h"
#include "openwow/ui/frame_script_standard_globals.h"
#include "openwow/ui/game/api/game_lua_api_globals.h"
#include "openwow/game/actions/macros/adapters/lua/macro_lua_api.h"
#include "openwow/ui/game/runtime/world_lua_runtime.h"
#include "openwow/ui/game/secure_execution.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/game/tooltip_system.h"
#include "openwow/ui/game/ui_error_manager.h"
#include "openwow/ui/glue/glue_native_binding_catalogs.h"
#include "openwow/ui/lua_base_overrides.h"
#include "openwow/ui/runtime/lua/frame_script_native_modules.h"
#include "openwow/ui/runtime/lua/lua_composition.h"
#include "openwow/ui/runtime/lua/retail_native_binding_plan.h"

#include <iterator>
#include <utility>
#include <vector>

namespace openwow::ui {

namespace {

void AppendCatalogs(std::vector<lua::NativeBindingCatalog>& destination,
                    std::vector<lua::NativeBindingCatalog> source) {
  destination.insert(destination.end(),
                     std::make_move_iterator(source.begin()),
                     std::make_move_iterator(source.end()));
}

void AppendCatalog(std::vector<lua::NativeBindingCatalog>& destination,
                   lua::NativeBindingCatalog catalog) {
  destination.push_back(std::move(catalog));
}

std::vector<lua::NativeBindingCatalog> GlueNativeModuleCatalogs(
    display::ProductionDisplaySettingsRuntime& display_settings) {
  auto sources = glue::detail::GlueNativeBindingCatalogs();
  AppendCatalog(
      sources,
      audio::integration::lua::
          SharedSoundVoiceChatNativeBindingCatalog());
  AppendCatalog(
      sources,
      display::lua_adapter::SharedDisplaySettingsNativeBindingCatalog(
          display_settings.service(), display_settings.frame_scale()));
  auto plan =
      lua::ComposeRetailGlueNativeBindings(std::move(sources));
  auto shared_type_modules =
      lua::modules::SharedFrameScriptTypeModules();

  std::vector<lua::NativeBindingCatalog> catalogs;
  catalogs.reserve(plan.before_frame_script_types.size() +
                   shared_type_modules.size() +
                   plan.after_frame_script_types.size());
  AppendCatalogs(catalogs, std::move(plan.before_frame_script_types));
  AppendCatalogs(catalogs, std::move(shared_type_modules));
  AppendCatalogs(catalogs, std::move(plan.after_frame_script_types));
  return catalogs;
}

std::vector<lua::NativeBindingCatalog> WorldNativeModuleCatalogs(
    display::ProductionDisplaySettingsRuntime& display_settings,
    std::shared_ptr<game::ItemLuaAdapter> item_adapter,
    std::shared_ptr<game::LootLuaAdapter> loot_adapter,
    std::shared_ptr<game::AuctionLuaAdapter> auction_adapter,
    std::shared_ptr<game::MailLuaAdapter> mail_adapter,
    std::shared_ptr<game::MerchantLuaAdapter> merchant_adapter,
    std::shared_ptr<game::TradeLuaAdapter> trade_adapter) {
  std::vector<lua::NativeBindingCatalog> sources;
  AppendCatalog(
      sources, game::runtime::WorldLuaRuntime::FrameNativeBindingCatalog());
  AppendCatalog(sources, game::AchievementNativeBindingCatalog());
  AppendCatalog(sources, game::ActionNativeBindingCatalog());
  AppendCatalog(sources, game::AuctionNativeBindingCatalog(
                              std::move(auction_adapter)));
  AppendCatalog(sources, game::CameraNativeBindingCatalog());
  AppendCatalog(sources, game::CalendarNativeBindingCatalog());
  AppendCatalog(sources, game::ChatNativeBindingCatalog());
  AppendCatalog(sources, game::CombatNativeBindingCatalog());
  AppendCatalog(sources, game::FactionNativeBindingCatalog());
  AppendCatalog(sources, game::GlyphNativeBindingCatalog());
  AppendCatalog(sources, game::GossipNativeBindingCatalog());
  AppendCatalog(sources, game::GuildNativeBindingCatalog());
  AppendCatalog(sources, game::InstanceNativeBindingCatalog());
  AppendCatalog(sources, game::ItemNativeBindingCatalog(
                              std::move(item_adapter)));
  AppendCatalog(sources, game::LootNativeBindingCatalog(
                              std::move(loot_adapter)));
  AppendCatalog(sources, game::MailNativeBindingCatalog(
                              std::move(mail_adapter)));
  AppendCatalog(sources, game::MapNativeBindingCatalog());
  AppendCatalog(sources, game::MerchantNativeBindingCatalog(
                              std::move(merchant_adapter)));
  AppendCatalog(sources, game::MovementNativeBindingCatalog());
  AppendCatalog(sources, game::PetNativeBindingCatalog());
  AppendCatalog(sources, game::PetitionNativeBindingCatalog());
  AppendCatalog(sources, game::QuestNativeBindingCatalog());
  AppendCatalog(sources, game::SecurityNativeBindingCatalog());
  AppendCatalog(sources, game::SocialNativeBindingCatalog());
  AppendCatalog(sources, game::SocketNativeBindingCatalog());
  AppendCatalog(sources, game::SpellNativeBindingCatalog());
  AppendCatalog(sources, game::TalentNativeBindingCatalog());
  AppendCatalog(sources, game::TaxiNativeBindingCatalog());
  AppendCatalog(sources, game::TrackingNativeBindingCatalog());
  AppendCatalog(sources, game::TradeNativeBindingCatalog(
                              std::move(trade_adapter)));
  AppendCatalog(sources, game::TradeSkillNativeBindingCatalog());
  AppendCatalog(sources, game::UnitNativeBindingCatalog());
  AppendCatalog(sources, game::VehicleNativeBindingCatalog());
  AppendCatalog(
      sources,
      audio::integration::lua::SharedSoundVoiceChatNativeBindingCatalog());
  AppendCatalog(
      sources,
      audio::integration::lua::WorldMusicVoiceChatNativeBindingCatalog());
  AppendCatalog(
      sources,
      display::lua_adapter::SharedDisplaySettingsNativeBindingCatalog(
          display_settings.service(), display_settings.frame_scale()));
  AppendCatalog(sources, game::detail::KnowledgeBaseNativeBindingCatalog());
  AppendCatalogs(sources, game::detail::FrameXmlCoreNativeBindingSources());
  AppendCatalog(sources, game::detail::FrameXmlClientNativeBindings());
  AppendCatalog(sources, game::detail::FrameXmlGameplayNativeBindings());
  AppendCatalog(sources, game::detail::CombatLogConstantCatalog());
  AppendCatalog(sources, game::detail::GlyphConstantCatalog());
  AppendCatalog(
      sources,
      ::openwow::game::actions::macros::adapters::lua::
          MacroConstantCatalog());
  AppendCatalog(sources, game::detail::SpellBookConstantCatalog());
  AppendCatalog(sources, game::detail::SpellPowerConstantCatalog());
  AppendCatalog(sources, game::detail::TotemConstantCatalog());

  auto plan =
      lua::ComposeRetailWorldNativeBindings(std::move(sources));
  auto shared_type_modules =
      lua::modules::SharedFrameScriptTypeModules();
  auto world_widget_subtype_modules =
      lua::modules::WorldWidgetSubtypeModules();
  std::vector<lua::NativeBindingCatalog> catalogs;
  catalogs.reserve(plan.before_frame_script_types.size() +
                   shared_type_modules.size() +
                   world_widget_subtype_modules.size() +
                   plan.after_frame_script_types.size());
  AppendCatalogs(catalogs, std::move(plan.before_frame_script_types));
  AppendCatalogs(catalogs, std::move(shared_type_modules));
  AppendCatalogs(catalogs, std::move(world_widget_subtype_modules));
  AppendCatalogs(catalogs, std::move(plan.after_frame_script_types));
  return catalogs;
}

}

std::unique_ptr<lua::LuaBindingComposition>
CreateProductionGlueLuaBindings(
    display::ProductionDisplaySettingsRuntime& display_settings) {
  return std::make_unique<lua::LuaBindingComposition>(
      lua::FrameScriptScope::kGlue,
      GlueNativeModuleCatalogs(display_settings));
}

ProductionWorldLuaAdapters CreateProductionWorldLuaAdapters() {
  return {
      .item = std::make_shared<game::ItemLuaAdapter>(),
      .loot = std::make_shared<game::LootLuaAdapter>(),
      .auction = std::make_shared<game::AuctionLuaAdapter>(),
      .mail = std::make_shared<game::MailLuaAdapter>(),
      .merchant = std::make_shared<game::MerchantLuaAdapter>(),
      .trade = std::make_shared<game::TradeLuaAdapter>(),
  };
}

void BindProductionWorldLuaAdapters(
    ProductionWorldLuaAdapters& adapters,
    openwow::game::WorldSession& session,
    openwow::game::CursorSurface& cursor,
    game::EventDispatcher& events,
    openwow::game::MailStationeryChoices& stationery) {
  stationery.Prime(
      session.GetDbcLoader(), session.query_cache(),
      session.item_definitions(), session.inventory_replica());
  adapters.mail->Bind({
      .mail = session.mail(),
      .interaction = session.interaction(),
      .world_session = session,
      .inventory = session.inventory_replica(),
      .queries = session.query_cache(),
      .social = session.social(),
      .items = session.item_definitions(),
      .item_interactions = session.item_interactions(),
      .localization = openwow::game::Localization::Get(),
      .stationery = stationery,
      .held_cursor = session.held_cursor(),
      .dbc = session.GetDbcLoader(),
      .item_requirements = session.item_use_requirement_sources(),
      .session_state = session.session(),
      .cvars = game::CVarSystem::Instance(),
      .events = game::ScriptEventDispatch::Get(),
  });
  adapters.trade->Bind({
      .trade = session.trade(),
      .interaction = session.interaction(),
      .world_session = session,
      .inventory = session.inventory_replica(),
      .items = session.item_definitions(),
      .localization = openwow::game::Localization::Get(),
      .queries = session.query_cache(),
      .item_interactions = session.item_interactions(),
      .spells = session.spells(),
      .item_requirements = session.item_use_requirement_sources(),
      .session_state = session.session(),
      .play_time = session.misc(),
      .group = openwow::game::GroupSystem::Get(),
      .battlefield = openwow::game::BattlefieldInfo::Get(),
      .instance = session.instance(),
      .events = game::ScriptEventDispatch::Get(),
      .security = game::SecureExecution::Get(),
      .ui_errors = openwow::ui::UIErrorManager::Get(),
      .cursor = session.held_cursor(),
      .dbc = session.GetDbcLoader(),
  });
  adapters.auction->Bind({
      .auction = session.auction(),
      .packets = session.auction_packets(),
      .dbc = session.GetDbcLoader(),
      .held_cursor = session.held_cursor(),
      .interaction = session.interaction(),
      .items = session.item_definitions(),
      .world_session = session,
      .inventory = session.inventory_replica(),
      .queries = session.query_cache(),
      .item_requirements = session.item_use_requirement_sources(),
      .session_state = session.session(),
      .events = game::ScriptEventDispatch::Get(),
      .security = game::SecureExecution::Get(),
  });
  adapters.item->Bind({
      .equipment = session.equipment(),
      .cooldowns = openwow::game::CooldownTracker::Get(),
      .cursor = cursor,
      .dbc = session.GetDbcLoader(),
      .events = events,
      .guild = openwow::game::GuildSystem::Get(),
      .held_cursor = session.held_cursor(),
      .interaction = session.interaction(),
      .inventory = session.inventory_replica(),
      .items = session.item_definitions(),
      .item_interactions = session.item_interactions(),
      .localization = openwow::game::Localization::Get(),
      .queries = session.query_cache(),
      .reputation = openwow::game::ReputationInfo::Get(),
      .spell_book = session.spell_book(),
      .spells = session.spells(),
      .trade = session.trade(),
      .world_session = session,
      .session_state = session.session(),
      .arena = session.arena(),
      .group = openwow::game::GroupSystem::Get(),
      .battlefield = openwow::game::BattlefieldInfo::Get(),
      .instance = session.instance(),
      .gossip = session.gossip(),
      .quests = session.quests(),
      .play_time = session.misc(),
      .script_events = game::ScriptEventDispatch::Get(),
      .security = game::SecureExecution::Get(),
      .ui_errors = openwow::ui::UIErrorManager::Get(),
  });
  adapters.loot->Bind({
      .dbc = session.GetDbcLoader(),
      .group = openwow::game::GroupSystem::Get(),
      .interaction = session.interaction(),
      .items = session.item_definitions(),
      .loot = session.loot(),
      .queries = session.query_cache(),
      .inventory = session.inventory_replica(),
      .localization = openwow::game::Localization::Get(),
      .events = game::ScriptEventDispatch::Get(),
      .targeting = session.targeting_system(),
      .world_session = session,
  });
  adapters.merchant->Bind({
      .dbc = session.GetDbcLoader(),
      .gossip = session.gossip(),
      .held_cursor = session.held_cursor(),
      .cursor = &cursor,
      .interaction = session.interaction(),
      .item_interactions = session.item_interactions(),
      .loot = session.loot(),
      .localization = openwow::game::Localization::Get(),
      .arena_team_query = session.merchant_arena_team_query(),
      .inventory = session.inventory_replica(),
      .queries = session.query_cache(),
      .world_session = session,
      .play_time = session.misc(),
      .item_requirements = session.item_use_requirement_sources(),
      .session_state = session.session(),
      .reputation = openwow::game::ReputationInfo::Get(),
      .events = game::ScriptEventDispatch::Get(),
      .tooltip = game::TooltipSystem::Get(),
  });
}

std::unique_ptr<lua::LuaBindingComposition>
CreateProductionWorldLuaBindings(
    display::ProductionDisplaySettingsRuntime& display_settings,
    std::shared_ptr<game::ItemLuaAdapter> item_adapter,
    std::shared_ptr<game::LootLuaAdapter> loot_adapter,
    std::shared_ptr<game::AuctionLuaAdapter> auction_adapter,
    std::shared_ptr<game::MailLuaAdapter> mail_adapter,
    std::shared_ptr<game::MerchantLuaAdapter> merchant_adapter,
    std::shared_ptr<game::TradeLuaAdapter> trade_adapter) {
  return std::make_unique<lua::LuaBindingComposition>(
      lua::FrameScriptScope::kWorld,
      WorldNativeModuleCatalogs(
          display_settings, std::move(item_adapter), std::move(loot_adapter),
          std::move(auction_adapter), std::move(mail_adapter),
          std::move(merchant_adapter), std::move(trade_adapter)));
}

bool InitializeProductionGlueLuaVm(lua::LuaVm& vm) {
  OpenFrameScriptRetailLibraries(vm.state());
  InstallLuaBaseOverrides(vm.state());
  RegisterFrameScriptStandardGlobals(vm.state(), FrameScriptGlobalProfile::Glue);
  game::SecureExecution::RegisterLuaBindings(
      vm.state(), game::SecureLuaBindingProfile::Glue);
  return true;
}

bool InitializeProductionWorldLuaVm(lua::LuaVm& vm) {
  OpenFrameScriptRetailLibraries(vm.state());
  InstallLuaBaseOverrides(vm.state());
  game::RegisterStandardWowGlobals(vm.state());
  return true;
}

}
