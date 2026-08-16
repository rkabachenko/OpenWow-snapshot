#include "openwow/game/actions/adapters/protocol/wotlk_action_packets.h"

#include "openwow/network/protocol/wotlk/opcodes.h"

#include <algorithm>

namespace openwow::game::actions::adapters::protocol {

std::optional<DecodedActionAssignments> DecodeActionAssignments(
    const net::wotlk::WorldPacket& packet,
    const ActionAssignments::Storage* previous) {
  if (packet.opcode != net::wotlk::Opcode::SMSG_ACTION_BUTTONS ||
      packet.payload.empty()) {
    return std::nullopt;
  }

  const auto state_value = packet.payload.front();
  if (state_value > 2) {
    return std::nullopt;
  }

  const auto state = static_cast<ActionAssignmentSyncState>(state_value);
  if (state == ActionAssignmentSyncState::kBeginSync) {
    return DecodedActionAssignments{state, std::nullopt};
  }

  ActionAssignments::Storage values =
      previous != nullptr ? *previous : ActionAssignments::Storage{};
  const auto available_slots =
      std::min<std::size_t>(ActionSlot::kCount,
                            (packet.payload.size() - 1) / sizeof(std::uint32_t));
  for (std::size_t index = 0; index < available_slots; ++index) {
    const auto offset = 1 + index * sizeof(std::uint32_t);
    const auto packed =
        static_cast<std::uint32_t>(packet.payload[offset]) |
        (static_cast<std::uint32_t>(packet.payload[offset + 1]) << 8) |
        (static_cast<std::uint32_t>(packet.payload[offset + 2]) << 16) |
        (static_cast<std::uint32_t>(packet.payload[offset + 3]) << 24);
    values[index] = Action::Decode(packed);
  }
  return DecodedActionAssignments{state, values};
}

net::wotlk::WorldPacket EncodeActionAssignment(ActionSlot slot,
                                                const Action& action) {
  net::wotlk::WorldPacket packet(
      net::wotlk::Opcode::CMSG_SET_ACTION_BUTTON);
  packet.AppendU8(slot.wire_value());
  packet.AppendU32(action.Encode());
  return packet;
}

}
