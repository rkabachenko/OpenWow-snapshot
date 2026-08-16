#pragma once

#include "openwow/game/object_guid.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::ui {

enum class PowerType : std::uint8_t {
  Mana = 0,
  Rage = 1,
  Focus = 2,
  Energy = 3,
  Happiness = 4,
  Runes = 5,
  RunicPower = 6,
  Health = 7,
};

struct UnitFrameData {
  openwow::game::ObjectGuid unitGuid;
  std::string name;
  std::uint32_t level{0};
  std::uint8_t classId{0};
  std::uint8_t raceId{0};

  std::uint32_t healthCurrent{0};
  std::uint32_t healthMax{0};

  std::uint32_t powerCurrent{0};
  std::uint32_t powerMax{0};
  PowerType powerType{PowerType::Mana};

  bool isDeadOrGhost{false};
  bool isConnected{true};
  bool isInCombat{false};
  bool hasAggro{false};
  bool isGroupLeader{false};
  bool isRaidAssistant{false};

  std::uint8_t raidIcon{0};

  std::uint32_t auraCount{0};
  std::uint32_t debuffCount{0};

  std::uint32_t absorbAmount{0};
  std::uint32_t incomingHeals{0};
};

class UnitFrameProvider {
 public:
  UnitFrameProvider() = default;

  void SetUnitData(const std::string& unitId, const UnitFrameData& data);
  [[nodiscard]] std::optional<UnitFrameData> GetUnitData(
      const std::string& unitId) const;
  [[nodiscard]] bool HasUnit(const std::string& unitId) const;
  void RemoveUnit(const std::string& unitId);

  [[nodiscard]] std::vector<std::string> GetActiveUnits() const;

  [[nodiscard]] std::uint32_t GetUnitCount() const;

  void SetHealth(const std::string& unitId, std::uint32_t current,
                 std::uint32_t max);
  void SetPower(const std::string& unitId, std::uint32_t current,
                std::uint32_t max, PowerType type);
  void SetLevel(const std::string& unitId, std::uint32_t level);
  void SetInCombat(const std::string& unitId, bool inCombat);
  void SetRaidIcon(const std::string& unitId, std::uint32_t icon);
  void SetAbsorb(const std::string& unitId, std::uint32_t amount);
  void SetIncomingHeals(const std::string& unitId, std::uint32_t amount);

  [[nodiscard]] float GetHealthPercent(const std::string& unitId) const;

  [[nodiscard]] float GetPowerPercent(const std::string& unitId) const;

  [[nodiscard]] static std::uint32_t GetPowerColor(PowerType type);

  void Clear();
  void Reset();

 private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, UnitFrameData> units_;
};

}
