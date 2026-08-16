#pragma once

#include "openwow/net/serialization/cdatastore_ops.h"
#include "openwow/network/protocol/wotlk/world_packet.h"

#include <functional>

namespace openwow::net {

using ClientServicesPacketSendFn =
    std::function<bool(const wotlk::WorldPacket&)>;

void SetClientServicesPacketSendFn(ClientServicesPacketSendFn fn);

[[nodiscard]] bool ClientServices__SendPacket(
    const wotlk::WorldPacket& pkt);

[[nodiscard]] bool ClientServices__SendPacket(const CDataStore& store);

}
