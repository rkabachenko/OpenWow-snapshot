#include "openwow/net/client_services_packet_sender.h"

#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/net/serialization/cdatastore_vtable.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace openwow::net {
namespace {

std::mutex g_packet_sender_mutex;
ClientServicesPacketSendFn g_packet_sender;

bool IsSpellListResendSuspect(const wotlk::Opcode opcode) {
  static const std::array<wotlk::Opcode, 14> kSuspects{
      wotlk::Opcode::MSG_TALENT_WIPE_CONFIRM,
      wotlk::Opcode::CMSG_LEARN_TALENT,
      wotlk::Opcode::CMSG_LEARN_PREVIEW_TALENTS,
      wotlk::Opcode::CMSG_LEARN_PREVIEW_TALENTS_PET,
      wotlk::Opcode::CMSG_UNLEARN_TALENTS,
      wotlk::Opcode::CMSG_UNLEARN_SKILL,
      wotlk::Opcode::CMSG_PET_LEARN_TALENT,
      wotlk::Opcode::CMSG_PET_UNLEARN_TALENTS,
      wotlk::Opcode::CMSG_ACTIVATETAXI,
      wotlk::Opcode::CMSG_ACTIVATETAXIEXPRESS,
      wotlk::Opcode::CMSG_SET_GLYPH_SLOT,
      wotlk::Opcode::CMSG_SET_GLYPH,
      wotlk::Opcode::CMSG_REMOVE_GLYPH,
      wotlk::Opcode::CMSG_SET_ACTIVE_TALENT_GROUP_OBSOLETE,
  };
  for (const wotlk::Opcode suspect : kSuspects) {
    if (suspect == opcode) {
      return true;
    }
  }
  return false;
}

bool DecodePacketStore(CDataStore store, wotlk::WorldPacket& packet) {
  if (store.write_pos < sizeof(std::uint32_t)) {
    return false;
  }

  store.read_pos = 0;

  std::uint32_t opcode = 0;
  if (!CDataStore_GetUInt32(store, opcode)) {
    return false;
  }

  const auto payload_bytes = store.write_pos - sizeof(std::uint32_t);
  std::vector<std::uint8_t> payload(payload_bytes);
  CDataStore_GetBytes(store, payload.data(), payload_bytes);
  if (store.read_pos > store.write_pos) {
    return false;
  }

  packet = wotlk::WorldPacket(static_cast<wotlk::Opcode>(opcode),
                              std::move(payload));
  return true;
}

}

void SetClientServicesPacketSendFn(ClientServicesPacketSendFn fn) {
  std::lock_guard<std::mutex> lock(g_packet_sender_mutex);
  g_packet_sender = std::move(fn);
}

bool ClientServices__SendPacket(const wotlk::WorldPacket& pkt) {
  if (IsSpellListResendSuspect(pkt.opcode)) {
    diagnostics::Log(
        diagnostics::LogLevel::kInfo,
        "ClientServices__SendPacket: spell-list-resend suspect sent (" +
            std::string(wotlk::OpcodeName(pkt.opcode)) + " 0x" +
            std::to_string(static_cast<unsigned>(pkt.opcode)) + ")");
  }

  ClientServicesPacketSendFn send_fn;
  {
    std::lock_guard<std::mutex> lock(g_packet_sender_mutex);
    send_fn = g_packet_sender;
  }

  if (!send_fn) {
    return false;
  }

  return send_fn(pkt);
}

bool ClientServices__SendPacket(const CDataStore& store) {
  wotlk::WorldPacket packet;
  if (!DecodePacketStore(store, packet)) {
    return false;
  }

  return ClientServices__SendPacket(packet);
}

}
