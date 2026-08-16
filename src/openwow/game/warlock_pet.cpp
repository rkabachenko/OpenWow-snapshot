
#include "openwow/game/warlock_pet.h"

#include <algorithm>

namespace openwow::game {

void WarlockPetManager::SetActiveDemon(DemonType type, ObjectGuid guid,
                                       const std::string& name) {
    active_demon_ = ActiveDemon{type, guid, name};
}

std::optional<ActiveDemon> WarlockPetManager::GetActiveDemon() const {
    return active_demon_;
}

bool WarlockPetManager::HasDemon() const {
    return active_demon_.has_value();
}

void WarlockPetManager::DismissDemon() {
    active_demon_.reset();
}

DemonType WarlockPetManager::GetDemonType() const {
    if (!active_demon_) return DemonType::Imp;
    return active_demon_->type;
}

std::string WarlockPetManager::GetDemonName() const {
    if (!active_demon_) return {};
    return active_demon_->name;
}

uint32_t WarlockPetManager::GetDemonDisplayId(DemonType type) {

    switch (type) {
        case DemonType::Imp:        return 4449;
        case DemonType::Voidwalker: return 1132;
        case DemonType::Succubus:   return 4162;
        case DemonType::Felhunter:  return 850;
        case DemonType::Felguard:   return 17252;
        case DemonType::Infernal:   return 169;
        case DemonType::Doomguard:  return 11380;
    }
    return 0;
}

std::vector<DemonType> WarlockPetManager::GetAvailableDemons() const {
    return available_demons_;
}

void WarlockPetManager::AddAvailableDemon(DemonType type) {
    auto it = std::find(available_demons_.begin(), available_demons_.end(), type);
    if (it == available_demons_.end()) {
        available_demons_.push_back(type);
    }
}

void WarlockPetManager::RemoveAvailableDemon(DemonType type) {
    auto it = std::find(available_demons_.begin(), available_demons_.end(), type);
    if (it != available_demons_.end()) {
        available_demons_.erase(it);
    }
}

std::string WarlockPetManager::GetDemonTypeName(DemonType type) {
    switch (type) {
        case DemonType::Imp:        return "Imp";
        case DemonType::Voidwalker: return "Voidwalker";
        case DemonType::Succubus:   return "Succubus";
        case DemonType::Felhunter:  return "Felhunter";
        case DemonType::Felguard:   return "Felguard";
        case DemonType::Infernal:   return "Infernal";
        case DemonType::Doomguard:  return "Doomguard";
    }
    return "Unknown";
}

bool WarlockPetManager::IsSacrificed() const { return is_sacrificed_; }
void WarlockPetManager::SetSacrificed(bool sacrificed) { is_sacrificed_ = sacrificed; }

bool WarlockPetManager::HasSoulLink() const { return has_soul_link_; }
void WarlockPetManager::SetSoulLink(bool active) { has_soul_link_ = active; }

uint32_t WarlockPetManager::GetSoulShardCount() const { return soul_shard_count_; }
void WarlockPetManager::SetSoulShardCount(uint32_t count) { soul_shard_count_ = count; }

uint32_t WarlockPetManager::GetSummonCost(DemonType type) {
    switch (type) {
        case DemonType::Imp:
        case DemonType::Voidwalker:
        case DemonType::Succubus:
        case DemonType::Felhunter:
        case DemonType::Felguard:
            return 1;
        case DemonType::Infernal:
        case DemonType::Doomguard:
            return 1;
    }
    return 0;
}

void WarlockPetManager::Reset() {
    active_demon_.reset();
    available_demons_.clear();
    is_sacrificed_ = false;
    has_soul_link_ = false;
    soul_shard_count_ = 0;
}

}
