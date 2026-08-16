#pragma once

#include "openwow/game/inventory/items/item_interactions.h"
#include "openwow/game/object_guid.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace openwow::game {

struct EnchantmentObservation {
  ObjectGuid target;
  ObjectGuid caster;
  std::uint32_t item_id = 0;
  std::uint32_t enchantment_id = 0;
};

struct EnchantmentTimeObservation {
  ObjectGuid item;
  std::uint32_t slot = 0;
  std::uint32_t seconds = 0;
  ObjectGuid owner;
};

struct ChargeObservation {
  ObjectGuid item;
  std::array<std::uint32_t, 5> charges{};
};

struct SocketObservation {
  ObjectGuid item;
  std::array<std::uint32_t, 3> gem_item_ids{};
};

struct ItemCooldownObservation {
  ObjectGuid item;
  std::uint32_t spell_id = 0;
};

struct ItemDurationObservation {
  ObjectGuid item;
  std::uint32_t seconds = 0;
};

struct ReadFailureObservation {
  ObjectGuid item;
  std::uint32_t status = 0;
};

struct ItemTextObservation {
  ObjectGuid item;
  std::string text;
};

[[nodiscard]] std::optional<EnchantmentObservation>
decode_enchantment(std::span<const std::uint8_t> payload);
[[nodiscard]] std::optional<EnchantmentTimeObservation>
decode_enchantment_time(std::span<const std::uint8_t> payload);
[[nodiscard]] std::optional<ChargeObservation>
decode_item_charges(std::span<const std::uint8_t> payload);
[[nodiscard]] std::optional<RefundQuote>
decode_refund_quote(std::span<const std::uint8_t> payload);
[[nodiscard]] std::optional<RefundResult>
decode_refund_result(std::span<const std::uint8_t> payload);
[[nodiscard]] std::optional<SocketObservation>
decode_socket_result(std::span<const std::uint8_t> payload);
[[nodiscard]] std::optional<ItemCooldownObservation>
decode_item_cooldown(std::span<const std::uint8_t> payload);
[[nodiscard]] std::optional<ItemDurationObservation>
decode_item_duration(std::span<const std::uint8_t> payload);
[[nodiscard]] std::optional<std::uint32_t>
decode_bank_slot_result(std::span<const std::uint8_t> payload);
[[nodiscard]] std::optional<ObjectGuid>
decode_read_ok(std::span<const std::uint8_t> payload);
[[nodiscard]] std::optional<ReadFailureObservation>
decode_read_failure(std::span<const std::uint8_t> payload);
[[nodiscard]] std::optional<ItemTextObservation>
decode_item_text(std::span<const std::uint8_t> payload);

}
