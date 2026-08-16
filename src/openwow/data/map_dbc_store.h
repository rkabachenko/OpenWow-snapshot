#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::data {

enum class MapDBCType : uint32_t {
    Normal      = 0,
    Instance    = 1,
    Raid        = 2,
    Battleground = 3,
    Arena       = 4,
};

struct MapDBCEntry {
    uint32_t   id              = 0;
    std::string internalName;
    MapDBCType type            = MapDBCType::Normal;
    uint32_t   flags           = 0;
    uint32_t   expansion       = 0;
    uint32_t   areaTableId     = 0;
    std::string name;
    uint32_t   loadingScreenId = 0;
    uint32_t   minLevel        = 0;
    uint32_t   maxLevel        = 0;
    uint32_t   maxPlayers      = 0;
    uint32_t   corpseMapId     = 0;
    float      corpseX         = 0.0f;
    float      corpseY         = 0.0f;
    uint32_t   timeOfDayOverride = 0;
    uint32_t   parentMapId     = 0;
    float      cosmeticX       = 0.0f;
    float      cosmeticY       = 0.0f;
};

class MapDBCStore {
public:
    MapDBCStore() = default;

    bool AddEntry(MapDBCEntry entry);

    [[nodiscard]] std::optional<MapDBCEntry> GetEntry(uint32_t mapId) const;
    [[nodiscard]] std::string                GetMapName(uint32_t mapId) const;
    [[nodiscard]] std::optional<MapDBCType>  GetMapType(uint32_t mapId) const;

    [[nodiscard]] static std::string GetMapTypeName(MapDBCType type);

    [[nodiscard]] std::vector<uint32_t> GetMapsByType(MapDBCType type) const;
    [[nodiscard]] std::vector<uint32_t> GetMapsByExpansion(uint32_t expansion) const;

    [[nodiscard]] bool IsInstance(uint32_t mapId) const;
    [[nodiscard]] bool IsRaid(uint32_t mapId) const;
    [[nodiscard]] bool IsBattleground(uint32_t mapId) const;

    [[nodiscard]] size_t GetEntryCount() const;

    void RegisterDefaults();

    [[nodiscard]] std::vector<uint32_t> SearchByName(const std::string& query) const;

    void Clear();

private:
    std::unordered_map<uint32_t, MapDBCEntry> entries_;
};

}
