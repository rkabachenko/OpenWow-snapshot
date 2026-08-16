
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::game {

enum class MapObjectiveType : uint8_t {
    Dungeon = 0,
    Raid,
    Battleground,
    Arena,
    FlightPath,
    QuestGiver,
    QuestTurnin,
    QuestObjective,
    Vendor,
    Repair,
    Inn,
    Mailbox,
    AuctionHouse,
    Bank,
    Trainer,
};

struct MapObjectiveMarker {
    MapObjectiveType objectiveType = MapObjectiveType::Dungeon;
    float x = 0.0f;
    float y = 0.0f;
    uint32_t mapId = 0;
    std::string name;
    std::string tooltip;
    uint32_t iconIndex = 0;
    bool isTracked = false;
};

class MapObjectiveManager {
public:
    static MapObjectiveManager& Get();

    void AddMarker(const MapObjectiveMarker& marker);

    std::vector<MapObjectiveMarker> GetMarkersForMap(uint32_t mapId) const;
    std::vector<MapObjectiveMarker> GetMarkersByType(MapObjectiveType type) const;
    std::vector<MapObjectiveMarker> GetTrackedMarkers() const;

    void SetTracked(const std::string& name, bool tracked);

    uint32_t GetMarkerCount() const;

    std::vector<MapObjectiveMarker> GetVisibleMarkers(
        uint32_t mapId, float viewMinX, float viewMinY,
        float viewMaxX, float viewMaxY) const;

    static std::string GetTypeName(MapObjectiveType type);
    static uint32_t GetTypeIcon(MapObjectiveType type);

    void Clear();
    void ClearMap(uint32_t mapId);

private:
    MapObjectiveManager() = default;

    mutable std::mutex mutex_;
    std::vector<MapObjectiveMarker> markers_;
};

}
