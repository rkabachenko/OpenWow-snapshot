#pragma once

#include "openwow/game/achievements/model/achievement_state_types.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>

namespace openwow::net::wotlk {
struct WorldPacket;
}

namespace openwow::game::achievement::protocol {

struct AchievementProtocolTimePoint final {
  std::chrono::seconds since_unix_epoch{};

  friend bool operator==(AchievementProtocolTimePoint,
                         AchievementProtocolTimePoint) = default;
};

class AchievementProtocolClock {
 public:
  virtual ~AchievementProtocolClock() = default;

  [[nodiscard]] virtual AchievementProtocolTimePoint Now() = 0;
};

[[nodiscard]] std::optional<AchievementDataSnapshot> DecodeAllAchievementData(
    std::span<const std::uint8_t> payload);
[[nodiscard]] std::optional<AchievementDataSnapshot> DecodeAllAchievementData(
    std::span<const std::uint8_t> payload, AchievementProtocolClock& clock);
[[nodiscard]] std::optional<AchievementEarnedInfo> DecodeAchievementEarned(
    std::span<const std::uint8_t> payload);
[[nodiscard]] std::optional<CriteriaProgress> DecodeCriteriaUpdate(
    std::span<const std::uint8_t> payload);
[[nodiscard]] std::optional<CriteriaProgress> DecodeCriteriaUpdate(
    std::span<const std::uint8_t> payload, AchievementProtocolClock& clock);
[[nodiscard]] std::optional<AchievementCriteriaId> DecodeCriteriaDeleted(
    std::span<const std::uint8_t> payload);
[[nodiscard]] std::optional<ServerFirstInfo> DecodeServerFirstAchievement(
    std::span<const std::uint8_t> payload);
[[nodiscard]] std::optional<InspectAchievementResult> DecodeInspectAchievements(
    std::span<const std::uint8_t> payload);
[[nodiscard]] std::optional<InspectAchievementResult> DecodeInspectAchievements(
    std::span<const std::uint8_t> payload, AchievementProtocolClock& clock);
[[nodiscard]] std::optional<AchievementId> DecodeAchievementDeleted(
    std::span<const std::uint8_t> payload);

[[nodiscard]] net::wotlk::WorldPacket BuildQueryInspectAchievements(
    ObjectGuid target);

}
