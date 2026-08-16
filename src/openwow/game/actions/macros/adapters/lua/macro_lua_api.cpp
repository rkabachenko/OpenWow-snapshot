
#include "openwow/game/actions/macros/adapters/lua/macro_lua_api.h"
#include "openwow/ui/game/loot_tooltip_support.h"
#include "openwow/game/action_validation_utils.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/items/item_icon_resolver.h"
#include "openwow/game/hyperlink.h"
#include "openwow/game/actions/macros/application/macro_catalog.h"
#include "openwow/game/actions/macros/adapters/retail/macro_input_adapter.h"
#include "openwow/game/spells/adapters/lua/spell_name_dispatch.h"
#include "openwow/game/spell_query_bridge.h"
#include "openwow/game/unit_query_bridge.h"
#include "openwow/core/decimal_parse.h"
#include "openwow/game/world_session.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/lua_numeric.h"
#include "openwow/ui/runtime/lua/lua_composition.h"
#include "openwow/ui/script_boolean.h"
#include "openwow/ui/game/lua_mouse_button_context.h"

#include <array>
#include <utility>
#include <cstring>
#include <sstream>
#include <string>
#include <string_view>

extern "C" {
#include <lua.hpp>
}

namespace openwow::game::actions::macros::adapters::lua {

namespace {

constexpr char kWorldSessionRegistryKey[] = "openwow.world_session";
constexpr char kDbcLoaderRegistryKey[] = "openwow.dbc_loader";
constexpr char kSpellQueryBridgeRegistryKey[] =
    "openwow.spell_query_bridge";

::openwow::game::WorldSession* GetWorldSession(lua_State* state) {
  lua_getfield(state, LUA_REGISTRYINDEX, kWorldSessionRegistryKey);
  auto* session = static_cast<::openwow::game::WorldSession*>(
      lua_touserdata(state, -1));
  lua_pop(state, 1);
  return session;
}

::openwow::game::MacroCatalog* GetMacroCatalog(lua_State* state) {
  auto* session = GetWorldSession(state);
  return session != nullptr ? &session->macros() : nullptr;
}

const ::openwow::data::dbc::DbcLoader* GetDbcLoader(lua_State* state) {
  lua_getfield(state, LUA_REGISTRYINDEX, kDbcLoaderRegistryKey);
  auto* loader = static_cast<const ::openwow::data::dbc::DbcLoader*>(
      lua_touserdata(state, -1));
  lua_pop(state, 1);
  if (loader != nullptr) {
    return loader;
  }
  const auto* session = GetWorldSession(state);
  return session != nullptr ? session->GetDbcLoader() : nullptr;
}

std::string SafeLuaString(lua_State* state, const int index) {
  const char* value = lua_tostring(state, index);
  return value != nullptr ? std::string(value) : std::string{};
}

::openwow::game::ObjectGuid ResolveUnitId(
    ::openwow::game::WorldSession* session,
    const std::string& unit_id) {
  return ::openwow::game::UnitQueryBridge::Get().ResolveToGuid(
      session, unit_id);
}

std::string ResolveItemEntryIconTexturePathOrFallback(
    lua_State* state, const std::uint32_t item_entry) {
  std::uint32_t display_id = 0;
  auto* const session = GetWorldSession(state);
  if (session != nullptr) {
    if (const auto* item =
            session->query_cache().GetItemTemplate(item_entry);
        item != nullptr) {
      display_id = item->display_id;
    }
  }
  if (display_id == 0 && session != nullptr) {
    if (const auto* item = session->item_definitions().GetItem(item_entry);
        item != nullptr) {
      display_id = item->display_id;
    }
  }
  if (display_id == 0) {
    return ::openwow::game::BuildItemInventoryIconTexturePath(
        ::openwow::game::kFallbackItemInventoryIconName);
  }
  return ::openwow::game::ResolveItemInventoryIconTexturePath(
      GetDbcLoader(state), display_id);
}

::openwow::game::SpellQueryBridge* GetSpellQueryBridge(lua_State* state) {
  lua_getfield(state, LUA_REGISTRYINDEX, kSpellQueryBridgeRegistryKey);
  auto* queries = static_cast<::openwow::game::SpellQueryBridge*>(
      lua_touserdata(state, -1));
  lua_pop(state, 1);
  return queries;
}

}

static std::optional<::openwow::game::MacroDocument> GetMacroFromLuaArgs(
    lua_State* L, const ::openwow::game::MacroCatalog& macros) {
    if (lua_isnumber(L, 1)) {
        const auto zero_based_index =
            openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1)) - 1u;
        if (zero_based_index >= ::openwow::game::MacroCatalog::kTotalSlots) {
            return std::nullopt;
        }
        return macros.FindMacroAtSlot(zero_based_index);
    } else if (lua_isstring(L, 1)) {
        const char* name = lua_tostring(L, 1);
        if (!name || !name[0]) return std::nullopt;
        return macros.FindMacroByName(name);
    }
    return std::nullopt;
}

void RunMacroByIndex(lua_State* L, std::uint32_t macro_index,
                     const char* button) {
  auto* macros = GetMacroCatalog(L);
  if (macros == nullptr || macro_index == 0) {
    return;
  }

  macros->ExecuteBody(
      macro_index - 1,
      ::openwow::game::actions::macros::adapters::retail::
          DecodeMacroInputButton(button));
}

static constexpr int kMaxGlobalMacros  = 36;
static constexpr int kMaxPerCharMacros = 18;

static std::uint32_t ResolveMacroItemIdArg(lua_State* L, int index) {
  const char* raw_value = lua_tostring(L, index);
  if (raw_value == nullptr || raw_value[0] == '\0') {
    return 0;
  }

  if (const char* payload = std::strstr(raw_value, "item:"); payload != nullptr) {
    return openwow::core::ParseSignedDecimalLikeSub76F0D0(payload + 5);
  }

  auto* session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  const auto* item_template =
      session->query_cache().GetItemTemplateByName(raw_value);
  return item_template != nullptr ? item_template->entry : 0;
}

static std::string ResolveMacroInfoIconTexturePath(
    lua_State* L,
    ::openwow::game::MacroCatalog& macros,
    const ::openwow::game::MacroDocument& macro) {
  if (macro.resolved_item_id != 0 &&
      openwow::text::EqualsIgnoreCaseAscii(
          macro.icon_name, "INV_Misc_QuestionMark")) {
    return ResolveItemEntryIconTexturePathOrFallback(L, macro.resolved_item_id);
  }

  return macros.GetIconPath(macro.id);
}

std::string BuildMacroIconTexturePath(const std::string_view icon_name) {
  std::string path = "Interface\\Icons\\";
  path += icon_name;
  return path;
}

template <typename GetIconPathFn>
int PushMacroIconInfo(lua_State* L,
                      ::openwow::game::MacroCatalog& macros,
                      const char* usage,
                      GetIconPathFn&& get_icon_path) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, usage);
  }

  const auto index = openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1));
  const auto icon_name = get_icon_path(macros, index);
  if (icon_name && !icon_name->empty()) {
    const auto path = BuildMacroIconTexturePath(*icon_name);
    lua_pushstring(L, path.c_str());
  } else {
    lua_pushstring(L, "");
  }
  return 1;
}

static const ::openwow::game::ItemInstance* FindFirstCarriedItemByEntry(
    const ::openwow::game::PlayerInventoryReplica& inventory,
    const std::uint32_t item_entry) {
  return ::openwow::game::FindInventoryItemByEntry(inventory, item_entry);
}

static std::string BuildMacroItemLink(
    const ::openwow::game::ItemTemplate& item_template,
    const std::string& display_name,
    const ::openwow::game::ItemInstance* carried_item,
    const std::uint32_t hyperlink_level) {
  const auto retail_signed_decimal = [](const std::uint32_t value) {
    return std::to_string(openwow::ui::SignedI32FromU32Bits(value));
  };

  struct CarriedItemLinkFields {
    decltype(std::declval<::openwow::game::ItemInstance>()
                 .GetPermanentEnchant()) permanent_enchant{};
    std::array<decltype(std::declval<::openwow::game::ItemInstance>()
                            .GetSocketEnchant(0)), 3> socket_enchants{};
    decltype(::openwow::game::ItemInstance::random_property)
        random_property{};
    decltype(::openwow::game::ItemInstance::random_suffix) random_suffix{};
  };
  CarriedItemLinkFields carried;
  if (carried_item != nullptr) {
    carried = CarriedItemLinkFields{
        .permanent_enchant = carried_item->GetPermanentEnchant(),
        .socket_enchants = {carried_item->GetSocketEnchant(0),
                            carried_item->GetSocketEnchant(1),
                            carried_item->GetSocketEnchant(2)},
        .random_property = carried_item->random_property,
        .random_suffix = carried_item->random_suffix,
    };
  }

  return ::openwow::game::HyperlinkParser::Build(
      "item",
      item_template.entry,
      display_name,
      ::openwow::game::HyperlinkParser::GetQualityColor(
          static_cast<std::uint32_t>(item_template.quality)),
      {
          retail_signed_decimal(carried.permanent_enchant),
          retail_signed_decimal(carried.socket_enchants[0]),
          retail_signed_decimal(carried.socket_enchants[1]),
          retail_signed_decimal(carried.socket_enchants[2]),

          "0",
          retail_signed_decimal(carried.random_property),
          retail_signed_decimal(carried.random_suffix),
          retail_signed_decimal(hyperlink_level),
      });
}

static bool PushSpellNameAndSubtext(lua_State* L, const std::uint32_t spell_id) {
  if (spell_id == 0) {
    return false;
  }

  if (const auto* queries = GetSpellQueryBridge(L); queries != nullptr) {
    if (const auto query = queries->Query(spell_id); query.has_value()) {
      lua_pushstring(L, query->name.c_str());
      lua_pushstring(L, query->subtext.c_str());
      return true;
    }
  }

  const auto* dbc = GetDbcLoader(L);
  if (dbc == nullptr) {
    return false;
  }

  const auto* spell_entry = dbc->spell().LookupEntry(spell_id);
  if (spell_entry == nullptr) {
    return false;
  }

  const std::string spell_name(spell_entry->spell_name);
  const std::string spell_subtext(spell_entry->rank);
  lua_pushstring(L, spell_name.c_str());
  lua_pushstring(L, spell_subtext.c_str());
  return true;
}

int LuaGetNumMacros(lua_State* L) {
  auto* macros = GetMacroCatalog(L);
  lua_pushnumber(L, static_cast<lua_Integer>(
                        macros != nullptr ? macros->GetNumAccountMacros() : 0));
  lua_pushnumber(L, static_cast<lua_Integer>(
                        macros != nullptr ? macros->GetNumCharacterMacros() : 0));
  return 2;
}

int LuaGetMacroInfo(lua_State* L) {
  auto* macros = GetMacroCatalog(L);
  if (macros == nullptr) {
    return 0;
  }
  const auto macro = GetMacroFromLuaArgs(L, *macros);
  if (!macro) {
    return 0;
  }
  lua_pushstring(L, macro->name.c_str());
  const std::string icon_texture =
      ResolveMacroInfoIconTexturePath(L, *macros, *macro);
  if (icon_texture.empty()) {
    lua_pushnil(L);
  } else {
    lua_pushstring(L, icon_texture.c_str());
  }
  lua_pushstring(L, macro->body.c_str());
  return 3;
}

int LuaGetMacroBody(lua_State* L) {
  const auto* macros = GetMacroCatalog(L);
  if (macros == nullptr) {
    return 0;
  }
  const auto macro = GetMacroFromLuaArgs(L, *macros);
  if (!macro) {
    return 0;
  }
  lua_pushstring(L, macro->body.c_str());
  return 1;
}

int LuaCreateMacro(lua_State* L) {
  if (!lua_isstring(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: CreateMacro(name, iconIndex, body, perCharacter)");
  }
  auto* macros = GetMacroCatalog(L);
  if (macros == nullptr ||
      !macros->CanPerform(MacroProtectedOperation::kModifyCatalog)) {
    return 0;
  }

  const char* name = lua_tostring(L, 1);
  if (!name || !name[0]) {
    return luaL_error(L, "CreateMacro() failed, no name specified");
  }

  const auto icon =
      openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 2));
  const char* body = lua_isstring(L, 3) ? lua_tostring(L, 3) : nullptr;
  const bool per_char =
      ::openwow::ui::ScriptReadBoolArgOrDefault(L, 4, false);

  auto& sys = *macros;
  const MacroScope scope =
      per_char ? MacroScope::kCharacter : MacroScope::kAccount;
  const MacroId id =
      sys.CreateMacro(name, icon, body != nullptr ? body : "", scope);
  if (!id.IsValid()) {

    return luaL_error(L, "CreateMacro() failed, already have %d macros", 36);
  }
  lua_pushnumber(L, sys.FindSlotIndex(id) + 1);
  return 1;
}

int LuaEditMacro(lua_State* L) {
  auto* macros = GetMacroCatalog(L);
  if (macros == nullptr ||
      !macros->CanPerform(MacroProtectedOperation::kModifyCatalog)) {
    return 0;
  }
  const auto macro = GetMacroFromLuaArgs(L, *macros);
  if (!macro) {
    for (std::uint32_t slot = 0; slot < MacroCatalog::kTotalSlots; ++slot) {
      if (!macros->FindMacroAtSlot(slot).has_value()) {
        lua_pushnumber(L, slot + 1);
        return 1;
      }
    }
    lua_pushnumber(L, 0);
    return 1;
  }

  std::string body = macro->body;
  if (lua_isstring(L, 4)) {
    body = SafeLuaString(L, 4);
  }

  const std::string name =
      lua_isstring(L, 2) ? SafeLuaString(L, 2)
                         : macro->name;
  std::uint32_t icon = 0;
  if (lua_isnumber(L, 3)) {
    macros->LoadIconList();
    const auto requested_icon =
        openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 3));
    if (macros->MacroIconName(requested_icon).has_value()) {
      icon = requested_icon;
    }
  }

  auto& sys = *macros;
  const MacroId result = sys.EditMacro(macro->id, name, icon, body);
  const int32_t slot = result.IsValid() ? sys.FindSlotIndex(result) : -1;
  lua_pushnumber(L, slot + 1);
  return 1;
}

int LuaDeleteMacro(lua_State* L) {
  auto* macros = GetMacroCatalog(L);
  if (macros == nullptr ||
      !macros->CanPerform(MacroProtectedOperation::kModifyCatalog)) {
    return 0;
  }
  if (const auto macro = GetMacroFromLuaArgs(L, *macros); macro) {
    macros->DeleteMacro(macro->id);
  }
  return 0;
}

int LuaGetMacroIndexByName(lua_State* L) {
  if (!lua_isstring(L, 1)) {
    return luaL_error(L, "Usage: GetMacroIndexByName(name)");
  }
  const char* name = lua_tostring(L, 1);
  auto* macros = GetMacroCatalog(L);
  if (macros == nullptr) {
    return 0;
  }
  auto& sys = *macros;
  lua_pushnumber(L, sys.GetMacroIndexByName(name));
  return 1;
}

int LuaGetRunningMacro(lua_State* L) {
  auto* macros = GetMacroCatalog(L);
  auto running_macro_id = decltype(macros->GetRunningMacroId()){};
  if (macros != nullptr) {
    running_macro_id = macros->GetRunningMacroId();
  }
  if (!running_macro_id.has_value() || !running_macro_id->IsValid()) {
    lua_pushnil(L);
  } else {
    lua_pushnumber(L, macros->FindSlotIndex(*running_macro_id) + 1);
  }
  return 1;
}

int LuaSetMacroItem(lua_State* L) {
  if (!lua_isstring(L, 2))
    return luaL_error(L, "Usage: SetMacroItem(macro, item [,target])");

  auto* macros = GetMacroCatalog(L);
  if (macros == nullptr) {
    return 0;
  }
  const auto macro = GetMacroFromLuaArgs(L, *macros);
  if (!macro) return 0;

  const auto resolved_item_id = ResolveMacroItemIdArg(L, 2);
  const auto target_guid =
      ResolveUnitId(GetWorldSession(L), SafeLuaString(L, 3)).GetRawValue();

  const bool refresh_action_bar =
      macro->resolved_spell_id > 0 ||
      resolved_item_id != macro->resolved_item_id ||
      target_guid != macro->target_guid;
  macros->UpdateMacro(
      macro->id, [=](::openwow::game::MacroDocument& document) {
        document.resolved_item_id = resolved_item_id;
        document.resolved_spell_id = resolved_item_id != 0 ? 0 : -1;
        document.target_guid = target_guid;
        document.needs_icon_update = false;
        document.requires_action_bar_icon_updates = false;
      });
  if (refresh_action_bar) {
    macros->NotifyActionBarRefresh(macro->id);
  }

  if (static_cast<std::int32_t>(resolved_item_id) <= 0) {
    lua_pushnil(L);
  } else {
    lua_pushnumber(L, 1.0);
  }
  return 1;
}

int LuaGetMacroItem(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  const auto macro = GetMacroFromLuaArgs(L, session->macros());
  if (!macro || macro->resolved_item_id == 0) {
    return 0;
  }

  const auto* item_template =
      session->query_cache().GetOrRequestItemTemplate(macro->resolved_item_id);
  if (item_template == nullptr) {
    return 0;
  }

  const auto* active_player = session->objects().GetActivePlayer();
  if (active_player != nullptr) {
    if (const auto* carried_item = FindFirstCarriedItemByEntry(
            session->inventory_replica(), macro->resolved_item_id);
        carried_item != nullptr) {
      const auto display_name =
          ::openwow::ui::game::detail::ResolveLootItemDisplayName(
              GetDbcLoader(L), item_template->name,
              openwow::ui::SignedI32FromU32Bits(
                  carried_item->random_property));
      const auto link = BuildMacroItemLink(
          *item_template, display_name, carried_item, active_player->State().GetLevel());
      lua_pushstring(L, display_name.c_str());
      lua_pushstring(L, link.c_str());
      return 2;
    }
  }

  const auto link = BuildMacroItemLink(
      *item_template, item_template->name, nullptr,
      active_player != nullptr ? active_player->State().GetLevel() : 0u);
  lua_pushstring(L, item_template->name.c_str());
  lua_pushstring(L, link.c_str());
  return 2;
}

int LuaSetMacroSpell(lua_State* L) {
  if (!lua_isstring(L, 2))
    return luaL_error(L, "Usage: SetMacroSpell(macro, spell [,target])");

  auto* macros = GetMacroCatalog(L);
  if (macros == nullptr) {
    return 0;
  }
  const auto macro = GetMacroFromLuaArgs(L, *macros);
  if (!macro) return 0;

  const auto spell_query =
      ::openwow::game::spells::adapters::lua::ResolveSpellName(
          L, SafeLuaString(L, 2));
  const auto spell_id =
      spell_query.has_value() ? static_cast<std::int32_t>(spell_query->spell_id) : -1;
  const auto target_guid =
      ResolveUnitId(GetWorldSession(L), SafeLuaString(L, 3)).GetRawValue();

  const bool refresh_action_bar =
      macro->resolved_item_id > 0 ||
      spell_id != macro->resolved_spell_id ||
      target_guid != macro->target_guid;
  macros->UpdateMacro(
      macro->id, [=](::openwow::game::MacroDocument& document) {
        document.resolved_item_id = 0;
        document.resolved_spell_id = spell_id;
        document.resolved_spell_from_pet_book =
            spell_query.has_value() && spell_query->from_pet_book;
        document.target_guid = target_guid;
        document.needs_icon_update = false;
        document.requires_action_bar_icon_updates = false;
      });
  if (refresh_action_bar) {
    macros->NotifyActionBarRefresh(macro->id);
  }

  if (spell_id <= 0) {
    lua_pushnil(L);
  } else {
    lua_pushnumber(L, 1.0);
  }
  return 1;
}

int LuaGetNumMacroIcons(lua_State* L) {
  auto* macros = GetMacroCatalog(L);
  if (macros != nullptr) {
    macros->LoadIconList();
  }
  lua_pushnumber(L, static_cast<lua_Integer>(
                        macros != nullptr ? macros->GetNumMacroIcons() : 0u));
  return 1;
}

int LuaGetNumMacroItemIcons(lua_State* L) {
  auto* macros = GetMacroCatalog(L);
  if (macros != nullptr) {
    macros->LoadIconList();
  }
  lua_pushnumber(L,
                 static_cast<lua_Integer>(
                     macros != nullptr ? macros->GetNumMacroItemIcons() : 0u));
  return 1;
}

int LuaRunMacro(lua_State* L) {
  auto* macros = GetMacroCatalog(L);
  if (macros == nullptr) {
    return 0;
  }
  const auto macro = GetMacroFromLuaArgs(L, *macros);
  if (!macro) {
    return 0;
  }

  const auto button = lua_tostring(L, 2);
  auto& sys = *macros;
  const auto slot = sys.FindSlotIndex(macro->id);
  if (slot < 0) {
    return 0;
  }

  sys.ExecuteBody(
      static_cast<std::uint32_t>(slot),
      ::openwow::game::actions::macros::adapters::retail::
          DecodeMacroInputButton(button));
  return 0;
}

int LuaRunMacroText(lua_State* L) {
  if (!lua_isstring(L, 1)) {
    return luaL_error(L, "Usage: RunMacroText(\"text\" [, \"button\"])");
  }

  std::string body = SafeLuaString(L, 1);
  if (body.size() > ::openwow::game::MacroCatalog::kMaxBodyLength) {
    body.resize(::openwow::game::MacroCatalog::kMaxBodyLength);
  }
  const auto button = lua_tostring(L, 2);
  auto* macros = GetMacroCatalog(L);
  if (macros == nullptr) {
    return 0;
  }
  auto& sys = *macros;
  sys.ExecuteBodyText(
      body,
      ::openwow::game::actions::macros::adapters::retail::
          DecodeMacroInputButton(button));
  return 0;
}

int LuaSecureCmdOptionParse(lua_State* L) {
  const char* options = lua_tostring(L, 1);
  if (options == nullptr) {
    return luaL_error(L, "Usage: SecureCmdOptionParse(\"options\")");
  }

  auto* macros = GetMacroCatalog(L);
  if (macros == nullptr) {
    return 0;
  }
  const auto parsed = macros->ParseSecureCommandOptions(options);

  if (!parsed.matched) {
    return 0;
  }

  lua_pushstring(L, parsed.value.c_str());
  if (!parsed.target.empty()) {
    lua_pushstring(L, parsed.target.c_str());
  } else {
    lua_pushnil(L);
  }
  return 2;
}

openwow::ui::lua::NativeBindingCatalog MacroConstantCatalog() {
  constexpr openwow::ui::LuaIntegerGlobal kMacroConstants[] = {
      {"CURRENT_ACTIONBAR_PAGE", 1},
      {"NUM_ACTIONBAR_PAGES", 6},
      {"MAX_ACCOUNT_MACROS", kMaxGlobalMacros},
      {"MAX_CHARACTER_MACROS", kMaxPerCharMacros},
  };
  return openwow::ui::lua::NativeConstantCatalog(
      "game.actions.macros", openwow::ui::lua::BindingScope::kWorld,
      kMacroConstants);
}

int LuaGetMacroIconInfo(lua_State* L) {
  auto* macros = GetMacroCatalog(L);
  if (macros == nullptr) {
    return 0;
  }
  return PushMacroIconInfo(
      L, *macros, "Usage: GetMacroIconInfo(index)",
      [](const ::openwow::game::MacroCatalog& sys, const std::uint32_t index) {
        return sys.MacroIconName(index);
      });
}

int LuaGetMacroSpell(lua_State* L) {
  const auto* macros = GetMacroCatalog(L);
  if (macros == nullptr) {
    return 0;
  }
  const auto macro = GetMacroFromLuaArgs(L, *macros);
  if (!macro) {
    return 0;
  }

  if (macro->resolved_spell_id > 0 &&
      PushSpellNameAndSubtext(
          L, static_cast<std::uint32_t>(macro->resolved_spell_id))) {
    return 2;
  }

  lua_pushnil(L);
  lua_pushnil(L);
  return 2;
}

int LuaStopMacro([[maybe_unused]] lua_State* L) {
  auto* macros = GetMacroCatalog(L);
  if (macros != nullptr) {
    macros->StopMacro();
  }
  return 0;
}

int LuaGetMacroItemIconInfo(lua_State* L) {
  auto* macros = GetMacroCatalog(L);
  if (macros == nullptr) {
    return 0;
  }
  return PushMacroIconInfo(
      L, *macros, "Usage: GetMacroItemIconInfo(index)",
      [](const ::openwow::game::MacroCatalog& sys, const std::uint32_t index) {
        return sys.MacroItemIconName(index);
      });
}

int LuaGetRunningMacroButton(lua_State* L) {
  auto* macros = GetMacroCatalog(L);
  std::optional<std::string> button;
  if (macros != nullptr) {
    if (const auto running_button = macros->RunningMacroInputButton();
        running_button) {
      button = running_button->value();
    }
  } else {
    button =
        openwow::ui::game::lua_adapter::CurrentMouseButtonOverride(L);
  }
  if (button) {
    lua_pushlstring(L, button->data(), button->size());
  } else {
    lua_pushnil(L);
  }
  return 1;
}

}
