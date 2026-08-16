#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace openwow::game {

enum class AvTowerState : std::uint8_t {
  kIntact      = 0,
  kUnderAssault = 1,
  kDestroyed   = 2,
};

enum class AvTower : std::uint8_t {

  kDunBaldarNorth = 0,
  kDunBaldarSouth = 1,
  kIcewing        = 2,
  kStonehearth    = 3,

  kIcebloodTower  = 4,
  kTowerPoint     = 5,
  kFrostwolfWest  = 6,
  kFrostwolfEast  = 7,
  kCount          = 8,
};

enum class AvGraveyard : std::uint8_t {
  kAidStation      = 0,
  kStormpikeGY     = 1,
  kStonehearth     = 2,
  kSnowfall        = 3,
  kIceblood        = 4,
  kFrostwolfGY     = 5,
  kFrostwolfRelief = 6,
  kCount           = 7,
};

enum class AvGraveyardOwner : std::uint8_t {
  kNeutral  = 0,
  kAlliance = 1,
  kHorde    = 2,
};

class BgAlteracValley {
 public:

  static constexpr std::int32_t kWsAllianceReinforcements = 3127;
  static constexpr std::int32_t kWsHordeReinforcements    = 3128;

  struct TowerWorldStates {
    std::int32_t intact;
    std::int32_t under_assault;
    std::int32_t destroyed;
  };

  static constexpr TowerWorldStates kTowerWS[8] = {

    {1361, 1375, 1370},
    {1362, 1374, 1371},
    {1363, 1379, 1372},
    {1364, 1378, 1373},

    {1385, 1390, 1368},
    {1384, 1389, 1367},
    {1382, 1387, 1365},
    {1383, 1388, 1366},
  };

  struct GraveyardWorldStates {
    std::int32_t alliance;
    std::int32_t horde;
    std::int32_t contested;
  };

  static constexpr GraveyardWorldStates kGraveyardWS[7] = {
    {1325, 1326, 0},
    {1333, 1334, 0},
    {1302, 1301, 1304},
    {1966, 1967, 1968},
    {1346, 1347, 1348},
    {1337, 1338, 0},
    {1343, 1344, 0},
  };

  static constexpr int kStartingReinforcements = 600;
  static constexpr int kReinforcementsPerTower = 75;

  [[nodiscard]] static bool IsRelevantWorldState(std::int32_t ws_id);
  void OnWorldStateUpdate(std::int32_t ws_id, std::int32_t value);

  void Update(float dt);

  [[nodiscard]] int GetAllianceReinforcements() const { return alliance_reinforcements_; }
  [[nodiscard]] int GetHordeReinforcements() const { return horde_reinforcements_; }

  [[nodiscard]] AvTowerState GetTowerState(AvTower tower) const;
  [[nodiscard]] int GetAllianceTowersIntact() const;
  [[nodiscard]] int GetHordeTowersIntact() const;
  [[nodiscard]] int GetAllianceTowersDestroyed() const;
  [[nodiscard]] int GetHordeTowersDestroyed() const;

  [[nodiscard]] AvGraveyardOwner GetGraveyardOwner(AvGraveyard gy) const;

  [[nodiscard]] bool IsBossAlive(bool alliance) const;

  [[nodiscard]] bool IsFinished() const;

  [[nodiscard]] static std::string_view GetTowerName(AvTower tower);
  [[nodiscard]] static std::string_view GetGraveyardName(AvGraveyard gy);

  void Reset();

 private:
  int alliance_reinforcements_{kStartingReinforcements};
  int horde_reinforcements_{kStartingReinforcements};
  std::array<AvTowerState, static_cast<std::size_t>(AvTower::kCount)> towers_{};
  std::array<AvGraveyardOwner, static_cast<std::size_t>(AvGraveyard::kCount)> graveyards_{};
  bool alliance_boss_alive_{true};
  bool horde_boss_alive_{true};
};

}
