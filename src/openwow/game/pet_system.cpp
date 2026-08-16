
#include "openwow/game/pet_system.h"

namespace openwow::game {

PetSystem& PetSystem::Get() {
    static PetSystem instance;
    return instance;
}

void PetSystem::SetCurrentPet(const PetInfo& pet) {
    std::lock_guard lock(mutex_);
    current_pet_ = pet;
}

void PetSystem::DismissPet() {
    std::lock_guard lock(mutex_);
    current_pet_.reset();
    action_bar_.clear();
}

bool PetSystem::HasPet() const {
    std::lock_guard lock(mutex_);
    return current_pet_.has_value();
}

const PetInfo* PetSystem::GetCurrentPet() const {
    std::lock_guard lock(mutex_);
    if (!current_pet_.has_value()) return nullptr;
    return &current_pet_.value();
}

size_t PetSystem::GetNumPetSpells() const {
    std::lock_guard lock(mutex_);
    if (!current_pet_.has_value()) return 0;
    return current_pet_->spells.size();
}

const PetSpellInfo* PetSystem::GetPetSpell(size_t index) const {
    std::lock_guard lock(mutex_);
    if (!current_pet_.has_value()) return nullptr;
    if (index >= current_pet_->spells.size()) return nullptr;
    return &current_pet_->spells[index];
}

void PetSystem::SetPetActionBar(const std::vector<PetAction>& actions) {
    std::lock_guard lock(mutex_);
    action_bar_ = actions;
}

size_t PetSystem::GetNumPetActions() const {
    std::lock_guard lock(mutex_);
    return action_bar_.size();
}

const PetAction* PetSystem::GetPetAction(size_t index) const {
    std::lock_guard lock(mutex_);
    if (index >= action_bar_.size()) return nullptr;
    return &action_bar_[index];
}

void PetSystem::SetStableSlots(const std::vector<StableSlot>& slots) {
    std::lock_guard lock(mutex_);
    stable_slots_ = slots;
}

size_t PetSystem::GetNumStableSlots() const {
    std::lock_guard lock(mutex_);
    return stable_slots_.size();
}

const StableSlot* PetSystem::GetStableSlot(size_t index) const {
    std::lock_guard lock(mutex_);
    if (index >= stable_slots_.size()) return nullptr;
    return &stable_slots_[index];
}

void PetSystem::SetPetTrainingPoints(uint32_t points) {
    std::lock_guard lock(mutex_);
    training_points_ = points;
}

uint32_t PetSystem::GetPetTrainingPoints() const {
    std::lock_guard lock(mutex_);
    return training_points_;
}

void PetSystem::Reset() {
    std::lock_guard lock(mutex_);
    current_pet_.reset();
    action_bar_.clear();
    stable_slots_.clear();
    training_points_ = 0;
}

}
