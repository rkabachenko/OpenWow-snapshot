#include "openwow/game/commerce/mail/adapters/lua/mail_lua_bindings.h"
#include "openwow/game/commerce/mail/adapters/lua/mail_lua_api.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/runtime/lua/lua_composition.h"
#include "openwow/game/commerce/mail/adapters/lua/mail_lua_adapter.h"
#include "openwow/game/chat_display.h"
#include "openwow/game/inventory/adapters/ui/item_cursor_pickup_controller.h"
#include "openwow/game/inventory/items/item_use_requirements.h"
#include "openwow/game/localization.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/spell_text_formatter.h"
#include "openwow/game/session_handler.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/game_ui_core.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"

extern "C" {
#include <lua.hpp>
}

#include <array>
#include <memory>
#include <utility>
namespace openwow::ui::game {

using namespace detail;

namespace {

constexpr openwow::ui::LuaGlobalBinding kMailLuaBindings[] = {
    {"CloseMail", LuaCloseMail},
    {"ClearSendMail", LuaClearSendMail},
    {"ClickSendMailItemButton", LuaClickSendMailItemButton},
    {"SetSendMailMoney", LuaSetSendMailMoney},
    {"GetSendMailMoney", LuaGetSendMailMoney},
    {"SetSendMailCOD", LuaSetSendMailCOD},
    {"GetSendMailCOD", LuaGetSendMailCOD},
    {"GetNumStationeries", LuaGetNumStationeries},
    {"GetStationeryInfo", LuaGetStationeryInfo},
    {"SelectStationery", LuaSelectStationery},
    {"GetSelectedStationeryTexture", LuaGetSelectedStationeryTexture},
    {"GetNumPackages", LuaGetNumPackages},
    {"GetPackageInfo", LuaGetPackageInfo},
    {"SelectPackage", LuaSelectPackage},
    {"GetSendMailItem", LuaGetSendMailItem},
    {"GetSendMailItemLink", LuaGetSendMailItemLink},
    {"GetSendMailPrice", LuaGetSendMailPrice},
    {"SendMail", LuaSendMail},
    {"CheckInbox", LuaCheckInbox},
    {"GetInboxNumItems", LuaGetInboxNumItems},
    {"GetInboxHeaderInfo", LuaGetInboxHeaderInfo},
    {"GetInboxText", LuaGetInboxText},
    {"GetInboxInvoiceInfo", LuaGetInboxInvoiceInfo},
    {"GetInboxItem", LuaGetInboxItem},
    {"GetInboxItemLink", LuaGetInboxItemLink},
    {"TakeInboxMoney", LuaTakeInboxMoney},
    {"TakeInboxItem", LuaTakeInboxItem},
    {"TakeInboxTextItem", LuaTakeInboxTextItem},
    {"ReturnInboxItem", LuaReturnInboxItem},
    {"DeleteInboxItem", LuaDeleteInboxItem},
    {"InboxItemCanDelete", LuaInboxItemCanDelete},
    {"HasNewMail", LuaHasNewMail},
    {"ComplainInboxItem", LuaComplainInboxItem},

    {"CanComplainInboxItem", LuaInboxItemCanComplain},
    {"GetLatestThreeSenders", LuaGetLatestThreeSenders},
    {"SetSendMailShowing", LuaSetSendMailShowing},
    {"AutoLootMailItem", LuaAutoLootMailItem},
    {"RespondMailLockSendItem", LuaRespondMailLockSendItem},
};

}

openwow::ui::lua::NativeBindingCatalog MailNativeBindingCatalog(
    std::shared_ptr<MailLuaAdapter> adapter) {
  auto catalog = openwow::ui::lua::NativeFunctionCatalog(
      "game.commerce.mail", openwow::ui::lua::BindingScope::kWorld, kMailLuaBindings);
  catalog.lifecycle_context = std::move(adapter);
  return catalog;
}

void MailLuaAdapter::Bind(Dependencies dependencies) {
  dependencies_.emplace(dependencies);
}
bool MailLuaAdapter::bound() const noexcept {
  return dependencies_.has_value();
}
const MailLuaAdapter::Dependencies& MailLuaAdapter::deps() const {
  return *dependencies_;
}
openwow::game::MailInteraction& MailLuaAdapter::mail() const {
  return deps().mail;
}
openwow::game::InteractionSender& MailLuaAdapter::interaction() const {
  return deps().interaction;
}
openwow::game::ObjectManager& MailLuaAdapter::objects() const {
  return deps().world_session.objects();
}
openwow::game::PlayerInventoryReplica& MailLuaAdapter::inventory() const {
  return deps().inventory;
}
openwow::game::QueryCache& MailLuaAdapter::queries() const {
  return deps().queries;
}
openwow::game::SocialManager& MailLuaAdapter::social() const {
  return deps().social;
}
openwow::game::ItemDefinitions& MailLuaAdapter::items() const {
  return deps().items;
}
openwow::game::ItemInteractionSession& MailLuaAdapter::item_interactions() const {
  return deps().item_interactions;
}
openwow::game::Localization& MailLuaAdapter::localization() const {
  return deps().localization;
}
openwow::game::MailStationeryChoices& MailLuaAdapter::stationery() const {
  return deps().stationery;
}
openwow::game::actions::held_cursor::HeldCursor* MailLuaAdapter::held_cursor() const noexcept {
  return deps().held_cursor;
}
const openwow::data::dbc::DbcLoader* MailLuaAdapter::dbc() const noexcept {
  return deps().dbc;
}
bool MailLuaAdapter::MeetsItemRequirements(
    const openwow::game::CGPlayer_C& player,
    const openwow::game::ItemUseRequirementView& requirements) const {
  return openwow::game::PlayerMeetsItemUseRequirements(
      player, requirements, deps().item_requirements,
      deps().session_state.GetProficiencyMask(
          static_cast<std::uint8_t>(requirements.item_class)));
}
std::string MailLuaAdapter::Localize(
    const std::string_view key, const std::string_view fallback) const {
  return localization().GetString(std::string(key), std::string(fallback));
}
std::string MailLuaAdapter::Format(
    const std::string_view format, std::vector<std::string> arguments) const {
  return localization().FormatString(std::string(format), arguments);
}
bool MailLuaAdapter::HasLocalization(const std::string_view key) const {
  return localization().HasString(std::string(key));
}
std::string MailLuaAdapter::ExpandBody(const std::string_view text) const {
  std::array<char, 4096> expanded{};
  openwow::game::BindSpellTextFormatterDbcLoader(dbc());
  openwow::game::SpellTextFormatter::ExpandObjectTextVariables(
      std::string(text).c_str(), expanded.data(), expanded.size(),
      objects().GetActivePlayerGuid().GetRawValue(), nullptr, 0);
  return expanded[0] != '\0' ? std::string(expanded.data())
                             : std::string(text);
}
void MailLuaAdapter::FilterMatureLanguage(std::string& text) const {
  openwow::game::BindChatDisplayDbcLoader(dbc());
  (void)openwow::game::ChatFrame_MatureLanguageFilter(text, false);
}
bool MailLuaAdapter::UseLongTimeFormat() const {
  return deps().cvars.GetCVarBool("timeMgrUseMilitaryTime");
}
void MailLuaAdapter::ShowSystemMessage(const int message) const {
  DisplaySystemMessage(message);
}
void MailLuaAdapter::ShowSystemText(const std::string_view message) const {
  const std::string text(message);
  openwow::game::ChatFrame_DisplayMessage(
      objects(), text.c_str(), openwow::game::ChatDisplayType::kSystem, nullptr, 0,
      nullptr, nullptr, nullptr, 0, 0, 0, 0, 0, nullptr);
}
void MailLuaAdapter::Present(
    const MailLuaEvent event, const int value, const std::string_view text) const {
  switch (event) {
    case MailLuaEvent::kSendInfoChanged:
      deps().events.FireEvent(events::MAIL_SEND_INFO_UPDATE);
      break;
    case MailLuaEvent::kSendMoneyChanged:
      deps().events.FireEvent(events::SEND_MAIL_MONEY_CHANGED);
      break;
    case MailLuaEvent::kSendCodChanged:
      deps().events.FireEvent(events::SEND_MAIL_COD_CHANGED);
      break;
    case MailLuaEvent::kSendSucceeded:
      deps().events.FireMailSendSuccess();
      break;
    case MailLuaEvent::kLockSendItems:
      deps().events.FireEventArgs(
          events::MAIL_LOCK_SEND_ITEMS, {value, std::string(text)});
      break;
    case MailLuaEvent::kUnlockSendItems:
      deps().events.FireEvent(events::MAIL_UNLOCK_SEND_ITEMS);
      break;
    case MailLuaEvent::kInboxChanged:
      deps().events.FireMailInboxUpdate();
      break;
    case MailLuaEvent::kPendingMailChanged:
      deps().events.FireUpdatePendingMail();
      break;
    case MailLuaEvent::kMailClosed:
      deps().events.FireMailClosed();
      break;
  }
}
void MailLuaAdapter::EnterItemMouseover(const std::uint64_t guid) const {
  GameUI_OnMouseoverUnitEnter(guid);
}
void MailLuaAdapter::LeaveItemMouseover(const std::uint64_t guid) const {
  GameUI_OnMouseoverUnitLeave(guid);
}
void MailLuaAdapter::PickupAttachment(
    const std::uint64_t guid, const int slot) const {
  (void)slot;
  auto* cursor = held_cursor();
  if (cursor == nullptr) return;
  (void)openwow::game::inventory::ui::PickupItemCursor(
      *cursor, inventory(), items(), dbc(), objects().GetActivePlayerGuid(),
      openwow::game::ObjectGuid(guid));
}
MailLuaAdapter* TryGetMailLuaAdapter(lua_State* state) {
  auto* adapter = static_cast<MailLuaAdapter*>(
      openwow::ui::lua::detail::ActiveBindingAdapter(state));
  if (adapter == nullptr) {
    adapter = static_cast<MailLuaAdapter*>(
        openwow::ui::lua::detail::GlobalBindingAdapter(state, "CloseMail"));
  }
  return (adapter != nullptr && adapter->bound()) ? adapter : nullptr;
}

MailLuaAdapter& RequireMailLuaAdapter(lua_State* state) {
  auto* adapter = static_cast<MailLuaAdapter*>(
      openwow::ui::lua::detail::ActiveBindingAdapter(state));
  if (adapter == nullptr) {

    adapter = static_cast<MailLuaAdapter*>(
        openwow::ui::lua::detail::GlobalBindingAdapter(state, "CloseMail"));
  }
  if (!adapter || !adapter->bound()) luaL_error(state, "mail Lua API is not bound");
  return *adapter;
}
}
