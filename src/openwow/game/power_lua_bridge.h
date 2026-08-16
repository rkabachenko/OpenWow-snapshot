#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <tuple>

namespace openwow::game {

class PowerLuaBridge {
 public:
  static PowerLuaBridge& Get();

  static constexpr int kMana = 0;
  static constexpr int kRage = 1;
  static constexpr int kFocus = 2;
  static constexpr int kEnergy = 3;
  static constexpr int kHappiness = 4;
  static constexpr int kRunes = 5;
  static constexpr int kRunicPower = 6;

  struct PowerTypeResult {
    int powerType{0};
    std::string powerToken;
  };

  struct ClassResult {
    std::string localizedName;
    std::string englishClass;
    std::uint8_t classId{0};
  };

  struct RaceResult {
    std::string localizedName;
    std::string englishRace;
    std::uint8_t raceId{0};
  };

  [[nodiscard]] std::int32_t UnitPower(const std::string& unitId,
                                       int powerType = -1) const;

  [[nodiscard]] std::int32_t UnitPowerMax(const std::string& unitId,
                                          int powerType = -1) const;

  [[nodiscard]] PowerTypeResult UnitPowerType(
      const std::string& unitId) const;

  [[nodiscard]] std::int32_t UnitHealth(const std::string& unitId) const;

  [[nodiscard]] std::int32_t UnitHealthMax(const std::string& unitId) const;

  [[nodiscard]] std::int32_t UnitMana(const std::string& unitId) const;

  [[nodiscard]] std::int32_t UnitManaMax(const std::string& unitId) const;

  [[nodiscard]] std::int32_t UnitLevel(const std::string& unitId) const;

  [[nodiscard]] std::string UnitName(const std::string& unitId) const;

  [[nodiscard]] ClassResult UnitClass(const std::string& unitId) const;

  [[nodiscard]] RaceResult UnitRace(const std::string& unitId) const;

  [[nodiscard]] std::uint8_t UnitSex(const std::string& unitId) const;

  [[nodiscard]] bool UnitIsDeadOrGhost(const std::string& unitId) const;

  [[nodiscard]] bool UnitIsConnected(const std::string& unitId) const;

  [[nodiscard]] bool UnitIsUnit(const std::string& unitId1,
                                const std::string& unitId2) const;

  [[nodiscard]] bool UnitExists(const std::string& unitId) const;

  [[nodiscard]] static std::string PowerTypeToken(int powerType);

  [[nodiscard]] static std::string ClassNameFromId(std::uint8_t classId);

  [[nodiscard]] static std::string ClassFileFromId(std::uint8_t classId);

  [[nodiscard]] static std::string RaceNameFromId(std::uint8_t raceId);

  [[nodiscard]] static std::string RaceFileFromId(std::uint8_t raceId);

 private:
  PowerLuaBridge() = default;
};

}
