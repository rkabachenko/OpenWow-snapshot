
#include "openwow/game/pet_state.h"

namespace openwow::game {

void PetStateManager::SetPet(const PetState& state) {
    pet_ = state;
}

std::optional<PetState> PetStateManager::GetPet() const {
    return pet_;
}

bool PetStateManager::HasPet() const {
    return pet_.has_value();
}

void PetStateManager::DismissPet() {
    pet_.reset();
}

ObjectGuid PetStateManager::GetPetGuid() const {
    if (!pet_) return ObjectGuid{};
    return pet_->guid;
}

std::string PetStateManager::GetPetName() const {
    if (!pet_) return {};
    return pet_->name;
}

void PetStateManager::SetHealth(uint32_t current, uint32_t max) {
    if (!pet_) return;
    pet_->health = current;
    pet_->maxHealth = (max > 0) ? max : 1;
}

float PetStateManager::GetHealthPercent() const {
    if (!pet_ || pet_->maxHealth == 0) return 0.0f;
    return static_cast<float>(pet_->health) / static_cast<float>(pet_->maxHealth) * 100.0f;
}

void PetStateManager::SetMana(uint32_t current, uint32_t max) {
    if (!pet_) return;
    pet_->mana = current;
    pet_->maxMana = (max > 0) ? max : 1;
}

float PetStateManager::GetManaPercent() const {
    if (!pet_ || pet_->maxMana == 0) return 0.0f;
    return static_cast<float>(pet_->mana) / static_cast<float>(pet_->maxMana) * 100.0f;
}

void PetStateManager::SetHappiness(PetHappiness h) {
    if (!pet_) return;
    pet_->happiness = h;
}

PetHappiness PetStateManager::GetHappiness() const {
    if (!pet_) return PetHappiness::Unhappy;
    return pet_->happiness;
}

std::string PetStateManager::GetHappinessName() const {
    if (!pet_) return "None";
    switch (pet_->happiness) {
        case PetHappiness::Unhappy: return "Unhappy";
        case PetHappiness::Content: return "Content";
        case PetHappiness::Happy:   return "Happy";
    }
    return "Unknown";
}

float PetStateManager::GetHappinessEffect() const {
    if (!pet_) return 1.0f;
    switch (pet_->happiness) {
        case PetHappiness::Unhappy: return 0.75f;
        case PetHappiness::Content: return 1.0f;
        case PetHappiness::Happy:   return 1.25f;
    }
    return 1.0f;
}

void PetStateManager::SetExperience(uint32_t current, uint32_t next) {
    if (!pet_) return;
    pet_->experience = current;
    pet_->nextLevelXP = (next > 0) ? next : 1;
}

float PetStateManager::GetXPPercent() const {
    if (!pet_ || pet_->nextLevelXP == 0) return 0.0f;
    return static_cast<float>(pet_->experience) / static_cast<float>(pet_->nextLevelXP) * 100.0f;
}

bool PetStateManager::IsPetAlive() const {
    if (!pet_) return false;
    return pet_->isAlive;
}

void PetStateManager::SetPetAlive(bool alive) {
    if (!pet_) return;
    pet_->isAlive = alive;
}

void PetStateManager::SetPetLevel(uint32_t level) {
    if (!pet_) return;
    pet_->level = level;
}

uint32_t PetStateManager::GetPetLevel() const {
    if (!pet_) return 0;
    return pet_->level;
}

uint32_t PetStateManager::GetLoyaltyLevel() const {
    if (!pet_) return 0;
    return pet_->loyaltyLevel;
}

void PetStateManager::SetLoyaltyLevel(uint32_t level) {
    if (!pet_) return;
    pet_->loyaltyLevel = level;
}

std::string PetStateManager::GetLoyaltyName(uint32_t level) {
    switch (level) {
        case 1: return "Rebellious";
        case 2: return "Unruly";
        case 3: return "Submissive";
        case 4: return "Dependable";
        case 5: return "Faithful";
        case 6: return "Best Friend";
        default: return "Unknown";
    }
}

void PetStateManager::Reset() {
    pet_.reset();
}

}
