
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "openwow/game/object_guid.h"

namespace openwow::game {

enum class GatherNodeType : uint32_t {
    Herb       = 0,
    Ore        = 1,
    SkinTarget = 2,
    Gas        = 3,
    Treasure   = 4,
};

struct GatherNodeEntry {
    ObjectGuid    guid;
    GatherNodeType nodeType      = GatherNodeType::Herb;
    std::string   name;
    float         x              = 0.0f;
    float         y              = 0.0f;
    float         z              = 0.0f;
    uint32_t      requiredSkill  = 0;
    bool          isTracked      = false;
    bool          isReachable    = false;
};

class GatheringNodeDisplay {
public:
    void AddNode(const GatherNodeEntry& entry);
    void RemoveNode(ObjectGuid guid);

    [[nodiscard]] std::optional<GatherNodeEntry> GetNode(ObjectGuid guid) const;
    [[nodiscard]] std::vector<GatherNodeEntry> GetNodesByType(GatherNodeType type) const;
    [[nodiscard]] std::vector<GatherNodeEntry> GetTrackedNodes() const;
    [[nodiscard]] std::vector<GatherNodeEntry> GetReachableNodes() const;

    [[nodiscard]] std::optional<GatherNodeEntry> GetNearestNode(
        GatherNodeType type, float playerX, float playerY, float playerZ) const;

    [[nodiscard]] std::vector<GatherNodeEntry> GetNodesInRange(
        float x, float y, float z, float range) const;

    [[nodiscard]] uint32_t GetNodeCount() const;

    void SetTracked(ObjectGuid guid, bool tracked);

    [[nodiscard]] static std::string  GetTypeName(GatherNodeType type);
    [[nodiscard]] static uint32_t     GetTypeColor(GatherNodeType type);

    void Clear();

private:
    std::vector<GatherNodeEntry> nodes_;
};

}
