#include "openwow/ui/surfaces/game/runtime/corpse_position_query.h"

#include "openwow/game/object_guid.h"
#include "openwow/game/transport_manager.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/network/protocol/wotlk/opcodes.h"
#include "openwow/network/protocol/wotlk/world_packet.h"

#include <cmath>

namespace openwow::ui::game {
namespace {

Vec3f TransformLocalPoint(
    const Vec3f local, const float x, const float y, const float z,
    const float orientation) {
  const float cosine = std::cos(orientation);
  const float sine = std::sin(orientation);
  return {
      x + local.x * cosine - local.y * sine,
      y + local.x * sine + local.y * cosine,
      z + local.z,
  };
}

CorpsePositionData Resolve(openwow::game::WorldSession* session) {
  if (session == nullptr) return {};
  const auto& corpse = session->corpse_query();
  if (!corpse.found) return {};

  CorpsePositionData result{
      .found = true,
      .map_id = corpse.map_id,
      .corpse_map_id = corpse.corpse_map_id,
      .position = {corpse.x, corpse.y, corpse.z},
  };
  if (corpse.transport_counter == 0) return result;

  const auto transport_guid = openwow::game::ObjectGuid::CreateGlobal(
      openwow::game::HighGuid::kMoTransport, corpse.transport_counter);
  if (const auto* transport =
          session->transport_mgr().GetTransport(transport_guid)) {
    const auto world =
        transport->GetWorldPosition(corpse.x, corpse.y, corpse.z, 0.0f);
    result.position = {world.x, world.y, world.z};
    session->misc().ResetCorpseTransportPositionQueryThrottle();
    return result;
  }

  if (session->misc().BeginCorpseTransportPositionQuery()) {
    openwow::net::wotlk::WorldPacket packet(
        openwow::net::wotlk::Opcode::CMSG_CORPSE_MAP_POSITION_QUERY);
    packet.AppendU32(corpse.transport_counter);
    session->Send(packet);
  }
  if (const auto& fallback = session->misc().last_corpse_map_pos()) {
    result.position = TransformLocalPoint(
        result.position, fallback->x, fallback->y, fallback->z,
        fallback->orientation);
  }
  return result;
}

}

CorpsePositionData GameUI_GetCorpsePositionData() {
  const auto* const manager = runtime::WorldUiRuntimeContext::FromActiveLua();
  return Resolve(manager != nullptr ? manager->world_session() : nullptr);
}

CorpsePositionData GameUI_GetCorpsePositionData(
    openwow::game::WorldSession& session) {
  return Resolve(&session);
}

Vec3f GameUI_GetCorpseMapPosition() {
  return GameUI_GetCorpsePositionData().position;
}

namespace detail {
void ResetCorpseMapPositionQueryThrottleForTests() {
  const auto* const manager = runtime::WorldUiRuntimeContext::FromActiveLua();
  if (auto* session = manager != nullptr ? manager->world_session() : nullptr;
      session != nullptr) {
    session->misc().ResetCorpseTransportPositionQueryThrottle();
  }
}
}

}
