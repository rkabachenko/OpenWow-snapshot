#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::game {

struct ExplorationArea {
    std::uint32_t areaId     = 0;
    std::uint32_t exploreFlag = 0;
    bool          isExplored = false;
    std::uint32_t mapId      = 0;
    std::string   name;
};

class MapExplorationManager {
 public:
    MapExplorationManager() = default;

    static constexpr std::uint32_t kMaxExplorationFields = 128;

    void SetExplorationBits(std::uint32_t index, std::uint32_t bits);

    [[nodiscard]] std::uint32_t GetExplorationBits(std::uint32_t index) const;

    [[nodiscard]] bool IsAreaExplored(std::uint32_t areaFlagIndex) const;

    void MarkExplored(std::uint32_t areaFlagIndex);

    [[nodiscard]] std::uint32_t GetExploredCount() const;

    [[nodiscard]] std::uint32_t GetTotalAreaCount() const;
    void SetTotalAreas(std::uint32_t count);

    [[nodiscard]] float GetExplorationPercent() const;

    void RegisterArea(std::uint32_t areaId, std::uint32_t exploreFlag,
                      std::uint32_t mapId, const std::string& name);

    [[nodiscard]] std::string GetAreaName(std::uint32_t areaId) const;

    [[nodiscard]] std::vector<std::uint32_t> GetExploredAreasForMap(
        std::uint32_t mapId) const;

    void Reset();

 private:
    std::uint32_t fields_[kMaxExplorationFields]{};
    std::uint32_t totalAreas_ = 0;

    std::unordered_map<std::uint32_t, ExplorationArea> areas_;
};

}
