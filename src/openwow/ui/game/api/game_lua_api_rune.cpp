
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/api/game_lua_api_rune.h"

namespace openwow::ui::game::detail {

namespace {

constexpr int kLuaRuneSlotLimit = 8;

const CGPlayer_C* GetLocalPlayer(WorldSession* session) {
  if (session == nullptr) {
    return nullptr;
  }
  return session->objects().GetLocalPlayerTyped();
}

}

int LuaGetRuneCount(lua_State* L) {
  auto* session = GetWorldSession(L);
  const auto* player = GetLocalPlayer(session);
  if (player == nullptr) {
    return 0;
  }

  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetRuneCount(slot)");
  }

  const auto slot =
      static_cast<std::uint32_t>(openwow::ui::TruncateLuaNumberToI32(
          lua_tonumber(L, 1))) -
      1u;
  if (slot >= static_cast<std::uint32_t>(kLuaRuneSlotLimit)) {
    return 0;
  }

  lua_pushnumber(L,
                 (session->runes().ready_mask() & (1u << slot)) != 0 ? 1.0 : 0.0);
  return 1;
}

int LuaGetRuneCooldown(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetRuneCooldown(slot)");
  }

  const auto raw_slot =
      static_cast<std::uint32_t>(openwow::ui::TruncateLuaNumberToI32(
          lua_tonumber(L, 1))) -
      1u;
  if (raw_slot >= static_cast<std::uint32_t>(kLuaRuneSlotLimit)) {
    return luaL_error(L, "Invalid slot");
  }
  const auto slot = static_cast<int>(raw_slot);

  auto* session = GetWorldSession(L);
  const auto* player = GetLocalPlayer(session);
  if (player == nullptr) {
    return 0;
  }

  const auto rune_type = session->runes().GetRuneType(slot);
  const auto query =
      session->runes().GetLuaCooldown(slot, player->GetRuneRegen(
                                                static_cast<std::uint8_t>(rune_type)));
  lua_pushnumber(L, static_cast<lua_Number>(query.start_ms) / 1000.0);
  lua_pushnumber(L, query.duration_seconds);
  lua_pushboolean(L, query.ready ? 1 : 0);
  return 3;
}

int LuaGetRuneType(lua_State* L) {
  auto* session = GetWorldSession(L);
  const auto* player = GetLocalPlayer(session);
  if (player == nullptr) {
    return 0;
  }

  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: Script_GetRuneType(slot)");
  }

  const auto slot = openwow::ui::SignedI32FromU32Bits(
      static_cast<std::uint32_t>(openwow::ui::TruncateLuaNumberToI32(
          lua_tonumber(L, 1))) -
      1u);
  if (!session->runes().IsLuaSlotValid(slot)) {
    return 0;
  }

  lua_pushnumber(L, static_cast<int>(session->runes().GetRuneType(slot)) + 1);
  return 1;
}

}
