
#include "openwow/game/map_objective.h"

#include <algorithm>

namespace openwow::game {

MapObjectiveManager& MapObjectiveManager::Get() {
    static MapObjectiveManager instance;
    return instance;
}

void MapObjectiveManager::AddMarker(const MapObjectiveMarker& marker) {
    std::lock_guard lock(mutex_);
    markers_.push_back(marker);
}

std::vector<MapObjectiveMarker> MapObjectiveManager::GetMarkersForMap(
    uint32_t mapId) const {
    std::lock_guard lock(mutex_);
    std::vector<MapObjectiveMarker> result;
    for (auto& m : markers_) {
        if (m.mapId == mapId) result.push_back(m);
    }
    return result;
}

std::vector<MapObjectiveMarker> MapObjectiveManager::GetMarkersByType(
    MapObjectiveType type) const {
    std::lock_guard lock(mutex_);
    std::vector<MapObjectiveMarker> result;
    for (auto& m : markers_) {
        if (m.objectiveType == type) result.push_back(m);
    }
    return result;
}

std::vector<MapObjectiveMarker> MapObjectiveManager::GetTrackedMarkers() const {
    std::lock_guard lock(mutex_);
    std::vector<MapObjectiveMarker> result;
    for (auto& m : markers_) {
        if (m.isTracked) result.push_back(m);
    }
    return result;
}

void MapObjectiveManager::SetTracked(const std::string& name, bool tracked) {
    std::lock_guard lock(mutex_);
    for (auto& m : markers_) {
        if (m.name == name) m.isTracked = tracked;
    }
}

uint32_t MapObjectiveManager::GetMarkerCount() const {
    std::lock_guard lock(mutex_);
    return static_cast<uint32_t>(markers_.size());
}

std::vector<MapObjectiveMarker> MapObjectiveManager::GetVisibleMarkers(
    uint32_t mapId, float viewMinX, float viewMinY,
    float viewMaxX, float viewMaxY) const {
    std::lock_guard lock(mutex_);
    std::vector<MapObjectiveMarker> result;
    for (auto& m : markers_) {
        if (m.mapId == mapId && m.x >= viewMinX && m.x <= viewMaxX &&
            m.y >= viewMinY && m.y <= viewMaxY) {
            result.push_back(m);
        }
    }
    return result;
}

std::string MapObjectiveManager::GetTypeName(MapObjectiveType type) {
    switch (type) {
        case MapObjectiveType::Dungeon:      return "Dungeon";
        case MapObjectiveType::Raid:         return "Raid";
        case MapObjectiveType::Battleground: return "Battleground";
        case MapObjectiveType::Arena:        return "Arena";
        case MapObjectiveType::FlightPath:   return "FlightPath";
        case MapObjectiveType::QuestGiver:   return "QuestGiver";
        case MapObjectiveType::QuestTurnin:  return "QuestTurnin";
        case MapObjectiveType::QuestObjective: return "QuestObjective";
        case MapObjectiveType::Vendor:       return "Vendor";
        case MapObjectiveType::Repair:       return "Repair";
        case MapObjectiveType::Inn:          return "Inn";
        case MapObjectiveType::Mailbox:      return "Mailbox";
        case MapObjectiveType::AuctionHouse: return "AuctionHouse";
        case MapObjectiveType::Bank:         return "Bank";
        case MapObjectiveType::Trainer:      return "Trainer";
    }
    return "Unknown";
}

uint32_t MapObjectiveManager::GetTypeIcon(MapObjectiveType type) {

    switch (type) {
        case MapObjectiveType::Dungeon:        return 0;
        case MapObjectiveType::Raid:           return 1;
        case MapObjectiveType::Battleground:   return 2;
        case MapObjectiveType::Arena:          return 3;
        case MapObjectiveType::FlightPath:     return 4;
        case MapObjectiveType::QuestGiver:     return 5;
        case MapObjectiveType::QuestTurnin:    return 6;
        case MapObjectiveType::QuestObjective: return 7;
        case MapObjectiveType::Vendor:         return 8;
        case MapObjectiveType::Repair:         return 9;
        case MapObjectiveType::Inn:            return 10;
        case MapObjectiveType::Mailbox:        return 11;
        case MapObjectiveType::AuctionHouse:   return 12;
        case MapObjectiveType::Bank:           return 13;
        case MapObjectiveType::Trainer:        return 14;
    }
    return 0;
}

void MapObjectiveManager::Clear() {
    std::lock_guard lock(mutex_);
    markers_.clear();
}

void MapObjectiveManager::ClearMap(uint32_t mapId) {
    std::lock_guard lock(mutex_);
    markers_.erase(
        std::remove_if(markers_.begin(), markers_.end(),
                       [mapId](auto& m) { return m.mapId == mapId; }),
        markers_.end());
}

}
