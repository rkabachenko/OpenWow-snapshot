
#include "openwow/game/hunter_pet.h"

#include <algorithm>

namespace openwow::game {

void HunterPetManager::SetCurrentPet(ObjectGuid guid, const std::string& name,
                                     PetFamily family, uint32_t level,
                                     uint32_t displayId) {
    current_pet_ = HunterCurrentPet{guid, name, family, level, displayId};
}

std::optional<HunterCurrentPet> HunterPetManager::GetCurrentPet() const {
    return current_pet_;
}

bool HunterPetManager::HasActivePet() const {
    return current_pet_.has_value();
}

void HunterPetManager::SetStableSlots(const std::vector<HunterStableSlot>& slots) {
    for (uint32_t i = 0; i < kMaxStableSlots; ++i) {
        if (i < slots.size()) {
            stable_slots_[i] = slots[i];
            stable_slots_[i].slotIndex = i;
        } else {
            stable_slots_[i] = HunterStableSlot{};
            stable_slots_[i].slotIndex = i;
        }
    }
}

std::vector<HunterStableSlot> HunterPetManager::GetStableSlots() const {
    return {std::begin(stable_slots_), std::end(stable_slots_)};
}

uint32_t HunterPetManager::GetOccupiedStableSlots() const {
    uint32_t count = 0;
    for (const auto& slot : stable_slots_) {
        if (!slot.isEmpty) ++count;
    }
    return count;
}

void HunterPetManager::SetStableSlot(uint32_t index, const HunterStableSlot& slot) {
    if (index >= kMaxStableSlots) return;
    stable_slots_[index] = slot;
    stable_slots_[index].slotIndex = index;
}

std::optional<HunterStableSlot> HunterPetManager::GetStableSlot(uint32_t index) const {
    if (index >= kMaxStableSlots) return std::nullopt;
    return stable_slots_[index];
}

std::string HunterPetManager::GetFamilyName(PetFamily family) {
    switch (family) {
        case PetFamily::Wolf:         return "Wolf";
        case PetFamily::Cat:          return "Cat";
        case PetFamily::Bear:         return "Bear";
        case PetFamily::Boar:         return "Boar";
        case PetFamily::Crocolisk:    return "Crocolisk";
        case PetFamily::Carrion_Bird: return "Carrion Bird";
        case PetFamily::Crab:         return "Crab";
        case PetFamily::Gorilla:      return "Gorilla";
        case PetFamily::Raptor:       return "Raptor";
        case PetFamily::Tallstrider:  return "Tallstrider";
        case PetFamily::Scorpid:      return "Scorpid";
        case PetFamily::Turtle:       return "Turtle";
        case PetFamily::Hyena:        return "Hyena";
        case PetFamily::Bird_of_Prey: return "Bird of Prey";
        case PetFamily::Wind_Serpent: return "Wind Serpent";
        case PetFamily::Nether_Ray:   return "Nether Ray";
        case PetFamily::Dragonhawk:   return "Dragonhawk";
        case PetFamily::Ravager:      return "Ravager";
        case PetFamily::Warp_Stalker: return "Warp Stalker";
        case PetFamily::Sporebat:     return "Sporebat";
        case PetFamily::Serpent:      return "Serpent";
        case PetFamily::Moth:         return "Moth";
        case PetFamily::Chimaera:     return "Chimaera";
        case PetFamily::Devilsaur:    return "Devilsaur";
        case PetFamily::Ghoul:        return "Ghoul";
        case PetFamily::Silithid:     return "Silithid";
        case PetFamily::Worm:         return "Worm";
        case PetFamily::Rhino:        return "Rhino";
        case PetFamily::Wasp:         return "Wasp";
        case PetFamily::Core_Hound:   return "Core Hound";
        case PetFamily::Spirit_Beast: return "Spirit Beast";
    }
    return "Unknown";
}

uint32_t HunterPetManager::GetFamilyAbility(PetFamily family) {

    switch (family) {
        case PetFamily::Wolf:         return 64495;
        case PetFamily::Cat:          return 49822;
        case PetFamily::Bear:         return 53476;
        case PetFamily::Boar:         return 53490;
        case PetFamily::Crocolisk:    return 53475;
        case PetFamily::Carrion_Bird: return 53478;
        case PetFamily::Crab:         return 53480;
        case PetFamily::Gorilla:      return 53481;
        case PetFamily::Raptor:       return 53479;
        case PetFamily::Tallstrider:  return 53485;
        case PetFamily::Scorpid:      return 55728;
        case PetFamily::Turtle:       return 53477;
        case PetFamily::Hyena:        return 53484;
        case PetFamily::Bird_of_Prey: return 53482;
        case PetFamily::Wind_Serpent: return 53497;
        case PetFamily::Nether_Ray:   return 53486;
        case PetFamily::Dragonhawk:   return 53488;
        case PetFamily::Ravager:      return 53493;
        case PetFamily::Warp_Stalker: return 53496;
        case PetFamily::Sporebat:     return 53492;
        case PetFamily::Serpent:      return 53489;
        case PetFamily::Moth:         return 53494;
        case PetFamily::Chimaera:     return 53495;
        case PetFamily::Devilsaur:    return 55024;
        case PetFamily::Ghoul:        return 53491;
        case PetFamily::Silithid:     return 55073;
        case PetFamily::Worm:         return 53487;
        case PetFamily::Rhino:        return 53498;
        case PetFamily::Wasp:         return 53499;
        case PetFamily::Core_Hound:   return 55078;
        case PetFamily::Spirit_Beast: return 53508;
    }
    return 0;
}

bool HunterPetManager::IsStableOpen() const { return stable_open_; }

void HunterPetManager::OpenStable(ObjectGuid stableMasterGuid) {
    stable_open_ = true;
    stable_master_ = stableMasterGuid;
}

void HunterPetManager::CloseStable() {
    stable_open_ = false;
    stable_master_ = ObjectGuid{};
}

uint32_t HunterPetManager::GetStableCost() const {

    return GetOccupiedStableSlots() * 100;
}

bool HunterPetManager::CanTame() const {
    return !current_pet_.has_value() && !is_taming_;
}

void HunterPetManager::SetTaming(bool taming) { is_taming_ = taming; }
bool HunterPetManager::IsTaming() const { return is_taming_; }

void HunterPetManager::Reset() {
    current_pet_.reset();
    for (uint32_t i = 0; i < kMaxStableSlots; ++i) {
        stable_slots_[i] = HunterStableSlot{};
        stable_slots_[i].slotIndex = i;
    }
    stable_open_ = false;
    stable_master_ = ObjectGuid{};
    is_taming_ = false;
}

}
