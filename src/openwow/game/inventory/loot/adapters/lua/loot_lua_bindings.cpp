#include "openwow/game/inventory/loot/adapters/lua/loot_lua_bindings.h"

#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/runtime/lua/lua_composition.h"
#include "openwow/game/inventory/loot/adapters/lua/loot_lua_api.h"
#include "openwow/game/inventory/loot/adapters/lua/loot_lua_adapter.h"
#include "openwow/game/game_misc_utils.h"
#include "openwow/game/chat_display.h"
#include "openwow/game/group_system.h"
#include "openwow/game/interaction_sender.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/items/item_acquisition_rules.h"
#include "openwow/game/inventory/loot/loot_interaction.h"
#include "openwow/game/localization.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/objects/cgitem.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/objects/cggameobject.h"
#include "openwow/game/targeting.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/game_ui_core.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"

extern "C" {
#include <lua.hpp>
}

#include <memory>
#include <algorithm>
#include <charconv>
#include <cctype>
#include <utility>

namespace openwow::ui::game {

using namespace detail;

namespace {

std::string Lower(const std::string_view text) {
  std::string result(text);
  std::transform(
      result.begin(), result.end(), result.begin(),
      [](const unsigned char value) {
        return static_cast<char>(std::tolower(value));
      });
  return result;
}

bool NameMatches(
    const std::string_view candidate, const std::string_view requested,
    const bool exact) {
  const auto name = Lower(candidate);
  const auto query = Lower(requested);
  return exact ? name == query : name.starts_with(query);
}

std::optional<std::uint64_t> ParseGuid(const std::string_view value) {
  std::uint64_t guid = 0;
  const int base = value.starts_with("0x") || value.starts_with("0X") ? 16 : 10;
  const auto digits = base == 16 ? value.substr(2) : value;
  const auto [end, error] =
      std::from_chars(digits.data(), digits.data() + digits.size(), guid, base);
  return error == std::errc{} && end == digits.data() + digits.size() &&
                 guid != 0
             ? std::optional<std::uint64_t>(guid)
             : std::nullopt;
}

std::optional<int> GroupTokenIndex(
    const std::string_view token, const std::string_view prefix,
    const int maximum) {
  if (!token.starts_with(prefix)) return std::nullopt;
  int index = 0;
  const auto digits = token.substr(prefix.size());
  const auto [end, error] =
      std::from_chars(digits.data(), digits.data() + digits.size(), index);
  return error == std::errc{} && end == digits.data() + digits.size() &&
                 index >= 1 && index <= maximum
             ? std::optional<int>(index)
             : std::nullopt;
}

constexpr openwow::ui::LuaGlobalBinding kLootLuaBindings[] = {
    {"SetLootPortrait", LuaSetLootPortrait},
    {"GetNumLootItems", LuaGetNumLootItems},
    {"GetLootSlotInfo", LuaGetLootSlotInfo},
    {"GetLootSlotLink", LuaGetLootSlotLink},
    {"LootSlot", LuaLootSlot},
    {"LootSlotIsItem", LuaLootSlotIsItem},
    {"LootSlotIsCoin", LuaLootSlotIsCoin},
    {"ConfirmLootSlot", LuaConfirmLootSlot},
    {"CloseLoot", LuaCloseLoot},
    {"IsFishingLoot", LuaIsFishingLoot},
    {"GetLootMethod", LuaGetLootMethod},
    {"SetLootMethod", LuaSetLootMethod},
    {"GetLootThreshold", LuaGetLootThreshold},
    {"SetLootThreshold", LuaSetLootThreshold},
    {"ConfirmLootRoll", LuaConfirmLootRoll},
    {"GetLootRollItemInfo", LuaGetLootRollItemInfo},
    {"GetLootRollItemLink", LuaGetLootRollItemLink},
    {"GetLootRollTimeLeft", LuaGetLootRollTimeLeft},
    {"RollOnLoot", LuaRollOnLoot},
    {"ConfirmBindOnUse", LuaConfirmBindOnUse},
    {"GetMasterLootCandidate", LuaGetMasterLootCandidate},
    {"GetOptOutOfLoot", LuaGetOptOutOfLoot},
    {"GiveMasterLoot", LuaGiveMasterLoot},
    {"SetOptOutOfLoot", LuaSetOptOutOfLoot},
};

}

openwow::ui::lua::NativeBindingCatalog LootNativeBindingCatalog(
    std::shared_ptr<LootLuaAdapter> adapter) {
  auto catalog = openwow::ui::lua::NativeFunctionCatalog(
      "game.inventory.loot", openwow::ui::lua::BindingScope::kWorld, kLootLuaBindings);
  catalog.lifecycle_context = std::move(adapter);
  return catalog;
}

void LootLuaAdapter::Bind(Dependencies dependencies) {
  dependencies_.emplace(dependencies);
}

bool LootLuaAdapter::bound() const noexcept {
  return dependencies_.has_value();
}

const LootLuaAdapter::Dependencies& LootLuaAdapter::deps() const {
  return *dependencies_;
}

const openwow::data::dbc::DbcLoader* LootLuaAdapter::dbc() const noexcept {
  return deps().dbc;
}

openwow::game::GroupSystem& LootLuaAdapter::group() const {
  return deps().group;
}

openwow::game::InteractionSender& LootLuaAdapter::interaction() const {
  return deps().interaction;
}

openwow::game::ItemDefinitions& LootLuaAdapter::item_definitions() const {
  return deps().items;
}

openwow::game::LootInteraction& LootLuaAdapter::loot() const {
  return deps().loot;
}

openwow::game::ObjectManager& LootLuaAdapter::objects() const {
  return deps().world_session.objects();
}

openwow::game::QueryCache& LootLuaAdapter::query_cache() const {
  return deps().queries;
}

std::string LootLuaAdapter::Localize(
    const std::string_view key, const std::string_view fallback) const {
  return deps().localization.GetString(
      std::string(key), std::string(fallback));
}

std::string LootLuaAdapter::Format(
    const std::string_view format,
    std::vector<std::string> arguments) const {
  return deps().localization.FormatString(
      std::string(format), arguments);
}

void LootLuaAdapter::Present(
    const LootLuaEvent event, const int first, const int second) const {
  switch (event) {
    case LootLuaEvent::kSlotCleared:
      deps().events.FireLootSlotCleared(first);
      break;
    case LootLuaEvent::kClosed:
      deps().events.FireLootClosed();
      break;
    case LootLuaEvent::kOpenMasterList:
      deps().events.FireOpenMasterLootList();
      break;
    case LootLuaEvent::kBindConfirm:
      deps().events.FireLootBindConfirm(first);
      break;
    case LootLuaEvent::kConfirmRoll:
      deps().events.FireConfirmLootRoll(first, second);
      break;
    case LootLuaEvent::kConfirmDisenchant:
      deps().events.FireConfirmDisenchantRoll(first, second);
      break;
    case LootLuaEvent::kCancelRoll:
      deps().events.FireCancelLootRoll(first);
      break;
    case LootLuaEvent::kStartRoll:
      deps().events.FireStartLootRoll(first, second);
      break;
  }
}

void LootLuaAdapter::CloseActiveLoot(const bool show_interrupted) const {

  const openwow::game::ObjectGuid release_guid =
      loot().TakePendingReleaseGuid();
  if (release_guid.IsEmpty()) return;

  if (show_interrupted) DisplaySystemMessage(144);
  if (auto* object = objects().GetMutableGameObject(release_guid);
      object != nullptr) {
    object->ApplyTransientGoStateByte(
        static_cast<std::uint8_t>(openwow::game::GOState::Ready));
  } else if (objects().GetUnit(release_guid) != nullptr) {
    GameUI_OnMouseoverUnitLeave(release_guid.GetRawValue());
  }
  if (auto* player = objects().GetActivePlayer(); player != nullptr) {
    player->Animation().ClearStandSelectionInteractionTargetAndRefresh(deps().world_session);
  }
  interaction().SendLootRelease(release_guid.GetRawValue());
  if (loot().is_looting()) {
    loot().CloseLootWindow();
    loot().state().ClearPendingConfirmSlot();
    deps().events.FireLootClosed();
  }
  if (const auto* source = objects().GetUnit(release_guid);
      deps().targeting != nullptr && source != nullptr &&
      source->State().GetHealth() == 0) {
    deps().targeting->ClearTarget(release_guid.GetRawValue(), true);
  }
}

bool LootLuaAdapter::CanPromptBind(
    const openwow::game::ItemTemplate& item, const std::uint32_t count) const {
  const auto result = openwow::game::EvaluateItemAcquisition(
      deps().inventory, item_definitions(), deps().dbc, item, count);
  if (result.allowed()) return true;
  if (result.failure ==
      openwow::game::ItemAcquisitionFailure::kUniqueItemLimit) {
    DisplaySystemMessage(20);
  } else {
    DisplaySystemMessage(626, result.limit, result.category.c_str());
  }
  return false;
}

std::optional<std::uint64_t> LootLuaAdapter::ResolveMasterLooter(
    lua_State* state, const std::string_view token,
    const bool exact_match) const {
  (void)state;
  if (const auto guid = ParseGuid(token); guid.has_value()) return guid;
  const auto lower = Lower(token);
  if (lower == "player") {
    const auto guid = objects().GetActivePlayerGuid().GetRawValue();
    return guid != 0 ? std::optional<std::uint64_t>(guid) : std::nullopt;
  }
  if (const auto index = GroupTokenIndex(lower, "party", 4);
      index.has_value()) {
    const auto guid = group().GetTrackedPartyMemberGuid(*index - 1);
    return guid != 0 ? std::optional<std::uint64_t>(guid) : std::nullopt;
  }
  if (const auto index = GroupTokenIndex(lower, "raid", 40);
      index.has_value()) {
    const auto* member = group().GetMember(*index - 1);
    return member != nullptr && member->guid != 0
               ? std::optional<std::uint64_t>(member->guid)
               : std::nullopt;
  }
  const bool full_match =
      exact_match || token.find('-') != std::string_view::npos;
  const auto active = objects().GetActivePlayerGuid();
  if (!active.IsEmpty() &&
      NameMatches(objects().GetPlayerName(active), token, full_match)) {
    return active.GetRawValue();
  }
  for (std::size_t index = 0; index < group().GetNumGroupMembers(); ++index) {
    const auto* member = group().GetMember(index);
    if (member != nullptr && member->guid != 0 &&
        NameMatches(member->name, token, full_match)) {
      return member->guid;
    }
  }
  return std::nullopt;
}

void LootLuaAdapter::ApplyOptOut(const bool enabled) const {
  loot().state().SetOptOut(enabled);
  interaction().SendOptOutOfLoot(enabled);
  const auto key = enabled ? "OPT_OUT_LOOT_TOGGLE_ON"
                           : "OPT_OUT_LOOT_TOGGLE_OFF";
  const auto message = deps().localization.GetString(key, key);
  openwow::game::ChatFrame_DisplayMessage(
      objects(), message.c_str(), openwow::game::ChatDisplayType::kSystem, nullptr, 0,
      nullptr, nullptr, nullptr, 0, 0, 0, 0, 0, nullptr);
}

void LootLuaAdapter::RequestCandidateName(const std::uint64_t guid) const {
  if (guid == 0 || query_cache().GetPlayerName(guid) != nullptr ||
      !objects().GetPlayerName(openwow::game::ObjectGuid(guid)).empty()) {
    return;
  }
  (void)query_cache().RequestNameQuery(guid);
}

void LootLuaAdapter::ShowSystemMessage(
    const int message, const std::string_view token) const {
  if (token.empty()) {
    DisplaySystemMessage(message);
  } else {
    const std::string text(token);
    DisplaySystemMessage(message, text.c_str());
  }
}

LootLuaAdapter& RequireLootLuaAdapter(lua_State* state) {
  auto* adapter = static_cast<LootLuaAdapter*>(
      openwow::ui::lua::detail::ActiveBindingAdapter(state));
  if (adapter == nullptr) {

    adapter = static_cast<LootLuaAdapter*>(
        openwow::ui::lua::detail::GlobalBindingAdapter(state, "GetNumLootItems"));
  }
  if (adapter == nullptr || !adapter->bound()) {
    luaL_error(state, "loot Lua API is not bound");
  }
  return *adapter;
}

}
