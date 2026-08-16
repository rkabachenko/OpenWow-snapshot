#include "openwow/game/spells/glyphs/adapters/lua/glyph_lua_api.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/spell_action.h"
#include "openwow/game/talent_info.h"
#include "openwow/game/update_fields.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/runtime/lua/lua_composition.h"
#include <cstdio>
#include <limits>

namespace openwow::ui::game::detail {

static constexpr int kNumGlyphSockets = 6;

namespace {

constexpr std::uint32_t kSpellEffectApplyGlyph = 74u;

std::uint32_t ResolveGlyphSocketLuaIndex(lua_State *L, int argument_index) {
  return TruncateLuaNumberToWrappedLowU32(lua_tonumber(L, argument_index));
}

std::int32_t ResolveGlyphSocketLuaSlotOffset(lua_State *L, int argument_index) {
  return static_cast<std::int32_t>(
             TruncateLuaNumberToWrappedLowU32(lua_tonumber(L, argument_index))) -
         1;
}

std::uint32_t ResolveGlyphTalentGroupIndexArg(lua_State *L, int argument_index) {
  std::optional<std::uint32_t> group_arg;
  if (lua_isnumber(L, argument_index)) {
    group_arg = TruncateLuaNumberToWrappedLowU32(lua_tonumber(L, argument_index));
  }
  return openwow::game::TalentInfoStore::Get().GetGroupIndexArg(group_arg);
}

const openwow::data::dbc::DbcLoader *GetDbc(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, "openwow.dbc_loader");
  auto *dbc = static_cast<const openwow::data::dbc::DbcLoader *>(lua_touserdata(L, -1));
  lua_pop(L, 1);
  return dbc;
}

struct GlyphSocketState {
  const openwow::data::dbc::DbcLoader *dbc = nullptr;
  const openwow::data::dbc::GlyphSlotEntry *slot_entry = nullptr;
  const openwow::data::dbc::GlyphPropertiesEntry *glyph_entry = nullptr;
  uint32_t slot_id = 0;
  uint16_t glyph_property_id = 0;
  bool enabled = false;
};

bool ResolveGlyphSocketState(lua_State *L, std::uint32_t lua_socket_index,
                             std::uint32_t talent_group_index, GlyphSocketState *out) {
  if (!out || lua_socket_index < 1 || lua_socket_index > kNumGlyphSockets) {
    return false;
  }

  auto *session = GetWorldSession(L);
  if (!session)
    return false;

  const auto *player = session->objects().GetActivePlayer();
  if (!player)
    return false;

  const auto *dbc = session->GetDbcLoader();
  if (!dbc) {
    dbc = GetDbc(L);
  }
  if (!dbc)
    return false;

  const auto *group =
      openwow::game::TalentInfoStore::Get().GetTalentGroupData(talent_group_index, false, false);
  if (!group)
    return false;

  const auto slot_index = static_cast<uint8_t>(lua_socket_index - 1);
  out->dbc = dbc;
  out->slot_id = player->GetGlyphSlot(slot_index);
  out->glyph_property_id = group->GetGlyph(slot_index);
  out->enabled = (player->GetGlyphsEnabled() & (1u << slot_index)) != 0;
  out->slot_entry = dbc->glyph_slot().LookupEntry(out->slot_id);
  out->glyph_entry = dbc->glyph_properties().LookupEntry(out->glyph_property_id);
  return true;
}

std::uint32_t ReadGlyphSlotIdForLuaSocketIndex(const openwow::game::CGPlayer_C &player,
                                               const std::int32_t zero_based_socket_index) {
  const auto field_index = static_cast<std::int32_t>(openwow::game::PLAYER_FIELD_GLYPH_SLOTS_1) +
                           zero_based_socket_index;
  if (field_index < 0 ||
      field_index > static_cast<std::int32_t>(std::numeric_limits<std::uint16_t>::max())) {
    return 0;
  }

  return player.GetUInt32(static_cast<std::uint16_t>(field_index));
}

const openwow::data::dbc::GlyphPropertiesEntry *
FindActiveTargetingGlyphPropertiesEntry(const openwow::data::dbc::DbcLoader &dbc,
                                        const std::uint32_t active_targeting_spell) {
  if (active_targeting_spell == 0u) {
    return nullptr;
  }

  const auto *spell_entry = dbc.spell().LookupEntry(active_targeting_spell);
  if (spell_entry == nullptr) {
    return nullptr;
  }

  for (std::size_t effect_index = 0; effect_index < spell_entry->effect.size(); ++effect_index) {
    if (spell_entry->effect[effect_index] != kSpellEffectApplyGlyph) {
      continue;
    }

    const auto glyph_property_id =
        static_cast<std::uint32_t>(spell_entry->effect_misc_value[effect_index]);
    if (glyph_property_id == 0u) {
      continue;
    }

    if (const auto *glyph_entry = dbc.glyph_properties().LookupEntry(glyph_property_id);
        glyph_entry != nullptr) {
      return glyph_entry;
    }
  }

  return nullptr;
}

}

int LuaGetNumGlyphSockets(lua_State *L) {

  lua_pushnumber(L, 6.0);
  return 1;
}

int LuaGetGlyphSocketInfo(lua_State *L) {
  if (!lua_isnumber(L, 1))
    return luaL_error(L, "Usage: GetGlyphSocketInfo(index[, talentGroup])");

  const auto socket_index = ResolveGlyphSocketLuaIndex(L, 1);
  const auto talent_group_index = ResolveGlyphTalentGroupIndexArg(L, 2);

  GlyphSocketState state;
  if (!ResolveGlyphSocketState(L, socket_index, talent_group_index, &state)) {
    return 0;
  }

  if (state.enabled) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }

  if (state.slot_entry) {
    lua_pushnumber(L, static_cast<lua_Number>(state.slot_entry->type + 1));
  } else {
    lua_pushnil(L);
  }

  if (state.glyph_entry) {
    lua_pushnumber(L, static_cast<lua_Number>(state.glyph_entry->spell_id));
    if (const auto *icon = state.dbc->spell_icon().LookupEntry(state.glyph_entry->spell_icon_id)) {
      lua_pushlstring(L, icon->icon_path.data(), icon->icon_path.size());
    } else {
      lua_pushnil(L);
    }
  } else {
    lua_pushnil(L);
    lua_pushnil(L);
  }

  return 4;
}

int LuaGetGlyphLink(lua_State *L) {
  if (!lua_isnumber(L, 1))
    return luaL_error(L, "Usage: GetGlyphLink(index[, talentGroup])");

  const auto socket_index = ResolveGlyphSocketLuaIndex(L, 1);
  const auto talent_group_index = ResolveGlyphTalentGroupIndexArg(L, 2);

  GlyphSocketState state;
  if (!ResolveGlyphSocketState(L, socket_index, talent_group_index, &state)) {
    return 0;
  }

  if (!state.slot_entry || !state.glyph_entry) {
    lua_pushstring(L, "");
    return 1;
  }

  const auto *spell = state.dbc->spell().LookupEntry(state.glyph_entry->spell_id);
  if (!spell || spell->spell_name.empty()) {
    lua_pushstring(L, "");
    return 1;
  }

  char link[512];
  std::snprintf(link, sizeof(link), "|cff66bbff|Hglyph:%u:%u|h[%.*s]|h|r", state.slot_id,
                static_cast<unsigned>(state.glyph_property_id),
                static_cast<int>(spell->spell_name.size()), spell->spell_name.data());
  lua_pushstring(L, link);
  return 1;
}

int LuaPlaceGlyphInSocket(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: PlaceGlyphInSocket(index)");
  }

  const auto zero_based_slot = ResolveGlyphSocketLuaIndex(L, 1) - 1u;
  auto *session = GetWorldSession(L);
  const auto *player = session != nullptr ? session->objects().GetActivePlayer() : nullptr;
  if (zero_based_slot >= static_cast<std::uint32_t>(kNumGlyphSockets) || player == nullptr) {
    return 0;
  }

  if (::openwow::game::SpellAction_PlaceGlyphInSocket(
          *session, static_cast<int>(zero_based_slot))) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaRemoveGlyphFromSocket(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: RemoveGlyphFromSocket(index)");
  }

  const auto zero_based_slot = ResolveGlyphSocketLuaIndex(L, 1) - 1u;
  auto *session = GetWorldSession(L);
  const auto *player = session != nullptr ? session->objects().GetActivePlayer() : nullptr;
  if (zero_based_slot >= static_cast<std::uint32_t>(kNumGlyphSockets) || player == nullptr ||
      player->GetGlyph(static_cast<std::uint8_t>(zero_based_slot)) == 0u) {
    return 0;
  }

  session->interaction().SendRemoveGlyph(zero_based_slot);
  return 0;
}

int LuaGlyphMatchesSocket(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GlyphMatchesSocket(socketIndex)");
  }

  const auto zero_based_socket_index = ResolveGlyphSocketLuaSlotOffset(L, 1);
  auto *session = GetWorldSession(L);
  const auto *player = session ? session->objects().GetActivePlayer() : nullptr;
  if (player == nullptr) {
    lua_pushnil(L);
    return 1;
  }

  const auto *dbc = session ? session->GetDbcLoader() : nullptr;
  if (dbc == nullptr) {
    dbc = GetDbc(L);
  }
  if (dbc == nullptr) {
    return 0;
  }

  const auto *glyph_entry = FindActiveTargetingGlyphPropertiesEntry(
      *dbc, session->spells().GetTargeting().GetSpellId());
  if (glyph_entry == nullptr) {
    return 0;
  }

  const auto slot_id = ReadGlyphSlotIdForLuaSocketIndex(*player, zero_based_socket_index);
  const auto *slot_entry = dbc->glyph_slot().LookupEntry(slot_id);
  if (slot_entry == nullptr || slot_entry->type != glyph_entry->type) {
    return 0;
  }

  lua_pushnumber(L, 1.0);
  return 1;
}

openwow::ui::lua::NativeBindingCatalog GlyphConstantCatalog() {
  constexpr openwow::ui::LuaIntegerGlobal kGlyphConstants[] = {
      {"GLYPH_TYPE_MAJOR", 1},
      {"GLYPH_TYPE_MINOR", 2},
      {"NUM_GLYPH_SLOTS", kNumGlyphSockets},
  };
  return openwow::ui::lua::NativeConstantCatalog(
      "game.spells.glyphs", openwow::ui::lua::BindingScope::kWorld,
      kGlyphConstants);
}

}
