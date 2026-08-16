#pragma once

#include "openwow/game/inventory/loot/loot_interaction.h"
#include "openwow/network/protocol/wotlk/world_packet.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace openwow::game::loot_protocol {

[[nodiscard]] std::optional<LootWindow> DecodeLootResponse(
    const std::uint8_t* data, std::size_t size);
[[nodiscard]] std::optional<std::pair<ObjectGuid, bool>>
DecodeLootReleaseResponse(const std::uint8_t* data, std::size_t size);
[[nodiscard]] std::optional<std::uint8_t> DecodeRemovedSlot(
    const std::uint8_t* data, std::size_t size);
[[nodiscard]] std::optional<LootMoneyNotify> DecodeMoneyNotify(
    const std::uint8_t* data, std::size_t size);
[[nodiscard]] std::optional<LootRollWon> DecodeRollWon(
    const std::uint8_t* data, std::size_t size);
[[nodiscard]] std::optional<LootItemNotify> DecodeItemNotify(
    const std::uint8_t* data, std::size_t size);
[[nodiscard]] std::optional<LootList> DecodeLootList(
    const std::uint8_t* data, std::size_t size);
[[nodiscard]] std::optional<LootMasterList> DecodeMasterList(
    const std::uint8_t* data, std::size_t size);
[[nodiscard]] std::optional<LootSlotChanged> DecodeSlotChanged(
    const std::uint8_t* data, std::size_t size);

[[nodiscard]] net::wotlk::WorldPacket EncodeLootRequest(ObjectGuid source);
[[nodiscard]] net::wotlk::WorldPacket EncodeReleaseRequest(ObjectGuid source);
[[nodiscard]] net::wotlk::WorldPacket EncodeTakeItemRequest(
    std::uint8_t wire_slot);
[[nodiscard]] net::wotlk::WorldPacket EncodeRollRequest(
    ObjectGuid source, std::uint32_t wire_slot, LootRollType roll);
[[nodiscard]] net::wotlk::WorldPacket EncodeMasterGiveRequest(
    ObjectGuid source, std::uint8_t wire_slot, ObjectGuid recipient);

}
