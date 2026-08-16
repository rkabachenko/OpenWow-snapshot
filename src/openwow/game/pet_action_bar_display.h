
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

enum class PetActionType : uint8_t {
    Spell   = 0,
    Stance  = 1,
    Command = 2,
    Toggle  = 3,
};

enum class PetStanceMode : uint8_t {
    Passive    = 0,
    Defensive  = 1,
    Aggressive = 2,
};

enum class PetCommandType : uint8_t {
    Attack  = 0,
    Follow  = 1,
    Stay    = 2,
    Dismiss = 3,
};

struct PetActionSlotInfo {
    uint8_t       slotIndex        = 0;
    PetActionType actionType       = PetActionType::Spell;
    uint32_t      actionId         = 0;
    std::string   name;
    uint32_t      iconId           = 0;
    bool          isActive         = false;
    bool          isAutocastable   = false;
    bool          isAutocasting    = false;
    float         cooldownRemaining = 0.0f;
    bool          isUsable         = true;
};

class PetActionBarDisplay {
 public:
    static constexpr uint8_t kMaxSlots = 10;

    void SetSlots(const std::vector<PetActionSlotInfo>& slots);

    [[nodiscard]] const std::vector<PetActionSlotInfo>& GetSlots() const;

    [[nodiscard]] uint8_t GetSlotCount() const;

    [[nodiscard]] std::optional<PetActionSlotInfo> GetSlot(uint8_t index) const;

    void SetSlotCooldown(uint8_t index, float seconds);

    void ToggleAutocast(uint8_t index);

    void SetStance(PetStanceMode mode);

    [[nodiscard]] PetStanceMode GetCurrentStance() const;

    void ActivateSlot(uint8_t index);

    void SetPetInfo(const std::string& name, uint8_t level,
                    uint8_t happiness, uint8_t loyaltyLevel);

    [[nodiscard]] const std::string& GetPetName() const;
    [[nodiscard]] uint8_t            GetPetLevel() const;
    [[nodiscard]] uint8_t            GetPetHappiness() const;

    [[nodiscard]] bool HasPet() const;

    void DismissPet();

    void SetHasPet(bool has);

    void Clear();

 private:
    std::vector<PetActionSlotInfo> slots_;
    PetStanceMode                  stance_    = PetStanceMode::Defensive;
    std::string                    petName_;
    uint8_t                        petLevel_      = 0;
    uint8_t                        petHappiness_  = 0;
    uint8_t                        petLoyalty_    = 0;
    bool                           hasPet_        = false;
};

}
