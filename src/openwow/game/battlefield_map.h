#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::game {

enum class BFMapNodeType : std::uint8_t {
  GraveyardAlliance,
  GraveyardHorde,
  GraveyardNeutral,
  TowerAlliance,
  TowerHorde,
  TowerNeutral,
  WorkshopAlliance,
  WorkshopHorde,
  WorkshopNeutral,
  Flag,
  Gate,
  Vehicle,
};

struct BFMapNode {
  std::uint32_t nodeId = 0;
  BFMapNodeType nodeType = BFMapNodeType::GraveyardNeutral;
  std::string name;
  float mapX = 0.0f;
  float mapY = 0.0f;
  float capturePercent = 0.0f;
  bool isContested = false;
  std::uint32_t controlFaction = 0;
};

class BattlefieldMap {
 public:
  BattlefieldMap() = default;

  void SetBattlefield(std::uint32_t zoneId, const std::string& name,
                      std::uint32_t mapTextureId);
  [[nodiscard]] std::uint32_t GetZoneId() const { return zone_id_; }
  [[nodiscard]] const std::string& GetName() const { return name_; }
  [[nodiscard]] std::uint32_t GetMapTextureId() const { return map_texture_id_; }

  void AddNode(const BFMapNode& node);
  void RemoveNode(std::uint32_t nodeId);
  void UpdateNode(std::uint32_t nodeId, const BFMapNode& node);
  [[nodiscard]] std::optional<BFMapNode> GetNode(std::uint32_t nodeId) const;
  [[nodiscard]] std::vector<BFMapNode> GetNodes() const;
  [[nodiscard]] std::vector<BFMapNode> GetNodesByType(BFMapNodeType type) const;
  [[nodiscard]] std::vector<BFMapNode> GetNodesByFaction(std::uint32_t faction) const;
  [[nodiscard]] std::uint32_t GetNodeCount() const;

  void SetAllianceScore(std::uint32_t score) { alliance_score_ = score; }
  [[nodiscard]] std::uint32_t GetAllianceScore() const { return alliance_score_; }
  void SetHordeScore(std::uint32_t score) { horde_score_ = score; }
  [[nodiscard]] std::uint32_t GetHordeScore() const { return horde_score_; }

  void SetTimer(float seconds) { timer_ = seconds; }
  [[nodiscard]] float GetTimer() const { return timer_; }

  void Update(float dt);
  [[nodiscard]] bool IsOpen() const { return open_; }
  void Open();
  void Close();
  void Clear();
  void Reset();

 private:
  std::uint32_t zone_id_ = 0;
  std::string name_;
  std::uint32_t map_texture_id_ = 0;
  std::unordered_map<std::uint32_t, BFMapNode> nodes_;
  std::uint32_t alliance_score_ = 0;
  std::uint32_t horde_score_ = 0;
  float timer_ = 0.0f;
  bool open_ = false;
};

}
