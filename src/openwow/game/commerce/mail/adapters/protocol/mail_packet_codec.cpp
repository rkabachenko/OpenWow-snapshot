#include "openwow/game/commerce/mail/adapters/protocol/mail_packet_codec.h"

#include "openwow/core/storm_string.h"
#include "openwow/game/packet_reader.h"
#include "openwow/network/protocol/wotlk/opcodes.h"

#include <algorithm>
#include <array>

namespace openwow::game::mail_protocol {
namespace {

constexpr std::uint32_t kTruncatedSendMailEquipError = 0x28u;

bool DecodeAttachment(PacketReader& reader, MailItemInfo& item) {
  if (!reader.ReadU8(item.index) || !reader.ReadU32(item.item_guid_low) ||
      !reader.ReadU32(item.item_entry)) {
    return false;
  }
  for (MailEnchantData& enchant : item.enchants) {
    if (!reader.ReadU32(enchant.enchant_id) ||
        !reader.ReadU32(enchant.enchant_duration) ||
        !reader.ReadU32(enchant.enchant_charges)) {
      return false;
    }
  }
  std::uint8_t ignored = 0;
  return reader.ReadI32(item.random_property_id) &&
         reader.ReadU32(item.suffix_factor) &&
         reader.ReadU32(item.stack_count) &&
         reader.ReadU32(item.spell_charges) &&
         reader.ReadU32(item.max_durability) &&
         reader.ReadU32(item.durability) && reader.ReadU8(ignored);
}

std::optional<std::uint8_t> SelectAttachmentSlot(
    const std::uint8_t requested,
    const std::array<bool, kMaxInboxAttachmentSlots>& occupied) {
  if (requested < occupied.size() && !occupied[requested]) {
    return requested;
  }
  for (std::uint8_t slot = 0; slot < occupied.size(); ++slot) {
    if (!occupied[slot]) {
      return slot;
    }
  }
  return std::nullopt;
}

}

std::optional<MailListSnapshot> DecodeMailList(const std::uint8_t* data,
                                               const std::size_t size) {
  PacketReader reader(data, size);
  MailListSnapshot snapshot;
  std::uint8_t shown_count = 0;
  if (!reader.ReadU32(snapshot.real_count) || !reader.ReadU8(shown_count)) {
    return std::nullopt;
  }
  snapshot.entries.reserve(shown_count);
  for (std::uint8_t index = 0; index < shown_count; ++index) {
    MailEntry entry;
    std::uint8_t type = 0;
    if (!reader.ReadU16(entry.message_size) ||
        !reader.ReadU32(entry.message_id) || !reader.ReadU8(type)) {
      return std::nullopt;
    }
    entry.message_type = static_cast<MailType>(type);
    if (entry.message_type == MailType::kNormal) {
      if (!reader.ReadU64(entry.sender_guid)) {
        return std::nullopt;
      }
    } else if (!reader.ReadU32(entry.sender_entry)) {
      return std::nullopt;
    }
    if (!reader.ReadU32(entry.cod) ||
        !reader.ReadU32(entry.package_icon_id) ||
        !reader.ReadU32(entry.stationery) || !reader.ReadU32(entry.money) ||
        !reader.ReadU32(entry.checked) ||
        !reader.ReadFloat(entry.expiration_time) ||
        !reader.ReadU32(entry.mail_template_id) ||
        !reader.ReadCString(entry.subject) || !reader.ReadCString(entry.body)) {
      return std::nullopt;
    }

    std::uint8_t item_count = 0;
    if (!reader.ReadU8(item_count)) {
      return std::nullopt;
    }
    std::array<MailItemInfo, kMaxInboxAttachmentSlots> attachments{};
    std::array<bool, kMaxInboxAttachmentSlots> occupied{};
    for (std::uint8_t item_index = 0; item_index < item_count; ++item_index) {
      MailItemInfo attachment;
      if (!DecodeAttachment(reader, attachment)) {
        return std::nullopt;
      }
      const auto slot = SelectAttachmentSlot(attachment.index, occupied);
      if (slot) {
        attachment.index = *slot;
        attachments[*slot] = attachment;
        occupied[*slot] = true;
      }
    }
    for (std::uint8_t slot = 0; slot < occupied.size(); ++slot) {
      if (occupied[slot]) {
        entry.items.push_back(attachments[slot]);
      }
    }
    if (entry.message_type == MailType::kCalendar) {
      entry.sender_guid = core::SStrToUInt64(entry.subject.c_str());
    }
    snapshot.entries.push_back(std::move(entry));
  }
  return snapshot;
}

std::optional<MailSendResult> DecodeActionResult(const std::uint8_t* data,
                                                 const std::size_t size) {
  PacketReader reader(data, size);
  MailSendResult result;
  std::uint32_t action = 0;
  std::uint32_t error = 0;
  if (!reader.ReadU32(result.mail_id) || !reader.ReadU32(action) ||
      !reader.ReadU32(error)) {
    return std::nullopt;
  }
  result.action = static_cast<MailResponseType>(action);
  result.error = static_cast<MailResult>(error);
  if (result.error == MailResult::kEquipError) {
    std::uint32_t equip_error = kTruncatedSendMailEquipError;
    (void)reader.ReadU32(equip_error);
    result.equip_error = equip_error;
  } else if (result.action == MailResponseType::kItemTaken &&
             (result.error == MailResult::kOk ||
              result.error == MailResult::kItemHasExpired) &&
             reader.HasBytes(8)) {
    (void)reader.ReadU32(result.item_guid_low);
    (void)reader.ReadU32(result.item_count);
  }
  return result;
}

std::optional<float> DecodeReceivedMailDelay(const std::uint8_t* data,
                                             const std::size_t size) {
  PacketReader reader(data, size);
  float delay = 0.0f;
  return reader.ReadFloat(delay) ? std::optional{delay} : std::nullopt;
}

std::optional<std::uint64_t> DecodeMailboxGuid(const std::uint8_t* data,
                                               const std::size_t size) {
  PacketReader reader(data, size);
  std::uint64_t guid = 0;
  return reader.ReadU64(guid) ? std::optional{guid} : std::nullopt;
}

std::optional<NextMailTimeSnapshot> DecodeNextMailTime(
    const std::uint8_t* data, const std::size_t size) {
  PacketReader reader(data, size);
  NextMailTimeSnapshot snapshot;
  std::uint32_t sender_count = 0;
  if (!reader.ReadFloat(snapshot.next_mail_time) ||
      !reader.ReadU32(sender_count)) {
    return std::nullopt;
  }

  const auto decoded_sender_count = std::min<std::size_t>(
      sender_count, kMaxNextMailSenders);
  snapshot.senders.reserve(decoded_sender_count);
  for (std::size_t index = 0; index < decoded_sender_count; ++index) {
    NextMailTimeSender sender;
    if (!reader.ReadU64(sender.sender_guid) ||
        !reader.ReadU32(sender.sender_entry) ||
        !reader.ReadU32(sender.message_type) ||
        !reader.ReadU32(sender.stationery) ||
        !reader.ReadFloat(sender.time_left)) {
      return std::nullopt;
    }
    snapshot.senders.push_back(sender);
  }
  return snapshot;
}

net::wotlk::WorldPacket EncodeDelete(const std::uint64_t mailbox,
                                     const std::uint32_t mail_id,
                                     const MailDeleteReason reason) {
  net::wotlk::WorldPacket packet(net::wotlk::Opcode::CMSG_MAIL_DELETE);
  packet.AppendU64(mailbox);
  packet.AppendU32(mail_id);
  packet.AppendU32(static_cast<std::uint32_t>(reason));
  return packet;
}

net::wotlk::WorldPacket EncodeReturnToSender(const std::uint64_t mailbox,
                                             const std::uint32_t mail_id,
                                             const std::uint64_t sender) {
  net::wotlk::WorldPacket packet(
      net::wotlk::Opcode::CMSG_MAIL_RETURN_TO_SENDER);
  packet.AppendU64(mailbox);
  packet.AppendU32(mail_id);
  packet.AppendU64(sender);
  return packet;
}

net::wotlk::WorldPacket EncodeTakeItem(const std::uint64_t mailbox,
                                       const std::uint32_t mail_id,
                                       const std::uint32_t item_guid_low) {
  net::wotlk::WorldPacket packet(net::wotlk::Opcode::CMSG_MAIL_TAKE_ITEM);
  packet.AppendU64(mailbox);
  packet.AppendU32(mail_id);
  packet.AppendU32(item_guid_low);
  return packet;
}

net::wotlk::WorldPacket EncodeTakeMoney(const std::uint64_t mailbox,
                                        const std::uint32_t mail_id) {
  net::wotlk::WorldPacket packet(net::wotlk::Opcode::CMSG_MAIL_TAKE_MONEY);
  packet.AppendU64(mailbox);
  packet.AppendU32(mail_id);
  return packet;
}

net::wotlk::WorldPacket EncodeQueryNextMailTime() {
  return net::wotlk::WorldPacket(net::wotlk::Opcode::MSG_QUERY_NEXT_MAIL_TIME);
}

net::wotlk::WorldPacket EncodeGetMailList(const std::uint64_t mailbox) {
  net::wotlk::WorldPacket packet(net::wotlk::Opcode::CMSG_GET_MAIL_LIST);
  packet.AppendU64(mailbox);
  return packet;
}

net::wotlk::WorldPacket EncodeSendMail(
    const std::uint64_t mailbox, const std::string_view recipient,
    const std::string_view subject, const std::string_view body,
    const std::uint32_t stationery, const std::uint32_t money,
    const std::uint32_t cod, const std::vector<MailAttachment>& attachments,
    const std::uint32_t package_id) {
  net::wotlk::WorldPacket packet(net::wotlk::Opcode::CMSG_SEND_MAIL);
  packet.AppendU64(mailbox);
  packet.AppendString(recipient);
  packet.AppendString(subject);
  packet.AppendString(body);
  packet.AppendU32(stationery);
  packet.AppendU32(package_id);
  const auto attachment_count =
      static_cast<std::uint8_t>(std::min<std::size_t>(attachments.size(), 16));
  packet.AppendU8(attachment_count);
  for (std::size_t index = 0; index < attachment_count; ++index) {
    const MailAttachment& attachment = attachments[index];
    packet.AppendU8(attachment.slot);
    packet.AppendU64(attachment.item_guid);
  }
  packet.AppendU32(money);
  packet.AppendU32(cod);
  packet.AppendU64(0);
  packet.AppendU8(0);
  return packet;
}

net::wotlk::WorldPacket EncodeMarkRead(const std::uint64_t mailbox,
                                       const std::uint32_t mail_id) {
  net::wotlk::WorldPacket packet(net::wotlk::Opcode::CMSG_MAIL_MARK_AS_READ);
  packet.AppendU64(mailbox);
  packet.AppendU32(mail_id);
  return packet;
}

net::wotlk::WorldPacket EncodeCreateTextItem(const std::uint64_t mailbox,
                                             const std::uint32_t mail_id) {
  net::wotlk::WorldPacket packet(
      net::wotlk::Opcode::CMSG_MAIL_CREATE_TEXT_ITEM);
  packet.AppendU64(mailbox);
  packet.AppendU32(mail_id);
  return packet;
}

net::wotlk::WorldPacket EncodeFollowup(const MailFollowupCommand& command) {
  switch (command.kind) {
    case MailFollowupKind::kDelete:
      return EncodeDelete(command.mailbox_guid, command.mail_id,
                          command.delete_reason);
    case MailFollowupKind::kReturnToSender:
      return EncodeReturnToSender(command.mailbox_guid, command.mail_id,
                                  command.sender_guid);
    case MailFollowupKind::kTakeItem:
      return EncodeTakeItem(command.mailbox_guid, command.mail_id,
                            command.item_guid_low);
    case MailFollowupKind::kTakeMoney:
      return EncodeTakeMoney(command.mailbox_guid, command.mail_id);
  }
  return EncodeQueryNextMailTime();
}

}
