#include "openwow/ui/game/api/held_cursor_lua_api.h"

#include "openwow/game/actions/macros/application/macro_catalog.h"
#include "openwow/game/inventory/equipment/equipment_sets.h"
#include "openwow/game/inventory/items/adapters/retail/item_display_name_formatter.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/items/item_link_parser.h"
#include "openwow/game/localization.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/cursor_texture_resolver.h"
#include "openwow/ui/game/runtime/lua/held_cursor_lua_binding.h"

#include <lua.hpp>

#include <cstdint>
#include <string>

namespace openwow::ui::game::detail {
namespace {

std::uint32_t CursorLinkLevel(lua_State* state) {
  auto* session = GetWorldSession(state);
  const auto* player =
      session != nullptr ? session->objects().GetActivePlayer() : nullptr;
  return player != nullptr ? player->State().GetLevel() : 0;
}

std::string TemplateItemLink(lua_State* state, const std::uint32_t item_id) {
  openwow::game::ItemLinkData data;
  data.itemId = item_id;
  data.quality = 1;
  data.linkLevel = CursorLinkLevel(state);
  if (const auto* item = RequireItemDefinitions(state).GetItem(item_id)) {
    data.quality = static_cast<std::uint8_t>(item->quality);
    data.name = item->name;
  }
  return openwow::game::ItemLinkParser::Generate(data);
}

std::string LiveItemLink(lua_State* state,
                         const openwow::game::ItemInstance& item) {
  openwow::game::ItemLinkData data;
  data.itemId = item.entry;
  data.enchantId = item.GetPermanentEnchant();
  data.gemIds[0] = item.GetSocketEnchant(0);
  data.gemIds[1] = item.GetSocketEnchant(1);
  data.gemIds[2] = item.GetSocketEnchant(2);
  data.randomPropertyId = item.random_property;
  data.suffixFactor = static_cast<std::int32_t>(item.random_suffix);
  data.quality = 1;
  data.linkLevel = CursorLinkLevel(state);
  if (const auto* item_template =
          RequireItemDefinitions(state).GetItem(item.entry)) {
    data.quality = static_cast<std::uint8_t>(item_template->quality);
    data.name = openwow::game::FormatItemDisplayNameWithRandomProperty(
        openwow::game::Localization::Get(), cursor_texture::GetDbcLoader(state),
        item_template->name, item.random_property);
  }
  return openwow::game::ItemLinkParser::Generate(data);
}

int PushItem(lua_State* state, const std::uint32_t id,
             const std::string& link) {
  if (id == 0) return 0;
  lua_pushstring(state, "item");
  lua_pushnumber(state, static_cast<lua_Number>(id));
  lua_pushlstring(state, link.data(), link.size());
  return 3;
}

}

int LuaGetCursorInfo(lua_State* state) {
  auto* cursor = lua::FindHeldCursor(*state);
  if (cursor == nullptr) return 0;
  namespace held = openwow::game::actions::held_cursor;
  if (const auto* item = cursor->get_if<held::LiveItem>()) {
    return PushItem(state, item->item.entry, LiveItemLink(state, item->item));
  }
  if (const auto* item = cursor->get_if<held::ActionBarItem>()) {
    return PushItem(state, item->item_entry, TemplateItemLink(state, item->item_entry));
  }
  if (const auto* item = cursor->get_if<held::AmmoItem>()) {
    return PushItem(state, item->item_entry, TemplateItemLink(state, item->item_entry));
  }
  if (const auto* item = cursor->get_if<held::GuildBankItem>()) {
    return PushItem(state, item->item_entry, TemplateItemLink(state, item->item_entry));
  }
  if (const auto* spell = cursor->get_if<held::Spell>()) {
    std::uint32_t companion_index = 0;
    const char* companion_type = nullptr;
    if (ResolveCompanionSpellCursorInfo(state, spell->spell_id, companion_index,
                                        companion_type)) {
      lua_pushstring(state, "companion");
      lua_pushnumber(state, static_cast<lua_Number>(companion_index));
      lua_pushstring(state, companion_type != nullptr ? companion_type : "UNKNOWN");
      return 3;
    }
    lua_pushstring(state, "spell");
    lua_pushnumber(state, static_cast<lua_Number>(spell->spellbook_slot));
    lua_pushstring(state, spell->from_pet_spellbook ? "pet" : "spell");
    return 3;
  }
  if (const auto* action = cursor->get_if<held::PetAction>()) {
    lua_pushstring(state, "petaction");
    lua_pushnumber(state, static_cast<lua_Number>(action->source_slot));
    return 2;
  }
  if (const auto* item = cursor->get_if<held::MerchantItem>()) {
    lua_pushstring(state, "merchant");
    lua_pushnumber(state, static_cast<lua_Number>(item->zero_based_slot + 1));
    return 2;
  }
  if (const auto* money = cursor->get_if<held::PlayerMoney>()) {
    lua_pushstring(state, "money");
    lua_pushnumber(state, static_cast<lua_Number>(money->amount));
    return 2;
  }
  if (const auto* money = cursor->get_if<held::GuildBankMoney>()) {
    lua_pushstring(state, "guildbankmoney");
    lua_pushnumber(state, static_cast<lua_Number>(money->amount));
    return 2;
  }
  if (const auto* macro = cursor->get_if<held::Macro>()) {
    auto* session = GetWorldSession(state);
    const openwow::game::actions::macros::MacroId id(macro->stable_id);
    if (session == nullptr || !id.IsValid()) return 0;
    lua_pushstring(state, "macro");
    lua_pushnumber(state,
                   static_cast<lua_Number>(session->macros().FindSlotIndex(id) + 1));
    return 2;
  }
  if (const auto* pet = cursor->get_if<held::StablePet>()) {
    lua_pushstring(state, "petaction");
    lua_pushnumber(state, static_cast<lua_Number>(pet->stable_index));
    return 2;
  }
  if (const auto* payload = cursor->get_if<held::EquipmentSet>()) {
    const auto* set = RequireEquipmentSets(state).find(payload->stable_id);
    if (set == nullptr) return 0;
    lua_pushstring(state, "equipmentset");
    lua_pushstring(state, set->name.c_str());
    return 2;
  }
  return 0;
}

}
