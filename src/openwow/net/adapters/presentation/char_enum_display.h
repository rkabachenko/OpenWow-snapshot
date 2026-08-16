#pragma once

#include "openwow/game/object_guid.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::net {

struct CharEnumEquipSlot {
  uint32_t displayInfoId{0};
  uint8_t inventoryType{0};
  uint32_t enchantAuraId{0};
};

struct CharEnumEntry {
  game::ObjectGuid guid;
  std::string name;
  uint8_t race{0};
  uint8_t classId{0};
  uint8_t gender{0};
  uint8_t skin{0};
  uint8_t face{0};
  uint8_t hairStyle{0};
  uint8_t hairColor{0};
  uint8_t facialHair{0};
  uint8_t level{0};
  uint32_t zoneId{0};
  uint32_t mapId{0};
  float posX{0.0f};
  float posY{0.0f};
  float posZ{0.0f};
  uint32_t guildId{0};
  uint32_t charFlags{0};
  uint32_t customizationFlags{0};
  bool firstLogin{false};
  uint32_t petDisplayId{0};
  uint32_t petLevel{0};
  uint32_t petFamily{0};
  std::array<CharEnumEquipSlot, 23> equipment{};
};

namespace CharFlag {
  inline constexpr uint32_t LockedForTransfer = 0x04;
  inline constexpr uint32_t HideHelm          = 0x400;
  inline constexpr uint32_t HideCloak         = 0x800;
  inline constexpr uint32_t Ghost             = 0x2000;
  inline constexpr uint32_t Rename            = 0x4000;
  inline constexpr uint32_t LockedByBilling   = 0x01000000;
  inline constexpr uint32_t Declined          = 0x02000000;
  inline constexpr uint32_t Banned            = 0x80;
}

class CharEnumDisplay {
 public:
  static constexpr uint8_t kMaxCharactersPerRealm = 10;

  CharEnumDisplay() = default;

  void SetCharacters(std::vector<CharEnumEntry> chars);
  [[nodiscard]] const std::vector<CharEnumEntry>& GetCharacters() const;
  [[nodiscard]] std::optional<CharEnumEntry> GetCharacter(game::ObjectGuid guid) const;
  [[nodiscard]] size_t GetCharacterCount() const;

  [[nodiscard]] std::optional<game::ObjectGuid> GetSelectedCharacter() const;
  bool SelectCharacter(game::ObjectGuid guid);

  [[nodiscard]] uint8_t GetMaxCharacters() const;
  [[nodiscard]] bool CanCreateMore() const;

  [[nodiscard]] static std::string GetClassName(uint8_t classId);
  [[nodiscard]] static std::string GetRaceName(uint8_t race);
  [[nodiscard]] static std::string GetZoneName(uint32_t zoneId);
  [[nodiscard]] static std::string GetLevelText(uint8_t level);
  [[nodiscard]] static bool IsDeathKnight(uint8_t classId);
  [[nodiscard]] static bool HasBannedFlag(uint32_t charFlags);

  void Clear();

 private:
  std::vector<CharEnumEntry> characters_;
  std::optional<game::ObjectGuid> selected_;
};

}
