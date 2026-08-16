
#include "openwow/game/raid_marker.h"

#include <algorithm>

namespace openwow::game {

static constexpr const char* kIconNames[kRaidTargetIconTypeCount] = {
    "Star", "Circle", "Diamond", "Triangle", "Moon", "Square", "Cross", "Skull",
};

void RaidMarkerSystem::SetMarker(RaidTargetIconType icon, ObjectGuid unitGuid) {
    const auto idx = static_cast<std::size_t>(icon);
    if (idx >= kRaidTargetIconTypeCount) return;

    for (std::size_t i = 0; i < kRaidTargetIconTypeCount; ++i) {
        if (i != idx && slots_[i] == unitGuid) {
            slots_[i] = ObjectGuid{};
        }
    }

    slots_[idx] = unitGuid;
}

void RaidMarkerSystem::ClearMarker(RaidTargetIconType icon) {
    const auto idx = static_cast<std::size_t>(icon);
    if (idx < kRaidTargetIconTypeCount) slots_[idx] = ObjectGuid{};
}

void RaidMarkerSystem::ClearAllMarkers() {
    slots_.fill(ObjectGuid{});
}

ObjectGuid RaidMarkerSystem::GetMarker(RaidTargetIconType icon) const {
    const auto idx = static_cast<std::size_t>(icon);
    if (idx >= kRaidTargetIconTypeCount) return ObjectGuid{};
    return slots_[idx];
}

std::optional<RaidTargetIconType> RaidMarkerSystem::GetUnitMarker(ObjectGuid unitGuid) const {
    for (std::size_t i = 0; i < kRaidTargetIconTypeCount; ++i) {
        if (!slots_[i].IsEmpty() && slots_[i] == unitGuid)
            return static_cast<RaidTargetIconType>(static_cast<std::uint8_t>(i));
    }
    return std::nullopt;
}

bool RaidMarkerSystem::HasMarker(ObjectGuid unitGuid) const {
    return GetUnitMarker(unitGuid).has_value();
}

std::vector<std::pair<RaidTargetIconType, ObjectGuid>> RaidMarkerSystem::GetAllMarkers() const {
    std::vector<std::pair<RaidTargetIconType, ObjectGuid>> result;
    result.reserve(kRaidTargetIconTypeCount);
    for (std::size_t i = 0; i < kRaidTargetIconTypeCount; ++i) {
        if (!slots_[i].IsEmpty())
            result.emplace_back(static_cast<RaidTargetIconType>(static_cast<std::uint8_t>(i)), slots_[i]);
    }
    return result;
}

std::uint32_t RaidMarkerSystem::GetActiveMarkerCount() const {
    std::uint32_t n = 0;
    for (const auto& g : slots_) {
        if (!g.IsEmpty()) ++n;
    }
    return n;
}

std::string RaidMarkerSystem::GetIconName(RaidTargetIconType icon) {
    const auto idx = static_cast<std::size_t>(icon);
    if (idx >= kRaidTargetIconTypeCount) return "Unknown";
    return kIconNames[idx];
}

std::uint32_t RaidMarkerSystem::GetIconIndex(RaidTargetIconType icon) {
    return static_cast<std::uint32_t>(icon);
}

bool RaidMarkerSystem::IsAvailable(RaidTargetIconType icon) const {
    const auto idx = static_cast<std::size_t>(icon);
    if (idx >= kRaidTargetIconTypeCount) return false;
    return slots_[idx].IsEmpty();
}

std::vector<RaidTargetIconType> RaidMarkerSystem::GetAvailableIcons() const {
    std::vector<RaidTargetIconType> result;
    result.reserve(kRaidTargetIconTypeCount);
    for (std::size_t i = 0; i < kRaidTargetIconTypeCount; ++i) {
        if (slots_[i].IsEmpty())
            result.push_back(static_cast<RaidTargetIconType>(static_cast<std::uint8_t>(i)));
    }
    return result;
}

}
