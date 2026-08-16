
#include "openwow/game/pet_action_bar_display.h"

#include <algorithm>

namespace openwow::game {

void PetActionBarDisplay::SetSlots(
    const std::vector<PetActionSlotInfo>& slots) {
    slots_.assign(slots.begin(),
                  slots.begin() +
                      std::min<size_t>(slots.size(), kMaxSlots));
}

const std::vector<PetActionSlotInfo>& PetActionBarDisplay::GetSlots() const {
    return slots_;
}

uint8_t PetActionBarDisplay::GetSlotCount() const {
    return static_cast<uint8_t>(slots_.size());
}

std::optional<PetActionSlotInfo> PetActionBarDisplay::GetSlot(
    uint8_t index) const {
    if (index >= slots_.size()) return std::nullopt;
    return slots_[index];
}

void PetActionBarDisplay::SetSlotCooldown(uint8_t index, float seconds) {
    if (index < slots_.size()) {
        slots_[index].cooldownRemaining = seconds;
    }
}

void PetActionBarDisplay::ToggleAutocast(uint8_t index) {
    if (index < slots_.size() && slots_[index].isAutocastable) {
        slots_[index].isAutocasting = !slots_[index].isAutocasting;
    }
}

void PetActionBarDisplay::SetStance(PetStanceMode mode) {
    stance_ = mode;
    for (auto& s : slots_) {
        if (s.actionType == PetActionType::Stance) {
            s.isActive =
                (s.actionId == static_cast<uint32_t>(mode));
        }
    }
}

PetStanceMode PetActionBarDisplay::GetCurrentStance() const {
    return stance_;
}

void PetActionBarDisplay::ActivateSlot(uint8_t index) {
    if (index >= slots_.size()) return;
    PetActionType t = slots_[index].actionType;
    for (auto& s : slots_) {
        if (s.actionType == t) s.isActive = false;
    }
    slots_[index].isActive = true;
}

void PetActionBarDisplay::SetPetInfo(const std::string& name, uint8_t level,
                                      uint8_t happiness,
                                      uint8_t loyaltyLevel) {
    petName_      = name;
    petLevel_     = level;
    petHappiness_ = happiness;
    petLoyalty_   = loyaltyLevel;
    hasPet_       = true;
}

const std::string& PetActionBarDisplay::GetPetName() const {
    return petName_;
}

uint8_t PetActionBarDisplay::GetPetLevel() const { return petLevel_; }
uint8_t PetActionBarDisplay::GetPetHappiness() const { return petHappiness_; }

bool PetActionBarDisplay::HasPet() const { return hasPet_; }

void PetActionBarDisplay::DismissPet() {
    Clear();
}

void PetActionBarDisplay::SetHasPet(bool has) { hasPet_ = has; }

void PetActionBarDisplay::Clear() {
    slots_.clear();
    stance_       = PetStanceMode::Defensive;
    petName_.clear();
    petLevel_     = 0;
    petHappiness_ = 0;
    petLoyalty_   = 0;
    hasPet_       = false;
}

}
