#include "openwow/game/quests/adapters/lua/completed_quest_lua_api.h"

#include "openwow/network/protocol/wotlk/opcodes.h"
#include "openwow/network/protocol/wotlk/world_packet.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"

#include "openwow/ui/runtime/lua/lua_call.h"

namespace openwow::ui::game::detail {

int LuaQueryQuestsCompleted(lua_State* state) {
  auto* session = GetWorldSession(state);
  if (session == nullptr) {
    return 0;
  }

  session->Send(openwow::net::wotlk::WorldPacket(
      openwow::net::wotlk::Opcode::CMSG_QUERY_QUESTS_COMPLETED));
  return 0;
}

int LuaGetQuestsCompleted(lua_State* state) {
  const auto* session = GetWorldSession(state);
  if (session == nullptr) {
    return openwow::ui::lua::LuaCall(state).PushBooleanSetTable({});
  }

  return openwow::ui::lua::LuaCall(state).PushBooleanSetTable(
      session->quests().completed_quests());
}

}
