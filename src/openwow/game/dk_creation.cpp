
#include "openwow/game/dk_creation.h"

namespace openwow::game {

DKCreationManager& DKCreationManager::Get() {
    static DKCreationManager instance;
    return instance;
}

void DKCreationManager::SetDKAvailable(bool available) {
    std::lock_guard lock(mutex_);
    available_ = available;
}

bool DKCreationManager::IsDKAvailable() const {
    std::lock_guard lock(mutex_);
    return available_;
}

void DKCreationManager::SetRequiredLevel(uint32_t level) {
    std::lock_guard lock(mutex_);
    required_level_ = level;
}

uint32_t DKCreationManager::GetRequiredLevel() const {
    std::lock_guard lock(mutex_);
    return required_level_;
}

void DKCreationManager::SetHasLevelRequirement(bool meets) {
    std::lock_guard lock(mutex_);
    meets_requirement_ = meets;
}

bool DKCreationManager::MeetsRequirement() const {
    std::lock_guard lock(mutex_);
    return meets_requirement_;
}

std::vector<uint32_t> DKCreationManager::GetAllowedRaces() const {
    std::lock_guard lock(mutex_);
    return allowed_races_;
}

void DKCreationManager::AddAllowedRace(uint32_t raceId) {
    std::lock_guard lock(mutex_);
    if (std::find(allowed_races_.begin(), allowed_races_.end(), raceId) ==
        allowed_races_.end()) {
        allowed_races_.push_back(raceId);
    }
}

bool DKCreationManager::IsRaceAllowed(uint32_t raceId) const {
    std::lock_guard lock(mutex_);
    return std::find(allowed_races_.begin(), allowed_races_.end(), raceId) !=
           allowed_races_.end();
}

uint32_t DKCreationManager::GetStartZone() const {
    return 609;
}

uint32_t DKCreationManager::GetStartLevel() const {
    return 55;
}

uint32_t DKCreationManager::GetMaxDKPerRealm() const {
    return 1;
}

void DKCreationManager::SetExistingDKCount(uint32_t count) {
    std::lock_guard lock(mutex_);
    existing_dk_count_ = count;
}

uint32_t DKCreationManager::GetExistingDKCount() const {
    std::lock_guard lock(mutex_);
    return existing_dk_count_;
}

bool DKCreationManager::CanCreateDK() const {
    std::lock_guard lock(mutex_);
    return available_ && meets_requirement_ && existing_dk_count_ < 1;
}

uint32_t DKCreationManager::GetDKClassId() const {
    return 6;
}

void DKCreationManager::SetDefaultSkinOptions(uint32_t raceId,
                                              const DKSkinOptions& opts) {
    std::lock_guard lock(mutex_);
    for (auto& [id, o] : skin_options_) {
        if (id == raceId) {
            o = opts;
            return;
        }
    }
    skin_options_.emplace_back(raceId, opts);
}

DKSkinOptions DKCreationManager::GetDefaultSkinOptions(uint32_t raceId) const {
    std::lock_guard lock(mutex_);
    for (const auto& [id, o] : skin_options_) {
        if (id == raceId) return o;
    }
    return {};
}

void DKCreationManager::Reset() {
    std::lock_guard lock(mutex_);
    available_ = false;
    required_level_ = 55;
    meets_requirement_ = false;
    allowed_races_.clear();
    existing_dk_count_ = 0;
    skin_options_.clear();
}

}
