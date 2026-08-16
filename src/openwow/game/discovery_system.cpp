
#include "openwow/game/discovery_system.h"

#include <algorithm>

namespace openwow::game {

bool DiscoverySystem::TriggerDiscovery(std::uint32_t areaId,
                                       const std::string& name,
                                       std::uint32_t xpReward,
                                       DiscoveryType type) {
    if (discovered_.count(areaId)) return false;
    discovered_.insert(areaId);

    DiscoveryEvent ev;
    ev.areaId    = areaId;
    ev.name      = name;
    ev.xpReward  = xpReward;
    ev.type      = type;
    ev.timestamp = clock_;
    events_.push_back(ev);

    totalXp_ += xpReward;

    hasPending_   = true;
    pendingName_  = name;
    pendingXp_    = xpReward;
    pendingTimer_ = displayTime_;

    return true;
}

bool DiscoverySystem::IsDiscovered(std::uint32_t areaId) const {
    return discovered_.count(areaId) != 0;
}

std::vector<std::uint32_t> DiscoverySystem::GetDiscoveredAreas() const {
    std::vector<std::uint32_t> result(discovered_.begin(), discovered_.end());
    std::sort(result.begin(), result.end());
    return result;
}

std::uint32_t DiscoverySystem::GetDiscoveredCount() const {
    return static_cast<std::uint32_t>(discovered_.size());
}

std::optional<DiscoveryEvent> DiscoverySystem::GetLastDiscovery() const {
    if (events_.empty()) return std::nullopt;
    return events_.back();
}

std::uint32_t DiscoverySystem::GetTotalXPFromDiscovery() const {
    return totalXp_;
}

bool DiscoverySystem::HasPendingDiscovery() const { return hasPending_; }

std::string DiscoverySystem::GetPendingDiscoveryName() const {
    return hasPending_ ? pendingName_ : std::string{};
}

std::uint32_t DiscoverySystem::GetPendingDiscoveryXP() const {
    return hasPending_ ? pendingXp_ : 0;
}

void DiscoverySystem::AcknowledgeDiscovery() {
    hasPending_   = false;
    pendingName_.clear();
    pendingXp_    = 0;
    pendingTimer_ = 0.0f;
}

float DiscoverySystem::GetDiscoveryDisplayTime() const { return displayTime_; }

void DiscoverySystem::SetDiscoveryDisplayTime(float seconds) {
    displayTime_ = seconds;
}

void DiscoverySystem::Update(float dt) {
    clock_ += dt;
    if (hasPending_) {
        pendingTimer_ -= dt;
        if (pendingTimer_ <= 0.0f) {
            AcknowledgeDiscovery();
        }
    }
}

void DiscoverySystem::Reset() {
    discovered_.clear();
    events_.clear();
    totalXp_      = 0;
    hasPending_   = false;
    pendingName_.clear();
    pendingXp_    = 0;
    pendingTimer_ = 0.0f;
    displayTime_  = kDefaultDisplayTime;
    clock_        = 0.0f;
}

}
