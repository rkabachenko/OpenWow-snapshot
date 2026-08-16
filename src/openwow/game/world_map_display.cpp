
#include "openwow/game/world_map_display.h"

#include <algorithm>

namespace openwow::game {

void WorldMapDisplay::SetContinent(std::uint32_t continentId,
                                   const std::string& name) {
    continentId_ = continentId;
    continentName_ = name;
}

std::uint32_t WorldMapDisplay::GetContinent() const {
    return continentId_;
}

std::string WorldMapDisplay::GetContinentName() const {
    return continentName_;
}

void WorldMapDisplay::SetZone(std::uint32_t zoneId,
                              const std::string& zoneName) {
    zoneId_ = zoneId;
    zoneName_ = zoneName;
}

std::uint32_t WorldMapDisplay::GetZone() const {
    return zoneId_;
}

std::string WorldMapDisplay::GetZoneName() const {
    return zoneName_;
}

void WorldMapDisplay::AddMarker(const WorldMapMarker& marker) {
    markers_.push_back(marker);
}

void WorldMapDisplay::ClearMarkers() {
    markers_.clear();
}

std::vector<WorldMapMarker> WorldMapDisplay::GetMarkers() const {
    return markers_;
}

std::vector<WorldMapMarker> WorldMapDisplay::GetMarkersByType(
    WorldMapOverlayType type) const {
    std::vector<WorldMapMarker> result;
    for (const auto& m : markers_) {
        if (m.type == type) {
            result.push_back(m);
        }
    }
    return result;
}

std::size_t WorldMapDisplay::GetMarkerCount() const {
    return markers_.size();
}

void WorldMapDisplay::SetPlayerMapPosition(float normX, float normY) {
    playerNormX_ = std::clamp(normX, 0.0f, 1.0f);
    playerNormY_ = std::clamp(normY, 0.0f, 1.0f);
}

WorldMapDisplay::MapPosition WorldMapDisplay::GetPlayerMapPosition() const {
    return {playerNormX_, playerNormY_};
}

void WorldMapDisplay::SetShowQuestObjectives(bool show) {
    showQuestObjectives_ = show;
}

bool WorldMapDisplay::GetShowQuestObjectives() const {
    return showQuestObjectives_;
}

void WorldMapDisplay::SetShowFlightPaths(bool show) {
    showFlightPaths_ = show;
}

bool WorldMapDisplay::GetShowFlightPaths() const {
    return showFlightPaths_;
}

void WorldMapDisplay::SetShowDungeons(bool show) {
    showDungeons_ = show;
}

bool WorldMapDisplay::GetShowDungeons() const {
    return showDungeons_;
}

bool WorldMapDisplay::IsWorldMapOpen() const {
    return mapOpen_;
}

void WorldMapDisplay::ToggleWorldMap() {
    mapOpen_ = !mapOpen_;
}

void WorldMapDisplay::Reset() {
    continentId_ = 0;
    continentName_.clear();
    zoneId_ = 0;
    zoneName_.clear();
    markers_.clear();
    playerNormX_ = 0.0f;
    playerNormY_ = 0.0f;
    showQuestObjectives_ = true;
    showFlightPaths_ = true;
    showDungeons_ = true;
    mapOpen_ = false;
}

}
