
#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

struct PetSpellInfo {
    uint32_t spell_id    = 0;
    uint8_t  state       = 0;
    bool     is_auto_cast = false;
};

struct PetInfo {
    uint64_t    guid          = 0;
    std::string name;
    uint32_t    creature_family = 0;
    uint32_t    creature_id   = 0;
    uint32_t    level         = 0;
    uint32_t    xp            = 0;
    uint32_t    xp_max        = 0;
    int32_t     health        = 0;
    int32_t     health_max    = 0;
    int32_t     mana          = 0;
    int32_t     mana_max      = 0;
    uint8_t     happiness     = 0;
    float       loyalty       = 0.0f;
    bool        is_active     = false;
    std::vector<PetSpellInfo> spells;
};

struct PetAction {
    uint32_t id      = 0;
    uint8_t  type    = 0;
    bool     enabled = true;
};

struct StableSlot {
    uint32_t    pet_number      = 0;
    uint32_t    creature_id     = 0;
    std::string name;
    uint32_t    level           = 0;
    uint32_t    creature_family = 0;
    bool        is_active       = false;
};

class PetSystem {
 public:
    static PetSystem& Get();

    void SetCurrentPet(const PetInfo& pet);
    void DismissPet();
    [[nodiscard]] bool HasPet() const;
    [[nodiscard]] const PetInfo* GetCurrentPet() const;

    [[nodiscard]] size_t GetNumPetSpells() const;
    [[nodiscard]] const PetSpellInfo* GetPetSpell(size_t index) const;

    void SetPetActionBar(const std::vector<PetAction>& actions);
    [[nodiscard]] size_t GetNumPetActions() const;
    [[nodiscard]] const PetAction* GetPetAction(size_t index) const;

    void SetStableSlots(const std::vector<StableSlot>& slots);
    [[nodiscard]] size_t GetNumStableSlots() const;
    [[nodiscard]] const StableSlot* GetStableSlot(size_t index) const;

    void SetPetTrainingPoints(uint32_t points);
    [[nodiscard]] uint32_t GetPetTrainingPoints() const;

    void Reset();

 private:
    PetSystem() = default;

    std::optional<PetInfo> current_pet_;
    std::vector<PetAction> action_bar_;
    std::vector<StableSlot> stable_slots_;
    uint32_t training_points_ = 0;
    mutable std::mutex mutex_;
};

}
