#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "openwow/game/object_guid.h"

namespace openwow::game {

enum class PetFamily : uint32_t {
    Wolf         = 1,
    Cat          = 2,
    Bear         = 4,
    Boar         = 5,
    Crocolisk    = 6,
    Carrion_Bird = 7,
    Crab         = 8,
    Gorilla      = 9,
    Raptor       = 11,
    Tallstrider  = 12,
    Scorpid      = 20,
    Turtle       = 21,
    Hyena        = 25,
    Bird_of_Prey = 26,
    Wind_Serpent = 27,
    Nether_Ray   = 34,
    Dragonhawk   = 30,
    Ravager      = 31,
    Warp_Stalker = 32,
    Sporebat     = 33,
    Serpent      = 35,
    Moth         = 37,
    Chimaera     = 38,
    Devilsaur    = 39,
    Ghoul        = 40,
    Silithid     = 41,
    Worm         = 42,
    Rhino        = 43,
    Wasp         = 44,
    Core_Hound   = 45,
    Spirit_Beast = 46,
};

struct HunterStableSlot {
    uint32_t    slotIndex = 0;
    std::string petName;
    uint32_t    petLevel  = 0;
    PetFamily   familyId  = PetFamily::Wolf;
    uint32_t    displayId = 0;
    bool        isEmpty   = true;
};

struct HunterCurrentPet {
    ObjectGuid  guid;
    std::string name;
    PetFamily   family   = PetFamily::Wolf;
    uint32_t    level    = 1;
    uint32_t    displayId = 0;
};

class HunterPetManager {
 public:
    static constexpr uint32_t kMaxStableSlots = 5;

    void SetCurrentPet(ObjectGuid guid, const std::string& name,
                       PetFamily family, uint32_t level, uint32_t displayId);
    [[nodiscard]] std::optional<HunterCurrentPet> GetCurrentPet() const;
    [[nodiscard]] bool HasActivePet() const;

    void SetStableSlots(const std::vector<HunterStableSlot>& slots);
    [[nodiscard]] std::vector<HunterStableSlot> GetStableSlots() const;
    [[nodiscard]] uint32_t GetStableSlotCount() const { return kMaxStableSlots; }
    [[nodiscard]] uint32_t GetOccupiedStableSlots() const;
    void SetStableSlot(uint32_t index, const HunterStableSlot& slot);
    [[nodiscard]] std::optional<HunterStableSlot> GetStableSlot(uint32_t index) const;

    [[nodiscard]] static std::string GetFamilyName(PetFamily family);
    [[nodiscard]] static uint32_t GetFamilyAbility(PetFamily family);

    [[nodiscard]] bool IsStableOpen() const;
    void OpenStable(ObjectGuid stableMasterGuid);
    void CloseStable();
    [[nodiscard]] uint32_t GetStableCost() const;

    [[nodiscard]] bool CanTame() const;
    void SetTaming(bool taming);
    [[nodiscard]] bool IsTaming() const;

    void Reset();

 private:
    std::optional<HunterCurrentPet> current_pet_;
    HunterStableSlot                stable_slots_[kMaxStableSlots]{};
    bool                            stable_open_   = false;
    ObjectGuid                      stable_master_;
    bool                            is_taming_     = false;
};

}
