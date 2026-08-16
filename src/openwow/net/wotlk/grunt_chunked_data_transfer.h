#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "openwow/net/wotlk/grunt_packet_io.h"

namespace openwow::net::wotlk {

inline constexpr std::size_t kMaxAccountDataChunkSize = 8000;

inline constexpr std::uint8_t kAccountDataChunkOpcode = 0x41;

inline constexpr std::uint32_t kDefaultAccountDataTypes[] = {
    0x656E5553u,
    0x6B6F4B52u,
    0x66724652u,
    0x64654445u,
    0x7A68434Eu,
    0x7A685457u,
    0x65734553u,
    0x65734D58u,
    0x72755255u,
};
inline constexpr std::size_t kDefaultAccountDataTypeCount = 9;

using AccountDataProvider = std::function<std::vector<std::uint8_t>(
    std::uint32_t request_id,
    std::uint32_t data_type)>;

using AuthPacketSendFn =
    std::function<bool(const std::vector<std::uint8_t>&)>;

void SendChunkedAccountData(
    std::uint32_t request_id,
    std::uint32_t data_type,
    const AccountDataProvider& data_provider,
    const AuthPacketSendFn& chunk_send_fn,
    const AuthPacketSendFn& checked_send_fn);

PacketHandlerResult HandleAccountDataRequest(
    const std::uint8_t* data,
    std::size_t size,
    std::size_t& read_pos,
    const AccountDataProvider& data_provider,
    const AuthPacketSendFn& chunk_send_fn,
    const AuthPacketSendFn& checked_send_fn);

}
