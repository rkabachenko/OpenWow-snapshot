#pragma once

#include "openwow/game/commerce/mail/mail_interaction.h"
#include "openwow/network/protocol/wotlk/world_packet.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace openwow::game::mail_protocol {

struct MailAttachment {
  std::uint8_t slot = 0;
  std::uint64_t item_guid = 0;
};

[[nodiscard]] std::optional<MailListSnapshot> DecodeMailList(
    const std::uint8_t* data, std::size_t size);
[[nodiscard]] std::optional<MailSendResult> DecodeActionResult(
    const std::uint8_t* data, std::size_t size);
[[nodiscard]] std::optional<float> DecodeReceivedMailDelay(
    const std::uint8_t* data, std::size_t size);
[[nodiscard]] std::optional<std::uint64_t> DecodeMailboxGuid(
    const std::uint8_t* data, std::size_t size);
[[nodiscard]] std::optional<NextMailTimeSnapshot> DecodeNextMailTime(
    const std::uint8_t* data, std::size_t size);
[[nodiscard]] net::wotlk::WorldPacket EncodeDelete(
    std::uint64_t mailbox, std::uint32_t mail_id, MailDeleteReason reason);
[[nodiscard]] net::wotlk::WorldPacket EncodeReturnToSender(
    std::uint64_t mailbox, std::uint32_t mail_id, std::uint64_t sender);
[[nodiscard]] net::wotlk::WorldPacket EncodeTakeItem(
    std::uint64_t mailbox, std::uint32_t mail_id, std::uint32_t item_guid_low);
[[nodiscard]] net::wotlk::WorldPacket EncodeTakeMoney(
    std::uint64_t mailbox, std::uint32_t mail_id);
[[nodiscard]] net::wotlk::WorldPacket EncodeQueryNextMailTime();
[[nodiscard]] net::wotlk::WorldPacket EncodeGetMailList(
    std::uint64_t mailbox);
[[nodiscard]] net::wotlk::WorldPacket EncodeSendMail(
    std::uint64_t mailbox, std::string_view recipient,
    std::string_view subject, std::string_view body, std::uint32_t stationery,
    std::uint32_t money, std::uint32_t cod,
    const std::vector<MailAttachment>& attachments, std::uint32_t package_id);
[[nodiscard]] net::wotlk::WorldPacket EncodeMarkRead(
    std::uint64_t mailbox, std::uint32_t mail_id);
[[nodiscard]] net::wotlk::WorldPacket EncodeCreateTextItem(
    std::uint64_t mailbox, std::uint32_t mail_id);
[[nodiscard]] net::wotlk::WorldPacket EncodeFollowup(
    const MailFollowupCommand& command);

}
