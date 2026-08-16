#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace openwow::game {

struct DanceId final {
  std::uint32_t value = 0;

  friend bool operator==(DanceId, DanceId) = default;
};

struct DanceIdHash final {
  [[nodiscard]] std::size_t operator()(DanceId id) const noexcept {
    return id.value;
  }
};

struct DanceSequenceId final {
  std::uint32_t value = 0;

  friend bool operator==(DanceSequenceId, DanceSequenceId) = default;
};

struct DancePlaybackStep final {
  std::uint32_t value = 0;

  friend bool operator==(DancePlaybackStep, DancePlaybackStep) = default;
};

struct DanceSequencePosition final {
  std::uint32_t value = 0;

  friend bool operator==(DanceSequencePosition,
                         DanceSequencePosition) = default;
};

struct DancePlaybackSeed final {
  std::uint32_t value = 0;

  friend bool operator==(DancePlaybackSeed, DancePlaybackSeed) = default;
};

struct DanceChecksum final {
  std::uint32_t value = 0;

  friend bool operator==(DanceChecksum, DanceChecksum) = default;
};

struct DanceSystemMessageId final {
  std::uint32_t value = 0;

  friend bool operator==(DanceSystemMessageId,
                         DanceSystemMessageId) = default;
};

struct DanceUnitGuid final {
  std::uint64_t value = 0;

  friend bool operator==(DanceUnitGuid, DanceUnitGuid) = default;
};

struct LearnedDanceMoveMask final {
  std::uint64_t value = 0;

  friend bool operator==(LearnedDanceMoveMask, LearnedDanceMoveMask) = default;
};

struct DanceMoveId final {
  std::int16_t value = 0;

  friend bool operator==(DanceMoveId, DanceMoveId) = default;
};

struct DanceMoveChancePercent final {
  std::uint8_t value = 0;

  friend bool operator==(DanceMoveChancePercent,
                         DanceMoveChancePercent) = default;
};

struct DanceEmoteAnimationId final {
  std::uint32_t value = 0;

  friend bool operator==(DanceEmoteAnimationId,
                         DanceEmoteAnimationId) = default;
};

struct DanceAnimationDataId final {
  std::uint32_t value = 0;

  friend bool operator==(DanceAnimationDataId,
                         DanceAnimationDataId) = default;
};

struct DanceSoundKitId final {
  std::uint32_t value = 0;

  friend bool operator==(DanceSoundKitId, DanceSoundKitId) = default;
};

struct DanceDelayDuration final {
  std::chrono::seconds value{};

  friend bool operator==(DanceDelayDuration,
                         DanceDelayDuration) = default;
};

enum class DancePlayerClass : std::uint8_t {
  kWarrior = 1,
  kPaladin = 2,
  kHunter = 3,
  kRogue = 4,
  kPriest = 5,
  kDeathKnight = 6,
  kShaman = 7,
  kMage = 8,
  kWarlock = 9,
  kDruid = 11,
};

struct DanceClassMask final {
  std::uint32_t value = 0;

  friend bool operator==(DanceClassMask, DanceClassMask) = default;
};

struct DanceLearnedMoveIndex final {
  std::uint32_t value = 0;

  friend bool operator==(DanceLearnedMoveIndex,
                         DanceLearnedMoveIndex) = default;
};

enum class DanceMoveResolutionMode : std::uint8_t {
  kDirect = 0,
  kCatalogFallback = 1,
};

enum class DancePlaybackState : std::uint8_t {
  kInactive,
  kActive,
};

[[nodiscard]] constexpr DancePlaybackState ToDancePlaybackState(
    const bool is_active) {
  return is_active ? DancePlaybackState::kActive
                   : DancePlaybackState::kInactive;
}

enum class DanceQueryStatus : std::uint8_t {
  kFound,
  kMissing,
};

enum class DanceManagementOperation : std::uint8_t {
  kCreate = 1,
  kUpdate = 2,
  kRemove = 4,
};

class DanceManagementOperations final {
 public:
  constexpr void Add(const DanceManagementOperation operation) {
    bits_ |= static_cast<std::uint8_t>(operation);
  }

  [[nodiscard]] constexpr bool Contains(
      const DanceManagementOperation operation) const {
    return (bits_ & static_cast<std::uint8_t>(operation)) != 0;
  }

  friend bool operator==(DanceManagementOperations,
                         DanceManagementOperations) = default;

 private:
  std::uint8_t bits_ = 0;
};

}
