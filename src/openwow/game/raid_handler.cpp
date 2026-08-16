#include "openwow/game/raid_handler.h"

#include "openwow/game/packet_reader.h"

namespace openwow::game {

bool SummonInteraction::ApplyRequest(const std::uint8_t* data,
                                     const std::size_t size,
                                     const std::uint32_t current_time_ms) {
  PacketReader reader(data, size);
  PendingSummonInfo next;
  if (!reader.ReadU64(next.summoner_guid) ||
      !reader.ReadU32(next.zone_id) ||
      !reader.ReadU32(next.timeout_ms)) {
    return false;
  }
  next.expiration_time_ms = current_time_ms + next.timeout_ms;
  pending_ = next;
  return true;
}

std::uint32_t SummonInteraction::SecondsRemaining(
    const std::uint32_t current_time_ms) const noexcept {
  if (pending_.expiration_time_ms == 0) {
    return 0;
  }
  const auto remaining = static_cast<std::int32_t>(
      pending_.expiration_time_ms - current_time_ms);
  return remaining > 0 ? static_cast<std::uint32_t>(remaining / 1000) : 0;
}

std::optional<RaidTargetUpdate> DecodeRaidTargetUpdate(
    const std::uint8_t* data, const std::size_t size) {
  PacketReader reader(data, size);
  std::uint8_t mode = 0;
  if (!reader.ReadU8(mode)) {
    return std::nullopt;
  }
  if (mode == 0) {
    std::uint64_t setter = 0;
    if (!reader.ReadU64(setter)) {
      return std::nullopt;
    }
  }

  RaidTargetUpdate update{.replace_all = mode != 0};
  while (reader.Remaining() != 0) {
    RaidTargetIcon icon;
    if (reader.Remaining() < 9 || !reader.ReadU8(icon.icon_id) ||
        !reader.ReadU64(icon.target_guid)) {
      return std::nullopt;
    }
    if (icon.icon_id < 8) {
      update.icons.push_back(icon);
    }
  }
  return update;
}

std::optional<RaidReadyCheck> DecodeRaidReadyCheck(
    const std::uint8_t* data, const std::size_t size) {
  PacketReader reader(data, size);
  RaidReadyCheck ready_check;
  if (!reader.ReadU64(ready_check.initiator_guid)) {
    return std::nullopt;
  }
  return ready_check;
}

std::optional<RaidReadyCheckConfirm> DecodeRaidReadyCheckConfirm(
    const std::uint8_t* data, const std::size_t size) {
  PacketReader reader(data, size);
  RaidReadyCheckConfirm confirmation;
  std::uint8_t ready = 0;
  if (!reader.ReadU64(confirmation.player_guid) ||
      !reader.ReadU8(ready)) {
    return std::nullopt;
  }
  confirmation.ready = ready != 0;
  return confirmation;
}

bool DecodeRaidReadyCheckFinished(const std::uint8_t* data,
                                  const std::size_t size) {
  return data != nullptr || size == 0;
}

std::optional<PartyAssignment> DecodePartyAssignment(
    const std::uint8_t* data, const std::size_t size) {
  PacketReader reader(data, size);
  PartyAssignment assignment;
  std::uint8_t apply = 0;
  if (!reader.ReadU8(assignment.role) || !reader.ReadU8(apply) ||
      !reader.ReadU64(assignment.target_guid)) {
    return std::nullopt;
  }
  assignment.apply = apply != 0;
  return assignment;
}

}
