#pragma once

#include "openwow/game/actions/model/action_assignments.h"
#include "openwow/network/protocol/wotlk/world_packet.h"

#include <optional>

namespace openwow::game::actions::adapters::protocol {

struct DecodedActionAssignments {
  ActionAssignmentSyncState state;
  std::optional<ActionAssignments::Storage> values;
};

[[nodiscard]] std::optional<DecodedActionAssignments> DecodeActionAssignments(
    const net::wotlk::WorldPacket& packet,
    const ActionAssignments::Storage* previous = nullptr);

[[nodiscard]] net::wotlk::WorldPacket EncodeActionAssignment(
    ActionSlot slot, const Action& action);

}
