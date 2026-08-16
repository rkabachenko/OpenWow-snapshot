#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::game {

enum class WorldPvPZoneType : std::uint8_t {
  Wintergrasp       = 0,
  Halaa             = 1,
  SpiritTowers      = 2,
  EasternPlaguelands = 3,
  Silithus          = 4,
};

enum class WorldPvPObjectiveState : std::uint8_t {
  Neutral   = 0,
  Alliance  = 1,
  Horde     = 2,
  Contested = 3,
};

struct WorldPvPObjective {
  std::uint32_t            objective_id{0};
  std::string              name;
  WorldPvPObjectiveState   state{WorldPvPObjectiveState::Neutral};
  float                    capture_progress{0.0f};
  std::uint32_t            controlling_faction{0};
};

struct WorldPvPZoneData {
  WorldPvPZoneType  zone_type{WorldPvPZoneType::Wintergrasp};
  std::string       zone_name;
  std::uint32_t     map_id{0};
  bool              is_active{false};
  std::uint32_t     time_to_next_battle{0};
  std::uint32_t     controlling_faction{0};
  std::vector<WorldPvPObjective> objectives;
};

class WorldPvPManager {
 public:
  void AddZone(const WorldPvPZoneData& zone);

  [[nodiscard]] std::optional<WorldPvPZoneData> GetZone(
      WorldPvPZoneType type) const;

  [[nodiscard]] std::vector<WorldPvPZoneData> GetAllZones() const;

  void SetObjectiveState(WorldPvPZoneType zone_type,
                         std::uint32_t objective_id,
                         WorldPvPObjectiveState state, float progress);

  [[nodiscard]] std::optional<WorldPvPObjective> GetObjective(
      WorldPvPZoneType zone_type, std::uint32_t objective_id) const;

  void SetControllingFaction(WorldPvPZoneType zone_type,
                             std::uint32_t faction);

  [[nodiscard]] std::uint32_t GetControllingFaction(
      WorldPvPZoneType zone_type) const;

  void SetBattleTimer(WorldPvPZoneType zone_type, std::uint32_t seconds);

  [[nodiscard]] std::uint32_t GetBattleTimer(
      WorldPvPZoneType zone_type) const;

  [[nodiscard]] bool IsActive(WorldPvPZoneType zone_type) const;
  void SetActive(WorldPvPZoneType zone_type, bool active);

  [[nodiscard]] static std::string GetStateName(
      WorldPvPObjectiveState state);

  void Update(float dt);

  void Reset();

 private:

  WorldPvPZoneData* FindZone(WorldPvPZoneType type);
  const WorldPvPZoneData* FindZone(WorldPvPZoneType type) const;

  std::unordered_map<std::uint8_t, WorldPvPZoneData> zones_;

  std::unordered_map<std::uint8_t, float> timer_accumulators_;
};

}
