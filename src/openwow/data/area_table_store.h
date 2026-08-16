#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::data {

namespace AreaTableFlags {
    inline constexpr uint32_t Capital    = 0x1;
    inline constexpr uint32_t Slave      = 0x2;
    inline constexpr uint32_t City       = 0x4;
    inline constexpr uint32_t Sanctuary  = 0x8;
    inline constexpr uint32_t PvP        = 0x10;
    inline constexpr uint32_t Arena      = 0x20;
    inline constexpr uint32_t Wintergrasp = 0x40;
}

struct AreaTableEntry {
    uint32_t   id                       = 0;
    uint32_t   mapId                    = 0;
    uint32_t   parentAreaId             = 0;
    uint32_t   areaBit                  = 0;
    uint32_t   flags                    = 0;
    uint32_t   soundProviderPrefId      = 0;
    uint32_t   soundProviderPrefUwId    = 0;
    uint32_t   soundAmbienceId          = 0;
    uint32_t   zoneMusicId              = 0;
    uint32_t   zoneIntroMusicId         = 0;
    uint32_t   explorationLevel         = 0;
    std::string name;
    uint32_t   factionGroupMask         = 0;
    uint32_t   ambientMultiplier        = 0;
    uint32_t   liquidType               = 0;
    uint32_t   minElevation             = 0;
    uint32_t   areaLevel                = 0;
};

class AreaTableStore {
public:
    AreaTableStore() = default;

    bool AddEntry(AreaTableEntry entry);

    [[nodiscard]] std::optional<AreaTableEntry> GetEntry(uint32_t areaId) const;
    [[nodiscard]] std::string                   GetName(uint32_t areaId) const;
    [[nodiscard]] std::optional<uint32_t>       GetParent(uint32_t areaId) const;

    [[nodiscard]] uint32_t GetZoneId(uint32_t areaId) const;

    [[nodiscard]] std::vector<uint32_t> GetSubAreas(uint32_t areaId) const;

    [[nodiscard]] std::vector<uint32_t> GetAreasForMap(uint32_t mapId) const;

    [[nodiscard]] bool IsCity(uint32_t areaId) const;
    [[nodiscard]] bool IsSanctuary(uint32_t areaId) const;
    [[nodiscard]] bool IsPvP(uint32_t areaId) const;

    [[nodiscard]] size_t GetEntryCount() const;

    void RegisterDefaults();

    [[nodiscard]] std::vector<uint32_t> SearchByName(const std::string& query) const;

    void Clear();

private:
    std::unordered_map<uint32_t, AreaTableEntry> entries_;
};

}
