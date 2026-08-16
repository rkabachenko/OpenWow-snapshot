#pragma once

#include "openwow/game/object_guid.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace openwow::game {

struct AchievementId final {
  std::uint32_t value = 0;

  friend bool operator==(AchievementId, AchievementId) = default;
};

struct AchievementIdHash final {
  [[nodiscard]] std::size_t operator()(AchievementId id) const noexcept {
    return id.value;
  }
};

struct AchievementCriteriaId final {
  std::uint32_t value = 0;

  friend bool operator==(AchievementCriteriaId, AchievementCriteriaId) =
      default;
};

struct AchievementCriteriaIdHash final {
  [[nodiscard]] std::size_t operator()(AchievementCriteriaId id) const
      noexcept {
    return id.value;
  }
};

struct PackedAchievementTime final {
  constexpr PackedAchievementTime() = default;

  static constexpr PackedAchievementTime FromWireValue(
      const std::uint32_t value) {
    return PackedAchievementTime(value);
  }

  [[nodiscard]] constexpr std::uint32_t ToWireValue() const {
    return value_;
  }

  [[nodiscard]] constexpr std::uint8_t Minute() const {
    return value_ & 0x3F;
  }
  [[nodiscard]] constexpr std::uint8_t Hour() const {
    return (value_ >> 6) & 0x1F;
  }
  [[nodiscard]] constexpr std::uint8_t Weekday() const {
    return (value_ >> 11) & 0x07;
  }
  [[nodiscard]] constexpr std::uint8_t Day() const {
    return ((value_ >> 14) & 0x3F) + 1;
  }
  [[nodiscard]] constexpr std::uint8_t Month() const {
    return (value_ >> 20) & 0x0F;
  }
  [[nodiscard]] constexpr std::uint16_t Year() const {
    return ((value_ >> 24) & 0x1F) + 100;
  }

  friend bool operator==(PackedAchievementTime, PackedAchievementTime) =
      default;

 private:
  constexpr explicit PackedAchievementTime(const std::uint32_t value)
      : value_(value) {}

  std::uint32_t value_ = 0;
};

struct AchievementProgressCounter final {
  std::uint64_t value = 0;

  friend bool operator==(AchievementProgressCounter,
                         AchievementProgressCounter) = default;
};

enum class CriteriaProgressFlag : std::uint32_t {
  kTimedFailureRemovesLocalState = 0x1,
};

struct CriteriaProgressFlags final {
  std::uint32_t value = 0;

  [[nodiscard]] constexpr bool Contains(
      const CriteriaProgressFlag flag) const {
    return (value & static_cast<std::uint32_t>(flag)) != 0;
  }

  friend bool operator==(CriteriaProgressFlags, CriteriaProgressFlags) =
      default;
};

struct AchievementElapsedTime final {
  std::chrono::seconds value{};

  friend bool operator==(AchievementElapsedTime, AchievementElapsedTime) =
      default;
};

struct AchievementEarnFlags final {
  std::uint32_t value = 0;

  friend bool operator==(AchievementEarnFlags, AchievementEarnFlags) = default;
};

struct ServerFirstLinkType final {
  std::uint32_t value = 0;

  friend bool operator==(ServerFirstLinkType, ServerFirstLinkType) = default;
};

enum class AchievementOwnerRelation : std::uint8_t {
  kOtherPlayer,
  kActivePlayer,
};

enum class AchievementRemovalOutcome : std::uint8_t {
  kNotPresent,
  kRemoved,
};

enum class AchievementUiReadiness : std::uint8_t {
  kWaitingForData,
  kReady,
};

enum class ComparisonAchievementDataStatus : std::uint8_t {
  kUnavailable,
  kAvailable,
};

enum class InspectApplicationStatus : std::uint8_t {
  kNotAttempted,
  kIgnoredWrongTarget,
  kApplied,
};

enum class ComparisonAchievementPointsStatus : std::uint8_t {
  kPending,
  kReady,
};

struct CompletedAchievement final {
  AchievementId id;
  PackedAchievementTime completion_date;
};

struct CriteriaProgress final {
  AchievementCriteriaId criteria_id;
  AchievementProgressCounter counter;
  ObjectGuid player_guid;
  CriteriaProgressFlags flags;
  PackedAchievementTime date;
  AchievementElapsedTime elapsed_since_update;
  AchievementElapsedTime elapsed_since_start;
};

struct AchievementEarnedInfo final {
  ObjectGuid player_guid;
  AchievementId achievement_id;
  PackedAchievementTime earn_date;
  AchievementEarnFlags flags;
  AchievementOwnerRelation owner_relation =
      AchievementOwnerRelation::kOtherPlayer;
};

struct ServerFirstInfo final {
  std::string name;
  ObjectGuid player_guid;
  AchievementId achievement_id;
  ServerFirstLinkType link_type;
};

struct InspectAchievementResult final {
  ObjectGuid target;
  std::vector<CompletedAchievement> achievements;
  std::vector<CriteriaProgress> criteria;
};

struct CriteriaDeleteResult final {
  AchievementCriteriaId criteria_id;
  AchievementRemovalOutcome outcome = AchievementRemovalOutcome::kNotPresent;
};

struct AchievementDeleteResult final {
  AchievementId achievement_id;
  AchievementRemovalOutcome outcome = AchievementRemovalOutcome::kNotPresent;
};

struct AchievementDataSnapshot final {
  std::vector<CompletedAchievement> achievements;
  std::vector<CriteriaProgress> criteria;
};

}
