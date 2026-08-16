#pragma once

#include "openwow/game/object_guid.h"

#include <cstdint>
#include <optional>
#include <string>

namespace openwow::game {

struct PlayerClassInfo {
  std::uint32_t classId{0};
  std::string className;
  std::string specName;
  std::string role;
};

struct GoldSilverCopper {
  std::uint32_t gold{0};
  std::uint32_t silver{0};
  std::uint32_t copper{0};
};

struct PlayerDataSnapshot {
  ObjectGuid guid;
  std::string name;
  std::uint32_t level{0};
  std::uint32_t xp{0};
  std::uint32_t nextLevelXP{0};
  std::uint64_t money{0};
  PlayerClassInfo classInfo;
  std::uint32_t restedXP{0};
  std::string guildName;
  std::string realmName;
  std::uint32_t titleId{0};
  std::uint32_t achievementPoints{0};
  std::uint32_t totalHKs{0};
  std::uint32_t honorPoints{0};
  std::uint32_t arenaPoints{0};
};

class PlayerDataView {
 public:
  PlayerDataView() = default;

  void SetPlayerData(const PlayerDataSnapshot& data);

  [[nodiscard]] std::optional<PlayerDataSnapshot> GetPlayerData() const;

  [[nodiscard]] std::string GetName() const;
  [[nodiscard]] std::uint32_t GetLevel() const;
  [[nodiscard]] std::uint32_t GetXP() const;
  [[nodiscard]] std::uint32_t GetNextLevelXP() const;
  [[nodiscard]] float GetXPPercent() const;
  [[nodiscard]] std::uint64_t GetMoney() const;
  [[nodiscard]] GoldSilverCopper GetMoneyAsGoldSilverCopper() const;
  [[nodiscard]] std::string GetGuildName() const;
  [[nodiscard]] std::string GetClassName() const;
  [[nodiscard]] std::uint32_t GetClassId() const;
  [[nodiscard]] std::uint32_t GetTitleId() const;
  [[nodiscard]] std::uint32_t GetAchievementPoints() const;

  [[nodiscard]] std::uint32_t GetTotalHKs() const;
  [[nodiscard]] std::uint32_t GetHonorPoints() const;
  [[nodiscard]] std::uint32_t GetArenaPoints() const;
  [[nodiscard]] std::uint32_t GetRestedXP() const;
  [[nodiscard]] std::string GetRealmName() const;

  [[nodiscard]] std::string FormatMoney() const;
  [[nodiscard]] std::string FormatLevel() const;
  [[nodiscard]] ObjectGuid GetGuid() const;

  [[nodiscard]] bool IsMaxLevel() const;
  void SetMaxLevel(std::uint32_t maxLevel);

  [[nodiscard]] bool HasData() const;

  void Reset();

 private:
  std::optional<PlayerDataSnapshot> data_;
  std::uint32_t maxLevel_{80};
};

}
