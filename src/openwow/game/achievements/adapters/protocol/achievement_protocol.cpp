#include "openwow/game/achievements/adapters/protocol/achievement_protocol.h"

#include "openwow/game/packet_reader.h"
#include "openwow/network/protocol/wotlk/world_packet.h"

#include <chrono>
#include <utility>

namespace openwow::game::achievement::protocol {
namespace {

constexpr std::uint32_t kTerminatedListEnd = 0xFFFFFFFFu;

class SystemAchievementProtocolClock final : public AchievementProtocolClock {
 public:
  [[nodiscard]] AchievementProtocolTimePoint Now() override {
    return AchievementProtocolTimePoint{
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()),
    };
  }
};

[[nodiscard]] AchievementElapsedTime ElapsedSince(
    const AchievementProtocolTimePoint now,
    const std::uint32_t wire_timestamp) {
  return AchievementElapsedTime{
      now.since_unix_epoch - std::chrono::seconds(wire_timestamp),
  };
}

[[nodiscard]] bool ReadCriteriaEntry(PacketReader& reader,
                                     CriteriaProgress& progress,
                                     AchievementProtocolClock& clock) {
  std::uint32_t criteria_id = 0;
  if (!reader.ReadU32(criteria_id)) {
    return false;
  }
  progress.criteria_id = AchievementCriteriaId{criteria_id};

  ObjectGuid counter_guid;
  std::uint32_t flags = 0;
  std::uint32_t date = 0;
  std::uint32_t elapsed_since_update = 0;
  std::uint32_t elapsed_since_start = 0;
  if (!reader.ReadPackedGuid(counter_guid) ||
      !reader.ReadPackedGuid(progress.player_guid) ||
      !reader.ReadU32(flags) ||
      !reader.ReadU32(date) ||
      !reader.ReadU32(elapsed_since_update) ||
      !reader.ReadU32(elapsed_since_start)) {
    return false;
  }
  progress.counter = AchievementProgressCounter{counter_guid.GetRawValue()};
  progress.flags = CriteriaProgressFlags{flags};
  progress.date = PackedAchievementTime::FromWireValue(date);
  progress.elapsed_since_update =
      ElapsedSince(clock.Now(), elapsed_since_update);
  progress.elapsed_since_start = ElapsedSince(clock.Now(), elapsed_since_start);
  return true;
}

[[nodiscard]] bool ReadAchievementAndCriteriaData(
    PacketReader& reader,
    AchievementDataSnapshot& snapshot,
    const AchievementProtocolTimePoint now) {
  for (;;) {
    std::uint32_t achievement_id = 0;
    if (!reader.ReadU32(achievement_id)) {
      return false;
    }
    if (achievement_id == kTerminatedListEnd) {
      break;
    }

    std::uint32_t completion_date = 0;
    if (!reader.ReadU32(completion_date)) {
      return false;
    }
    snapshot.achievements.push_back(
        CompletedAchievement{
            AchievementId{achievement_id},
            PackedAchievementTime::FromWireValue(completion_date),
        });
  }

  for (;;) {
    std::uint32_t criteria_id = 0;
    if (!reader.ReadU32(criteria_id)) {
      return false;
    }
    if (criteria_id == kTerminatedListEnd) {
      break;
    }

    CriteriaProgress progress;
    progress.criteria_id = AchievementCriteriaId{criteria_id};
    ObjectGuid counter_guid;
    std::uint32_t flags = 0;
    std::uint32_t date = 0;
    std::uint32_t elapsed_since_update = 0;
    std::uint32_t elapsed_since_start = 0;
    if (!reader.ReadPackedGuid(counter_guid) ||
        !reader.ReadPackedGuid(progress.player_guid) ||
        !reader.ReadU32(flags) ||
        !reader.ReadU32(date) ||
        !reader.ReadU32(elapsed_since_update) ||
        !reader.ReadU32(elapsed_since_start)) {
      return false;
    }
    progress.counter = AchievementProgressCounter{counter_guid.GetRawValue()};
    progress.flags = CriteriaProgressFlags{flags};
    progress.date = PackedAchievementTime::FromWireValue(date);
    progress.elapsed_since_update = ElapsedSince(now, elapsed_since_update);
    progress.elapsed_since_start = ElapsedSince(now, elapsed_since_start);
    snapshot.criteria.push_back(progress);
  }
  return true;
}

}

std::optional<AchievementDataSnapshot> DecodeAllAchievementData(
    const std::span<const std::uint8_t> payload) {
  SystemAchievementProtocolClock clock;
  return DecodeAllAchievementData(payload, clock);
}

std::optional<AchievementDataSnapshot> DecodeAllAchievementData(
    const std::span<const std::uint8_t> payload,
    AchievementProtocolClock& clock) {
  PacketReader reader(payload.data(), payload.size());
  AchievementDataSnapshot snapshot;
  const auto now = clock.Now();
  if (!ReadAchievementAndCriteriaData(reader, snapshot, now)) {
    return std::nullopt;
  }
  return snapshot;
}

std::optional<AchievementEarnedInfo> DecodeAchievementEarned(
    const std::span<const std::uint8_t> payload) {
  PacketReader reader(payload.data(), payload.size());
  AchievementEarnedInfo earned;
  std::uint32_t achievement_id = 0;
  std::uint32_t earn_date = 0;
  std::uint32_t flags = 0;
  if (!reader.ReadPackedGuid(earned.player_guid) ||
      !reader.ReadU32(achievement_id) ||
      !reader.ReadU32(earn_date) ||
      !reader.ReadU32(flags)) {
    return std::nullopt;
  }
  earned.achievement_id = AchievementId{achievement_id};
  earned.earn_date = PackedAchievementTime::FromWireValue(earn_date);
  earned.flags = AchievementEarnFlags{flags};
  return earned;
}

std::optional<CriteriaProgress> DecodeCriteriaUpdate(
    const std::span<const std::uint8_t> payload) {
  SystemAchievementProtocolClock clock;
  return DecodeCriteriaUpdate(payload, clock);
}

std::optional<CriteriaProgress> DecodeCriteriaUpdate(
    const std::span<const std::uint8_t> payload,
    AchievementProtocolClock& clock) {
  PacketReader reader(payload.data(), payload.size());
  CriteriaProgress progress;
  if (!ReadCriteriaEntry(reader, progress, clock)) {
    return std::nullopt;
  }
  return progress;
}

std::optional<AchievementCriteriaId> DecodeCriteriaDeleted(
    const std::span<const std::uint8_t> payload) {
  PacketReader reader(payload.data(), payload.size());
  std::uint32_t criteria_id = 0;
  if (!reader.ReadU32(criteria_id)) {
    return std::nullopt;
  }
  return AchievementCriteriaId{criteria_id};
}

std::optional<ServerFirstInfo> DecodeServerFirstAchievement(
    const std::span<const std::uint8_t> payload) {
  PacketReader reader(payload.data(), payload.size());
  ServerFirstInfo server_first;
  std::uint64_t player_guid = 0;
  std::uint32_t achievement_id = 0;
  std::uint32_t link_type = 0;
  if (!reader.ReadCString(server_first.name) ||
      !reader.ReadU64(player_guid) ||
      !reader.ReadU32(achievement_id) ||
      !reader.ReadU32(link_type)) {
    return std::nullopt;
  }
  server_first.player_guid = ObjectGuid(player_guid);
  server_first.achievement_id = AchievementId{achievement_id};
  server_first.link_type = ServerFirstLinkType{link_type};
  return server_first;
}

std::optional<InspectAchievementResult> DecodeInspectAchievements(
    const std::span<const std::uint8_t> payload) {
  SystemAchievementProtocolClock clock;
  return DecodeInspectAchievements(payload, clock);
}

std::optional<InspectAchievementResult> DecodeInspectAchievements(
    const std::span<const std::uint8_t> payload,
    AchievementProtocolClock& clock) {
  const auto now = clock.Now();
  PacketReader reader(payload.data(), payload.size());
  InspectAchievementResult inspect;
  if (!reader.ReadPackedGuid(inspect.target)) {
    return std::nullopt;
  }

  AchievementDataSnapshot snapshot;
  if (!ReadAchievementAndCriteriaData(reader, snapshot, now)) {
    return std::nullopt;
  }
  inspect.achievements = std::move(snapshot.achievements);
  inspect.criteria = std::move(snapshot.criteria);
  return inspect;
}

std::optional<AchievementId> DecodeAchievementDeleted(
    const std::span<const std::uint8_t> payload) {
  PacketReader reader(payload.data(), payload.size());
  std::uint32_t achievement_id = 0;
  if (!reader.ReadU32(achievement_id)) {
    return std::nullopt;
  }
  return AchievementId{achievement_id};
}

net::wotlk::WorldPacket BuildQueryInspectAchievements(
    const ObjectGuid target) {
  net::wotlk::WorldPacket packet(
      net::wotlk::Opcode::CMSG_QUERY_INSPECT_ACHIEVEMENTS);
  const auto packed_guid = target.Pack();
  packet.AppendBytes(packed_guid.data(), packed_guid.size());
  return packet;
}

}
