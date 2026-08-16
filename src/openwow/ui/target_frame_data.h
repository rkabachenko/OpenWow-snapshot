#pragma once

#include "openwow/game/object_guid.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace openwow::ui {

enum class TargetReaction : std::uint8_t {
  Hostile = 0,
  Unfriendly = 1,
  Neutral = 2,
  Friendly = 3,
};

enum class CreatureType : std::uint8_t {
  Normal = 0,
  Elite = 1,
  RareElite = 2,
  WorldBoss = 3,
  Rare = 4,
};

enum class TargetPowerType : std::uint8_t {
  Mana = 0,
  Rage = 1,
  Focus = 2,
  Energy = 3,
  Happiness = 4,
  Runes = 5,
  RunicPower = 6,
};

struct TargetFrameData {
  openwow::game::ObjectGuid unitGuid;
  std::string name;
  std::int32_t level{1};
  std::uint8_t classId{0};
  CreatureType creatureType{CreatureType::Normal};
  TargetReaction reaction{TargetReaction::Neutral};
  bool isPlayer{false};
  bool isTapped{false};
  bool isTappedByMe{false};
  std::string classification;
  float healthPercent{100.0f};
  float powerPercent{0.0f};
  TargetPowerType powerType{TargetPowerType::Mana};
};

class TargetFrameProvider {
 public:
  TargetFrameProvider() = default;

  void SetTarget(const TargetFrameData& data);
  [[nodiscard]] std::optional<TargetFrameData> GetTarget() const;
  [[nodiscard]] bool HasTarget() const;
  void ClearTarget();

  void SetFocus(const TargetFrameData& data);
  [[nodiscard]] std::optional<TargetFrameData> GetFocus() const;
  [[nodiscard]] bool HasFocus() const;
  void ClearFocus();

  void SetTargetOfTarget(const TargetFrameData& data);
  [[nodiscard]] std::optional<TargetFrameData> GetTargetOfTarget() const;
  [[nodiscard]] bool HasTargetOfTarget() const;

  [[nodiscard]] static std::uint32_t GetReactionColor(TargetReaction reaction);

  [[nodiscard]] static std::uint32_t GetLevelColor(std::int32_t playerLevel,
                                                    std::int32_t targetLevel);

  [[nodiscard]] static std::string GetCreatureTypeString(CreatureType type);

  [[nodiscard]] bool IsTargetAttackable() const;

  [[nodiscard]] std::uint32_t GetTargetClassColor() const;

  [[nodiscard]] static std::uint32_t GetClassColor(std::uint8_t classId);

  void Reset();

 private:
  mutable std::mutex mutex_;
  std::optional<TargetFrameData> target_;
  std::optional<TargetFrameData> focus_;
  std::optional<TargetFrameData> targetOfTarget_;
};

}
