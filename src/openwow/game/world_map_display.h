
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

enum class WorldMapOverlayType : std::uint8_t {
    None = 0,
    QuestObjective,
    FlightPath,
    Dungeon,
    Raid,
    Battleground,
    Zone,
    Subzone,
};

struct WorldMapMarker {
    float normalizedX{0.0f};
    float normalizedY{0.0f};
    std::string label;
    WorldMapOverlayType type{WorldMapOverlayType::None};
    std::uint32_t id{0};
};

class WorldMapDisplay {
 public:
    WorldMapDisplay() = default;

    void SetContinent(std::uint32_t continentId, const std::string& name);
    [[nodiscard]] std::uint32_t GetContinent() const;
    [[nodiscard]] std::string GetContinentName() const;

    void SetZone(std::uint32_t zoneId, const std::string& zoneName);
    [[nodiscard]] std::uint32_t GetZone() const;
    [[nodiscard]] std::string GetZoneName() const;

    void AddMarker(const WorldMapMarker& marker);
    void ClearMarkers();
    [[nodiscard]] std::vector<WorldMapMarker> GetMarkers() const;
    [[nodiscard]] std::vector<WorldMapMarker> GetMarkersByType(
        WorldMapOverlayType type) const;
    [[nodiscard]] std::size_t GetMarkerCount() const;

    void SetPlayerMapPosition(float normX, float normY);

    struct MapPosition {
        float x{0.0f};
        float y{0.0f};
    };
    [[nodiscard]] MapPosition GetPlayerMapPosition() const;

    void SetShowQuestObjectives(bool show);
    [[nodiscard]] bool GetShowQuestObjectives() const;

    void SetShowFlightPaths(bool show);
    [[nodiscard]] bool GetShowFlightPaths() const;

    void SetShowDungeons(bool show);
    [[nodiscard]] bool GetShowDungeons() const;

    [[nodiscard]] bool IsWorldMapOpen() const;
    void ToggleWorldMap();

    void Reset();

 private:
    std::uint32_t continentId_{0};
    std::string   continentName_;
    std::uint32_t zoneId_{0};
    std::string   zoneName_;

    std::vector<WorldMapMarker> markers_;

    float playerNormX_{0.0f};
    float playerNormY_{0.0f};

    bool showQuestObjectives_{true};
    bool showFlightPaths_{true};
    bool showDungeons_{true};
    bool mapOpen_{false};
};

}
