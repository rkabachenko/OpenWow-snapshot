
#include "openwow/game/gathering_node.h"

namespace openwow::game {

void GatheringNodeDisplay::AddNode(const GatherNodeEntry& entry) {

    for (auto& n : nodes_) {
        if (n.guid.GetRawValue() == entry.guid.GetRawValue()) {
            n = entry;
            return;
        }
    }
    nodes_.push_back(entry);
}

void GatheringNodeDisplay::RemoveNode(ObjectGuid guid) {
    std::erase_if(nodes_, [&](const GatherNodeEntry& n) {
        return n.guid.GetRawValue() == guid.GetRawValue();
    });
}

std::optional<GatherNodeEntry> GatheringNodeDisplay::GetNode(ObjectGuid guid) const {
    for (const auto& n : nodes_) {
        if (n.guid.GetRawValue() == guid.GetRawValue()) return n;
    }
    return std::nullopt;
}

std::vector<GatherNodeEntry> GatheringNodeDisplay::GetNodesByType(GatherNodeType type) const {
    std::vector<GatherNodeEntry> result;
    for (const auto& n : nodes_) {
        if (n.nodeType == type) result.push_back(n);
    }
    return result;
}

std::vector<GatherNodeEntry> GatheringNodeDisplay::GetTrackedNodes() const {
    std::vector<GatherNodeEntry> result;
    for (const auto& n : nodes_) {
        if (n.isTracked) result.push_back(n);
    }
    return result;
}

std::vector<GatherNodeEntry> GatheringNodeDisplay::GetReachableNodes() const {
    std::vector<GatherNodeEntry> result;
    for (const auto& n : nodes_) {
        if (n.isReachable) result.push_back(n);
    }
    return result;
}

std::optional<GatherNodeEntry> GatheringNodeDisplay::GetNearestNode(
    GatherNodeType type, float playerX, float playerY, float playerZ) const {
    std::optional<GatherNodeEntry> nearest;
    float bestDist2 = std::numeric_limits<float>::max();

    for (const auto& n : nodes_) {
        if (n.nodeType != type) continue;
        float dx = n.x - playerX;
        float dy = n.y - playerY;
        float dz = n.z - playerZ;
        float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 < bestDist2) {
            bestDist2 = d2;
            nearest = n;
        }
    }
    return nearest;
}

std::vector<GatherNodeEntry> GatheringNodeDisplay::GetNodesInRange(
    float x, float y, float z, float range) const {
    std::vector<GatherNodeEntry> result;
    float r2 = range * range;
    for (const auto& n : nodes_) {
        float dx = n.x - x;
        float dy = n.y - y;
        float dz = n.z - z;
        if (dx * dx + dy * dy + dz * dz <= r2) {
            result.push_back(n);
        }
    }
    return result;
}

uint32_t GatheringNodeDisplay::GetNodeCount() const {
    return static_cast<uint32_t>(nodes_.size());
}

void GatheringNodeDisplay::SetTracked(ObjectGuid guid, bool tracked) {
    for (auto& n : nodes_) {
        if (n.guid.GetRawValue() == guid.GetRawValue()) {
            n.isTracked = tracked;
            return;
        }
    }
}

std::string GatheringNodeDisplay::GetTypeName(GatherNodeType type) {
    switch (type) {
        case GatherNodeType::Herb:       return "Herb";
        case GatherNodeType::Ore:        return "Ore";
        case GatherNodeType::SkinTarget: return "Skin Target";
        case GatherNodeType::Gas:        return "Gas";
        case GatherNodeType::Treasure:   return "Treasure";
    }
    return "Unknown";
}

uint32_t GatheringNodeDisplay::GetTypeColor(GatherNodeType type) {

    switch (type) {
        case GatherNodeType::Herb:       return 0xFF00FF00;
        case GatherNodeType::Ore:        return 0xFFC0C0C0;
        case GatherNodeType::SkinTarget: return 0xFF8B4513;
        case GatherNodeType::Gas:        return 0xFF9370DB;
        case GatherNodeType::Treasure:   return 0xFFFFD700;
    }
    return 0xFFFFFFFF;
}

void GatheringNodeDisplay::Clear() {
    nodes_.clear();
}

}
