#include "openwow/data/wmo/wmo_group_info.h"

#include <algorithm>
#include <set>

namespace openwow::data {

void WMOGroupInfoStore::AddGroup(const WMOGroupEntry& group) {
    groups_[group.groupIndex] = group;
}

std::optional<WMOGroupEntry> WMOGroupInfoStore::GetGroup(uint32_t groupIndex) const {
    auto it = groups_.find(groupIndex);
    if (it == groups_.end()) return std::nullopt;
    return it->second;
}

std::vector<WMOGroupEntry> WMOGroupInfoStore::GetAllGroups() const {
    std::vector<WMOGroupEntry> out;
    out.reserve(groups_.size());
    for (auto& [k, v] : groups_) out.push_back(v);
    std::sort(out.begin(), out.end(),
              [](const WMOGroupEntry& a, const WMOGroupEntry& b) {
                  return a.groupIndex < b.groupIndex;
              });
    return out;
}

uint32_t WMOGroupInfoStore::GetGroupCount() const {
    return static_cast<uint32_t>(groups_.size());
}

bool WMOGroupInfoStore::IsOutdoor(uint32_t groupIndex) const {
    return HasFlag(groupIndex, WMOGroupFlag_IsOutdoor);
}

bool WMOGroupInfoStore::HasWater(uint32_t groupIndex) const {
    return HasFlag(groupIndex, WMOGroupFlag_HasWater);
}

bool WMOGroupInfoStore::HasFlag(uint32_t groupIndex, WMOGroupFlags flag) const {
    auto it = groups_.find(groupIndex);
    if (it == groups_.end()) return false;
    return (it->second.flags & static_cast<uint32_t>(flag)) != 0;
}

void WMOGroupInfoStore::AddPortal(const WMOPortalEntry& portal) {
    portals_.push_back(portal);
}

std::vector<WMOPortalEntry> WMOGroupInfoStore::GetPortals() const {
    return portals_;
}

std::vector<WMOPortalEntry> WMOGroupInfoStore::GetPortalsForGroup(uint32_t groupIndex) const {
    std::vector<WMOPortalEntry> out;
    for (auto& p : portals_) {
        if (p.groupFrom == groupIndex || p.groupTo == groupIndex) {
            out.push_back(p);
        }
    }
    return out;
}

std::vector<uint32_t> WMOGroupInfoStore::GetAdjacentGroups(uint32_t groupIndex) const {
    std::set<uint32_t> adj;
    for (auto& p : portals_) {
        if (p.groupFrom == groupIndex) adj.insert(p.groupTo);
        if (p.groupTo == groupIndex) adj.insert(p.groupFrom);
    }
    return {adj.begin(), adj.end()};
}

uint32_t WMOGroupInfoStore::GetPortalCount() const {
    return static_cast<uint32_t>(portals_.size());
}

void WMOGroupInfoStore::Clear() {
    groups_.clear();
    portals_.clear();
}

}
