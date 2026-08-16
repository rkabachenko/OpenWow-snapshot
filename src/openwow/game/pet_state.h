#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "openwow/game/object_guid.h"

namespace openwow::game {

enum class PetHappiness : uint8_t {
    Unhappy = 0,
    Content = 1,
    Happy   = 2,
};

struct PetState {
    ObjectGuid  guid;
    std::string name;
    uint32_t    level        = 1;
    uint32_t    classId      = 0;
    uint32_t    health       = 0;
    uint32_t    maxHealth    = 1;
    uint32_t    mana         = 0;
    uint32_t    maxMana      = 1;
    PetHappiness happiness   = PetHappiness::Happy;
    uint32_t    experience   = 0;
    uint32_t    nextLevelXP  = 1;
    uint32_t    loyaltyLevel = 1;
    bool        isAlive      = true;
    bool        isSummoned   = false;
};

class PetStateManager {
 public:
    void SetPet(const PetState& state);
    [[nodiscard]] std::optional<PetState> GetPet() const;
    [[nodiscard]] bool HasPet() const;
    void DismissPet();

    [[nodiscard]] ObjectGuid GetPetGuid() const;
    [[nodiscard]] std::string GetPetName() const;

    void SetHealth(uint32_t current, uint32_t max);
    [[nodiscard]] float GetHealthPercent() const;

    void SetMana(uint32_t current, uint32_t max);
    [[nodiscard]] float GetManaPercent() const;

    void SetHappiness(PetHappiness h);
    [[nodiscard]] PetHappiness GetHappiness() const;
    [[nodiscard]] std::string GetHappinessName() const;
    [[nodiscard]] float GetHappinessEffect() const;

    void SetExperience(uint32_t current, uint32_t next);
    [[nodiscard]] float GetXPPercent() const;

    [[nodiscard]] bool IsPetAlive() const;
    void SetPetAlive(bool alive);

    void SetPetLevel(uint32_t level);
    [[nodiscard]] uint32_t GetPetLevel() const;

    [[nodiscard]] uint32_t GetLoyaltyLevel() const;
    void SetLoyaltyLevel(uint32_t level);
    [[nodiscard]] static std::string GetLoyaltyName(uint32_t level);

    void Reset();

 private:
    std::optional<PetState> pet_;
};

}
