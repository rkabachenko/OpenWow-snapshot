
#include "openwow/game/battlefield_map.h"

namespace openwow::game {

void BattlefieldMap::SetBattlefield(std::uint32_t zoneId,
                                     const std::string& name,
                                     std::uint32_t mapTextureId) {
  zone_id_ = zoneId;
  name_ = name;
  map_texture_id_ = mapTextureId;
}

void BattlefieldMap::AddNode(const BFMapNode& node) {
  nodes_[node.nodeId] = node;
}

void BattlefieldMap::RemoveNode(std::uint32_t nodeId) {
  nodes_.erase(nodeId);
}

void BattlefieldMap::UpdateNode(std::uint32_t nodeId, const BFMapNode& node) {
  auto it = nodes_.find(nodeId);
  if (it != nodes_.end()) {
    it->second = node;
    it->second.nodeId = nodeId;
  }
}

std::optional<BFMapNode> BattlefieldMap::GetNode(std::uint32_t nodeId) const {
  auto it = nodes_.find(nodeId);
  if (it == nodes_.end()) return std::nullopt;
  return it->second;
}

std::vector<BFMapNode> BattlefieldMap::GetNodes() const {
  std::vector<BFMapNode> result;
  result.reserve(nodes_.size());
  for (const auto& [_, node] : nodes_) {
    result.push_back(node);
  }
  return result;
}

std::vector<BFMapNode> BattlefieldMap::GetNodesByType(BFMapNodeType type) const {
  std::vector<BFMapNode> result;
  for (const auto& [_, node] : nodes_) {
    if (node.nodeType == type) {
      result.push_back(node);
    }
  }
  return result;
}

std::vector<BFMapNode> BattlefieldMap::GetNodesByFaction(
    std::uint32_t faction) const {
  std::vector<BFMapNode> result;
  for (const auto& [_, node] : nodes_) {
    if (node.controlFaction == faction) {
      result.push_back(node);
    }
  }
  return result;
}

std::uint32_t BattlefieldMap::GetNodeCount() const {
  return static_cast<std::uint32_t>(nodes_.size());
}

void BattlefieldMap::Update(float dt) {
  if (!open_) return;
  if (timer_ > 0.0f) {
    timer_ -= dt;
    if (timer_ < 0.0f) timer_ = 0.0f;
  }
}

void BattlefieldMap::Open() { open_ = true; }
void BattlefieldMap::Close() { open_ = false; }

void BattlefieldMap::Clear() {
  nodes_.clear();
  alliance_score_ = 0;
  horde_score_ = 0;
  timer_ = 0.0f;
}

void BattlefieldMap::Reset() {
  Clear();
  zone_id_ = 0;
  name_.clear();
  map_texture_id_ = 0;
  open_ = false;
}

}
