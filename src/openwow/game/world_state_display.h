
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::game {

struct WorldStateDisplayEntry {
    uint32_t stateId = 0;
    int32_t value = 0;
    uint32_t zoneId = 0;
    bool isVisible = true;
    std::string formatString;
    std::string displayText;
};

class WorldStateDisplay {
public:
    static WorldStateDisplay& Get();

    void SetState(uint32_t stateId, int32_t value, uint32_t zoneId = 0);
    int32_t GetState(uint32_t stateId) const;
    bool HasState(uint32_t stateId) const;
    void RemoveState(uint32_t stateId);

    void SetFormat(uint32_t stateId, const std::string& format);
    std::string GetDisplayText(uint32_t stateId) const;

    std::vector<WorldStateDisplayEntry> GetStatesForZone(uint32_t zoneId) const;
    std::vector<WorldStateDisplayEntry> GetVisibleStates() const;

    void SetVisible(uint32_t stateId, bool visible);

    uint32_t GetActiveStateCount() const;

    void ClearZone(uint32_t zoneId);
    void ClearAll();

private:
    WorldStateDisplay() = default;

    std::string FormatEntry(const WorldStateDisplayEntry& entry) const;

    mutable std::mutex mutex_;
    std::unordered_map<uint32_t, WorldStateDisplayEntry> states_;
};

}
