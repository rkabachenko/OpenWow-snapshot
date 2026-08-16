
#include "openwow/data/area_table_store.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace openwow::data {

static std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool AreaTableStore::AddEntry(AreaTableEntry entry) {
    if (entry.id == 0) return false;
    auto [_, ok] = entries_.emplace(entry.id, std::move(entry));
    return ok;
}

std::optional<AreaTableEntry> AreaTableStore::GetEntry(uint32_t areaId) const {
    auto it = entries_.find(areaId);
    if (it == entries_.end()) return std::nullopt;
    return it->second;
}

std::string AreaTableStore::GetName(uint32_t areaId) const {
    auto it = entries_.find(areaId);
    return it != entries_.end() ? it->second.name : "";
}

std::optional<uint32_t> AreaTableStore::GetParent(uint32_t areaId) const {
    auto it = entries_.find(areaId);
    if (it == entries_.end()) return std::nullopt;
    return it->second.parentAreaId;
}

uint32_t AreaTableStore::GetZoneId(uint32_t areaId) const {
    uint32_t current = areaId;

    for (int depth = 0; depth < 32; ++depth) {
        auto it = entries_.find(current);
        if (it == entries_.end()) return current;
        if (it->second.parentAreaId == 0) return current;
        current = it->second.parentAreaId;
    }
    return current;
}

std::vector<uint32_t> AreaTableStore::GetSubAreas(uint32_t areaId) const {
    std::vector<uint32_t> result;
    for (auto& [id, e] : entries_) {
        if (e.parentAreaId == areaId) result.push_back(id);
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<uint32_t> AreaTableStore::GetAreasForMap(uint32_t mapId) const {
    std::vector<uint32_t> result;
    for (auto& [id, e] : entries_) {
        if (e.mapId == mapId) result.push_back(id);
    }
    std::sort(result.begin(), result.end());
    return result;
}

bool AreaTableStore::IsCity(uint32_t areaId) const {
    auto it = entries_.find(areaId);
    return it != entries_.end() && (it->second.flags & AreaTableFlags::City);
}

bool AreaTableStore::IsSanctuary(uint32_t areaId) const {
    auto it = entries_.find(areaId);
    return it != entries_.end() && (it->second.flags & AreaTableFlags::Sanctuary);
}

bool AreaTableStore::IsPvP(uint32_t areaId) const {
    auto it = entries_.find(areaId);
    return it != entries_.end() && (it->second.flags & AreaTableFlags::PvP);
}

size_t AreaTableStore::GetEntryCount() const { return entries_.size(); }

void AreaTableStore::RegisterDefaults() {

    auto addZone = [&](uint32_t id, uint32_t mapId, const char* name, uint32_t flags = 0, uint32_t level = 0) {
        AreaTableEntry e{};
        e.id = id; e.mapId = mapId; e.parentAreaId = 0; e.name = name;
        e.flags = flags; e.areaLevel = level;
        AddEntry(std::move(e));
    };

    addZone(12,   0, "Elwynn Forest",  0, 5);
    addZone(40,   0, "Westfall",       0, 10);
    addZone(1519, 0, "Stormwind City", AreaTableFlags::City | AreaTableFlags::Capital, 0);
    addZone(1537, 0, "Ironforge",      AreaTableFlags::City | AreaTableFlags::Capital, 0);

    addZone(14,   1, "Durotar",        0, 5);
    addZone(1637, 1, "Orgrimmar",      AreaTableFlags::City | AreaTableFlags::Capital, 0);
    addZone(1657, 1, "Darnassus",      AreaTableFlags::City | AreaTableFlags::Capital, 0);

    addZone(65,   571, "Dragonblight",    0, 72);
    addZone(66,   571, "Zul'Drak",        0, 74);
    addZone(67,   571, "Storm Peaks",     0, 77);
    addZone(210,  571, "Icecrown",        0, 77);
    addZone(394,  571, "Grizzly Hills",   0, 73);
    addZone(495,  571, "Howling Fjord",   0, 68);
    addZone(3537, 571, "Borean Tundra",   0, 68);
    addZone(4395, 571, "Dalaran",         AreaTableFlags::City | AreaTableFlags::Sanctuary, 0);

    {
        AreaTableEntry e{};
        e.id = 5148; e.mapId = 571; e.parentAreaId = 4395;
        e.name = "Dalaran - The Violet Citadel";
        e.flags = AreaTableFlags::Sanctuary;
        AddEntry(std::move(e));
    }
    {
        AreaTableEntry e{};
        e.id = 5149; e.mapId = 571; e.parentAreaId = 4395;
        e.name = "Dalaran - Sunreaver's Sanctuary";
        e.flags = AreaTableFlags::Sanctuary;
        AddEntry(std::move(e));
    }

    addZone(3518, 0, "Nagrand", AreaTableFlags::PvP, 65);
}

std::vector<uint32_t> AreaTableStore::SearchByName(const std::string& query) const {
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

void AreaTableStore::Clear() { entries_.clear(); }

}
