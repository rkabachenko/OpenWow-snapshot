
#include "openwow/data/map_dbc_store.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace openwow::data {

static std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool MapDBCStore::AddEntry(MapDBCEntry entry) {
    uint32_t id = entry.id;
    auto [_, ok] = entries_.emplace(id, std::move(entry));
    return ok;
}

std::optional<MapDBCEntry> MapDBCStore::GetEntry(uint32_t mapId) const {
    auto it = entries_.find(mapId);
    if (it == entries_.end()) return std::nullopt;
    return it->second;
}

std::string MapDBCStore::GetMapName(uint32_t mapId) const {
    auto it = entries_.find(mapId);
    return it != entries_.end() ? it->second.name : "";
}

std::optional<MapDBCType> MapDBCStore::GetMapType(uint32_t mapId) const {
    auto it = entries_.find(mapId);
    if (it == entries_.end()) return std::nullopt;
    return it->second.type;
}

std::string MapDBCStore::GetMapTypeName(MapDBCType type) {
    switch (type) {
        case MapDBCType::Normal:      return "Normal";
        case MapDBCType::Instance:    return "Instance";
        case MapDBCType::Raid:        return "Raid";
        case MapDBCType::Battleground: return "Battleground";
        case MapDBCType::Arena:       return "Arena";
    }
    return "Unknown";
}

std::vector<uint32_t> MapDBCStore::GetMapsByType(MapDBCType type) const {
    std::vector<uint32_t> result;
    for (auto& [id, e] : entries_) {
        if (e.type == type) result.push_back(id);
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<uint32_t> MapDBCStore::GetMapsByExpansion(uint32_t expansion) const {
    std::vector<uint32_t> result;
    for (auto& [id, e] : entries_) {
        if (e.expansion == expansion) result.push_back(id);
    }
    std::sort(result.begin(), result.end());
    return result;
}

bool MapDBCStore::IsInstance(uint32_t mapId) const {
    auto it = entries_.find(mapId);
    return it != entries_.end() && it->second.type == MapDBCType::Instance;
}

bool MapDBCStore::IsRaid(uint32_t mapId) const {
    auto it = entries_.find(mapId);
    return it != entries_.end() && it->second.type == MapDBCType::Raid;
}

bool MapDBCStore::IsBattleground(uint32_t mapId) const {
    auto it = entries_.find(mapId);
    return it != entries_.end() && it->second.type == MapDBCType::Battleground;
}

size_t MapDBCStore::GetEntryCount() const { return entries_.size(); }

void MapDBCStore::RegisterDefaults() {

    {
        MapDBCEntry e{};
        e.id = 0; e.internalName = "Azeroth"; e.name = "Eastern Kingdoms";
        e.type = MapDBCType::Normal; e.expansion = 0; e.maxPlayers = 0;
        AddEntry(std::move(e));
    }
    {
        MapDBCEntry e{};
        e.id = 1; e.internalName = "Kalimdor"; e.name = "Kalimdor";
        e.type = MapDBCType::Normal; e.expansion = 0; e.maxPlayers = 0;
        AddEntry(std::move(e));
    }
    {
        MapDBCEntry e{};
        e.id = 530; e.internalName = "Expansion01"; e.name = "Outland";
        e.type = MapDBCType::Normal; e.expansion = 1; e.maxPlayers = 0;
        AddEntry(std::move(e));
    }
    {
        MapDBCEntry e{};
        e.id = 571; e.internalName = "Northrend"; e.name = "Northrend";
        e.type = MapDBCType::Normal; e.expansion = 2; e.maxPlayers = 0;
        AddEntry(std::move(e));
    }

    {
        MapDBCEntry e{};
        e.id = 33; e.internalName = "Shadowfang"; e.name = "Shadowfang Keep";
        e.type = MapDBCType::Instance; e.expansion = 0; e.maxPlayers = 5;
        e.minLevel = 18; e.maxLevel = 21;
        AddEntry(std::move(e));
    }
    {
        MapDBCEntry e{};
        e.id = 36; e.internalName = "DeadminesInstance"; e.name = "The Deadmines";
        e.type = MapDBCType::Instance; e.expansion = 0; e.maxPlayers = 5;
        e.minLevel = 15; e.maxLevel = 21;
        AddEntry(std::move(e));
    }
    {
        MapDBCEntry e{};
        e.id = 595; e.internalName = "StratholmeCOT"; e.name = "The Culling of Stratholme";
        e.type = MapDBCType::Instance; e.expansion = 2; e.maxPlayers = 5;
        e.minLevel = 78; e.maxLevel = 80;
        AddEntry(std::move(e));
    }

    {
        MapDBCEntry e{};
        e.id = 603; e.internalName = "Ulduar"; e.name = "Ulduar";
        e.type = MapDBCType::Raid; e.expansion = 2; e.maxPlayers = 25;
        e.minLevel = 80; e.maxLevel = 80;
        AddEntry(std::move(e));
    }
    {
        MapDBCEntry e{};
        e.id = 631; e.internalName = "IcecrownCitadel"; e.name = "Icecrown Citadel";
        e.type = MapDBCType::Raid; e.expansion = 2; e.maxPlayers = 25;
        e.minLevel = 80; e.maxLevel = 80;
        AddEntry(std::move(e));
    }

    {
        MapDBCEntry e{};
        e.id = 30; e.internalName = "PVPZone01"; e.name = "Alterac Valley";
        e.type = MapDBCType::Battleground; e.expansion = 0; e.maxPlayers = 40;
        AddEntry(std::move(e));
    }

    {
        MapDBCEntry e{};
        e.id = 559; e.internalName = "NagrandArena"; e.name = "Nagrand Arena";
        e.type = MapDBCType::Arena; e.expansion = 1; e.maxPlayers = 5;
        AddEntry(std::move(e));
    }
}

std::vector<uint32_t> MapDBCStore::SearchByName(const std::string& query) const {
    std::vector<uint32_t> results;
    const auto lq = ToLower(query);
    for (auto& [id, entry] : entries_) {
        if (ToLower(entry.name).find(lq) != std::string::npos) {
            results.push_back(id);
        }
    }
    std::sort(results.begin(), results.end());
    return results;
}

void MapDBCStore::Clear() { entries_.clear(); }

}
