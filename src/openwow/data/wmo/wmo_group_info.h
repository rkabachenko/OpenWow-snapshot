#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::data {

enum WMOGroupFlags : uint32_t {
    WMOGroupFlag_HasBSP     = 0x1,
    WMOGroupFlag_HasLight   = 0x2,
    WMOGroupFlag_HasDoodads = 0x4,
    WMOGroupFlag_IsOutdoor  = 0x8,
    WMOGroupFlag_HasWater   = 0x10,
    WMOGroupFlag_IsExterior = 0x20,
    WMOGroupFlag_HasPortals = 0x40,
};

struct WMOGroupEntry {
    uint32_t    groupIndex  = 0;
    std::string name;
    uint32_t    flags       = 0;
    float       bbMinX      = 0.0f;
    float       bbMinY      = 0.0f;
    float       bbMinZ      = 0.0f;
    float       bbMaxX      = 0.0f;
    float       bbMaxY      = 0.0f;
    float       bbMaxZ      = 0.0f;
    uint32_t    portalStart = 0;
    uint32_t    portalCount = 0;
    uint32_t    batchCount  = 0;
};

struct WMOPortalEntry {
    uint32_t portalIndex = 0;
    uint32_t groupFrom   = 0;
    uint32_t groupTo     = 0;
    float    normalX     = 0.0f;
    float    normalY     = 0.0f;
    float    normalZ     = 0.0f;
    float    distance    = 0.0f;
};

class WMOGroupInfoStore {
public:
    void AddGroup(const WMOGroupEntry& group);
    std::optional<WMOGroupEntry> GetGroup(uint32_t groupIndex) const;
    std::vector<WMOGroupEntry> GetAllGroups() const;
    uint32_t GetGroupCount() const;

    bool IsOutdoor(uint32_t groupIndex) const;
    bool HasWater(uint32_t groupIndex) const;
    bool HasFlag(uint32_t groupIndex, WMOGroupFlags flag) const;

    void AddPortal(const WMOPortalEntry& portal);
    std::vector<WMOPortalEntry> GetPortals() const;
    std::vector<WMOPortalEntry> GetPortalsForGroup(uint32_t groupIndex) const;
    std::vector<uint32_t> GetAdjacentGroups(uint32_t groupIndex) const;
    uint32_t GetPortalCount() const;

    void Clear();

private:
    std::unordered_map<uint32_t, WMOGroupEntry> groups_;
    std::vector<WMOPortalEntry> portals_;
};

}
