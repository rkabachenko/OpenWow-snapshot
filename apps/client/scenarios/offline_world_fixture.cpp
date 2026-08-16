#include "scenarios/offline_world_fixture.h"

#include "openwow/game/actions/model/action_assignments.h"
#include "openwow/network/protocol/wotlk/opcodes.h"

#include <cstddef>
#include <cstdint>

namespace openwow::client::offline_world_fixture {

namespace {

constexpr std::uint32_t kPrimaryActionSpell = 6603u;

}

openwow::net::wotlk::WorldPacket BuildInitialSpells() {
  openwow::net::wotlk::WorldPacket packet(
      openwow::net::wotlk::Opcode::SMSG_INITIAL_SPELLS);
  packet.AppendU8(0u);
  packet.AppendU16(1u);
  packet.AppendU32(kPrimaryActionSpell);
  packet.AppendU16(0u);
  packet.AppendU16(0u);
  return packet;
}

openwow::net::wotlk::WorldPacket BuildActionAssignments() {
  using openwow::game::actions::Action;
  using openwow::game::actions::ActionAssignmentSyncState;
  using openwow::game::actions::ActionKind;
  using openwow::game::actions::ActionSlot;

  openwow::net::wotlk::WorldPacket packet(
      openwow::net::wotlk::Opcode::SMSG_ACTION_BUTTONS);
  packet.payload.reserve(1u + ActionSlot::kCount * sizeof(std::uint32_t));
  packet.AppendU8(
      static_cast<std::uint8_t>(ActionAssignmentSyncState::kUpdate));

  constexpr auto primary_action =
      Action::Create(ActionKind::kSpell, kPrimaryActionSpell);
  static_assert(primary_action.has_value());
  packet.AppendU32(primary_action->Encode());
  for (std::size_t slot = 1; slot < ActionSlot::kCount; ++slot) {
    packet.AppendU32(0u);
  }
  return packet;
}

}
