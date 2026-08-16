#pragma once

#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/game/inventory/equipment/adapters/lua/equipment_lua_api.h"
#include "openwow/game/inventory/equipment/equipment_sets.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/actions/macros/application/macro_catalog.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/spell_query_bridge.h"

#include <string>
#include <string_view>

namespace openwow::ui::game::detail::cursor_texture {

inline const openwow::data::dbc::DbcLoader* GetDbcLoader(lua_State* L) {
  lua_getfield(L, LUA_REGISTRYINDEX, "openwow.dbc_loader");
  auto* dbc = static_cast<const openwow::data::dbc::DbcLoader*>(
      lua_touserdata(L, -1));
  lua_pop(L, 1);
  return dbc;
}

inline const openwow::data::dbc::SpellEntry* GetSpellEntry(
    lua_State* L,
    std::uint32_t spell_id) {
  const auto* dbc = GetDbcLoader(L);
  return dbc ? dbc->spell().LookupEntry(spell_id) : nullptr;
}

inline std::string ResolveSpellIconPath(lua_State* L,
                                        const std::uint32_t spell_id,
                                        const std::uint32_t explicit_icon_id = 0) {
  const auto* dbc = GetDbcLoader(L);
  if (dbc == nullptr) {
    return {};
  }

  const auto resolve_icon = [dbc](const std::uint32_t icon_id) -> std::string {
    if (icon_id == 0) {
      return {};
    }

    const auto* icon = dbc->spell_icon().LookupEntry(icon_id);
    if (icon == nullptr || std::string_view(icon->icon_path).empty()) {
      return {};
    }

    return std::string(icon->icon_path);
  };

  if (auto texture = resolve_icon(explicit_icon_id); !texture.empty()) {
    return texture;
  }

  const auto* spell = dbc->spell().LookupEntry(spell_id);
  if (spell == nullptr) {
    return {};
  }

  return resolve_icon(spell->spell_icon_id);
}

inline std::string ResolveSpellTexturePath(lua_State* L, std::uint32_t spell_id) {
  const auto* dbc = GetDbcLoader(L);
  auto* session = ::openwow::ui::game::detail::GetWorldSession(L);
  if (session != nullptr &&
      ::openwow::game::SpellHasAttackActionEffect(spell_id, dbc)) {
    if (auto texture =
            ::openwow::game::ResolveActiveAttackActionTexturePath(*session, dbc);
        !texture.empty()) {
      return texture;
    }
  }

  if (session != nullptr &&
      ::openwow::game::SpellHasRangedAttackActionFlags(spell_id, dbc)) {
    if (auto texture =
            ::openwow::game::ResolveActiveRangedActionTexturePath(*session, dbc);
        !texture.empty()) {
      return texture;
    }
  }

  if (const auto query = ::openwow::game::SpellQueryBridge::Get().Query(spell_id);
      query.has_value() && query->iconId != 0) {
    if (auto texture = ResolveSpellIconPath(L, spell_id, query->iconId);
        !texture.empty()) {
      return texture;
    }
  }

  return ResolveSpellIconPath(L, spell_id);
}

inline std::string ResolveItemTexturePath(lua_State* L, std::uint32_t item_entry) {
  const auto* dbc = GetDbcLoader(L);
  const auto* item =
      ::openwow::ui::game::detail::RequireItemDefinitions(L).GetItem(item_entry);
  if (!dbc || !item || item->display_id == 0) {
    return {};
  }

  return ::openwow::game::ResolveItemInventoryIconTexturePath(
      dbc, item->display_id);
}

inline bool ItemTemplateHasLocationRestriction(
    const ::openwow::game::ItemTemplate& item_template) {
  return item_template.area != 0 || item_template.map != 0;
}

inline std::string ResolveMacroTexturePath(lua_State* L,
                                           std::uint32_t macro_id) {
  if (macro_id == 0) {
    return {};
  }
  auto* session = detail::GetWorldSession(L);
  if (session == nullptr) {
    return {};
  }
  auto& macros = session->macros();

  if (const auto icon_path =
          macros.GetIconPath(
              ::openwow::game::actions::macros::MacroId(macro_id));
      !icon_path.empty()) {
    return icon_path;
  }

  return {};
}

inline std::string ResolveEquipmentSetTexturePath(lua_State* L,
                                                  std::uint32_t set_id) {
  const auto* set = RequireEquipmentSets(L).find(set_id);
  if (set == nullptr || set->icon.empty()) {
    return {};
  }
  return ::openwow::game::equipment_set_icon_path(set->icon);
}

}
