#include "openwow/game/inventory/items/item_protocol.h"

#include "openwow/game/packet_reader.h"

namespace openwow::game {
namespace {

template <typename T, typename Read>
std::optional<T> decode_exact(const std::span<const std::uint8_t> payload,
                              Read read) {
  PacketReader packet(payload);
  T value{};
  if (!read(packet, value) || packet.Remaining() != 0) {
    return std::nullopt;
  }
  return value;
}

}

std::optional<EnchantmentObservation> decode_enchantment(
    const std::span<const std::uint8_t> payload) {
  return decode_exact<EnchantmentObservation>(
      payload, [](PacketReader& packet, EnchantmentObservation& value) {
        return packet.ReadPackedGuid(value.target) &&
               packet.ReadPackedGuid(value.caster) &&
               packet.ReadU32(value.item_id) &&
               packet.ReadU32(value.enchantment_id);
      });
}

std::optional<EnchantmentTimeObservation> decode_enchantment_time(
    const std::span<const std::uint8_t> payload) {
  return decode_exact<EnchantmentTimeObservation>(
      payload, [](PacketReader& packet,
                  EnchantmentTimeObservation& value) {
        return packet.ReadGuid(value.item) &&
               packet.ReadU32(value.slot) &&
               packet.ReadU32(value.seconds) &&
               packet.ReadGuid(value.owner);
      });
}

std::optional<ChargeObservation> decode_item_charges(
    const std::span<const std::uint8_t> payload) {
  return decode_exact<ChargeObservation>(
      payload, [](PacketReader& packet, ChargeObservation& value) {
        if (!packet.ReadGuid(value.item)) {
          return false;
        }
        for (auto& charge : value.charges) {
          if (!packet.ReadU32(charge)) {
            return false;
          }
        }
        return true;
      });
}

std::optional<RefundQuote> decode_refund_quote(
    const std::span<const std::uint8_t> payload) {
  return decode_exact<RefundQuote>(
      payload, [](PacketReader& packet, RefundQuote& value) {
        std::uint32_t ignored = 0;
        if (!packet.ReadGuid(value.item) ||
            !packet.ReadU32(value.money) ||
            !packet.ReadU32(value.honor) ||
            !packet.ReadU32(value.arena)) {
          return false;
        }
        for (auto& item : value.required_items) {
          if (!packet.ReadU32(item.item_id) ||
              !packet.ReadU32(item.count)) {
            return false;
          }
        }
        return packet.ReadU32(ignored) &&
               packet.ReadU32(value.time_left);
      });
}

std::optional<RefundResult> decode_refund_result(
    const std::span<const std::uint8_t> payload) {
  PacketReader packet(payload);
  RefundResult value{};
  if (!packet.ReadGuid(value.item) || !packet.ReadU32(value.error)) {
    return std::nullopt;
  }
  if (value.error == 0) {
    if (!packet.ReadU32(value.money) ||
        !packet.ReadU32(value.honor) ||
        !packet.ReadU32(value.arena)) {
      return std::nullopt;
    }
    for (auto& item : value.returned_items) {
      if (!packet.ReadU32(item.item_id) || !packet.ReadU32(item.count)) {
        return std::nullopt;
      }
    }
  }
  return packet.Remaining() == 0 ? std::optional(value) : std::nullopt;
}

std::optional<SocketObservation> decode_socket_result(
    const std::span<const std::uint8_t> payload) {
  return decode_exact<SocketObservation>(
      payload, [](PacketReader& packet, SocketObservation& value) {
        if (!packet.ReadGuid(value.item)) {
          return false;
        }
        for (auto& gem : value.gem_item_ids) {
          if (!packet.ReadU32(gem)) {
            return false;
          }
        }
        return true;
      });
}

std::optional<ItemCooldownObservation> decode_item_cooldown(
    const std::span<const std::uint8_t> payload) {
  return decode_exact<ItemCooldownObservation>(
      payload, [](PacketReader& packet, ItemCooldownObservation& value) {
        return packet.ReadGuid(value.item) &&
               packet.ReadU32(value.spell_id);
      });
}

std::optional<ItemDurationObservation> decode_item_duration(
    const std::span<const std::uint8_t> payload) {
  return decode_exact<ItemDurationObservation>(
      payload, [](PacketReader& packet, ItemDurationObservation& value) {
        return packet.ReadGuid(value.item) &&
               packet.ReadU32(value.seconds);
      });
}

std::optional<std::uint32_t> decode_bank_slot_result(
    const std::span<const std::uint8_t> payload) {
  return decode_exact<std::uint32_t>(
      payload, [](PacketReader& packet, std::uint32_t& value) {
        return packet.ReadU32(value);
      });
}

std::optional<ObjectGuid> decode_read_ok(
    const std::span<const std::uint8_t> payload) {
  return decode_exact<ObjectGuid>(
      payload, [](PacketReader& packet, ObjectGuid& value) {
        return packet.ReadGuid(value);
      });
}

std::optional<ReadFailureObservation> decode_read_failure(
    const std::span<const std::uint8_t> payload) {
  return decode_exact<ReadFailureObservation>(
      payload, [](PacketReader& packet, ReadFailureObservation& value) {
        return packet.ReadGuid(value.item) &&
               packet.ReadU32(value.status);
      });
}

std::optional<ItemTextObservation> decode_item_text(
    const std::span<const std::uint8_t> payload) {
  PacketReader packet(payload);
  std::uint8_t missing = 0;
  ItemTextObservation value{};
  if (!packet.ReadU8(missing)) {
    return std::nullopt;
  }
  if (missing != 0) {
    return packet.Remaining() == 0 ? std::optional(value) : std::nullopt;
  }
  if (!packet.ReadGuid(value.item) ||
      !packet.ReadCString(value.text, 8000) ||
      packet.Remaining() != 0) {
    return std::nullopt;
  }
  return value;
}

}
