
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/api/game_lua_api_petition.h"
#include "openwow/game/petition_frame.h"

namespace openwow::ui::game::detail {

namespace {

const char* PetitionTypeToLuaString(const std::uint32_t petition_type) {
  if (petition_type == 0) {
    return "guild";
  }
  if (petition_type == 1) {
    return "arena";
  }
  return "other";
}

auto MakeWorldSessionPacketSender(::openwow::game::WorldSession& session) {
  return [&session](const openwow::net::wotlk::WorldPacket& packet) {
    return session.Send(packet);
  };
}

}

int LuaClosePetition([[maybe_unused]] lua_State* L) {
  if (auto* session = GetWorldSession(L)) {
    session->ClosePetitionSignatureDisplay();
  }
  return 0;
}

int LuaClosePetitionVendor([[maybe_unused]] lua_State* L) {
  if (auto* session = GetWorldSession(L)) {
    session->ClosePetitionVendorInteraction();
  }
  return 0;
}

int LuaCloseTabardCreation([[maybe_unused]] lua_State* L) {
  if (auto* session = GetWorldSession(L)) {
    session->CloseTabardVendorInteraction();
  }
  return 0;
}

int LuaClickPetitionButton(lua_State* L) {
  if (auto* session = GetWorldSession(L)) {
    ::openwow::game::PetitionFrame_ClickPetitionButton(*session);
  }
  return 0;
}

int LuaRenamePetition(lua_State* L) {
  if (lua_isstring(L, 1) == 0) {
    return luaL_error(L, "Usage(RenamePetition(\"name\")");
  }

  auto* session = GetWorldSession(L);
  if (!session) return 0;

  const auto petition_guid = session->petition().last_signatures().petition_guid;
  if (petition_guid == 0) return 0;

  const char* name = lua_tostring(L, 1);
  if (::openwow::game::PetitionFrame_ValidateRename(name) != 0) return 0;

  session->interaction().SendPetitionRename(petition_guid,
                                            name != nullptr ? name : "");
  return 0;
}

int LuaSignPetition(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (!session) return 0;

  std::uint8_t petition_choice = 1;
  if (lua_isnumber(L, 1) != 0) {
    petition_choice = static_cast<std::uint8_t>(
        static_cast<std::int64_t>(lua_tonumber(L, 1)));
  }

  const auto petition_guid = session->petition().active_petition_guid();
  if (petition_guid != 0) {
    session->interaction().SendPetitionSign(petition_guid, petition_choice);
    session->petition().MarkActivePetitionSignRequested();
  }
  return 0;
}

int LuaHasFilledPetition(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (session && ::openwow::game::PetitionFrame_HasFilledArenaPetition(*session)) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaTurnInGuildCharter(lua_State* L) {
  if (auto* session = GetWorldSession(L)) {
    ::openwow::game::PetitionFrame_TurnInGuildCharter(*session);
  }
  return 0;
}

int LuaTurnInPetition(lua_State* L) {
  if (auto* session = GetWorldSession(L)) {
    ::openwow::game::PetitionFrame_TurnInSelectedPetition(*session);
  }
  return 0;
}

int LuaCanSignPetition(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (!session) {
    lua_pushnumber(L, 1.0);
    return 1;
  }

  bool active_player_is_guilded = false;
  if (const auto* active_player = session->objects().GetActivePlayer()) {
    active_player_is_guilded = active_player->GetUInt32(PLAYER_GUILDID) != 0;
  }

  if (session->petition().CanActivePlayerSign(
          session->objects().GetActivePlayerGuid().GetRawValue(),
          active_player_is_guilded)) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaGetNumPetitionNames(lua_State* L) {
  if (auto* session = GetWorldSession(L)) {
    lua_pushnumber(
        L,
        static_cast<lua_Number>(
            session->petition().GetDisplayedPetitionSignatureCount()));
  } else {
    lua_pushnumber(L, 0.0);
  }
  return 1;
}

int LuaGetNumPetitionItems(lua_State* L) {
  if (auto* session = GetWorldSession(L)) {
    lua_pushnumber(
        L,
        static_cast<lua_Number>(session->petition().GetPetitionVendorOfferCount()));
  } else {
    lua_pushnumber(L, 0.0);
  }
  return 1;
}

int LuaGetPetitionInfo(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (!session || !session->petition().HasActivePetitionQuery()) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnumber(L, 0.0);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnumber(L, 0.0);
    return 7;
  }

  auto& petition = session->petition();
  const auto& query = petition.last_petition_query();
  lua_pushstring(L, PetitionTypeToLuaString(query.petition_type));
  lua_pushstring(L, query.name.c_str());
  lua_pushstring(L, query.body_text.c_str());
  lua_pushnumber(L, static_cast<lua_Number>(query.max_signatures));

  std::string owner_name;
  if (petition.ResolveOrRequestDisplayedPetitionOwnerName(
          session->query_cache(), session->objects(),
          MakeWorldSessionPacketSender(*session), &owner_name)) {
    lua_pushstring(L, owner_name.c_str());
  } else {
    lua_pushnil(L);
  }

  if (query.owner_guid == session->objects().GetActivePlayerGuid().GetRawValue()) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }

  lua_pushnumber(L, static_cast<lua_Number>(query.min_signatures));
  return 7;
}

int LuaGetPetitionNameInfo(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (lua_gettop(L) < 1 || lua_isnumber(L, 1) == 0) {
    return luaL_error(L, "Usage: GetPetitionNameInfo(index)");
  }

  if (!session) {
    lua_pushnil(L);
    return 1;
  }

  const auto index = static_cast<lua_Integer>(lua_tointeger(L, 1));
  if (index <= 0) {
    lua_pushnil(L);
    return 1;
  }

  std::string signer_name;
  if (session->petition().ResolveOrRequestDisplayedPetitionSignerName(
          static_cast<std::size_t>(index - 1), session->query_cache(),
          session->objects(), MakeWorldSessionPacketSender(*session),
          &signer_name)) {
    lua_pushstring(L, signer_name.c_str());
  } else {
    lua_pushnil(L);
  }
  return 1;
}

}
