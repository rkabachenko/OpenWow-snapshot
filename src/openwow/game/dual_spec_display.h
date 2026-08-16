
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

struct SpecDisplaySlot {
    uint8_t     specIndex = 0;
    std::string name;
    uint32_t    iconId = 0;
    std::string tabName;
    uint32_t    pointsSpent = 0;
    bool        isActive = false;
};

class DualSpecDisplay {
public:
    void SetSpec(uint8_t index, SpecDisplaySlot spec);
    [[nodiscard]] std::optional<SpecDisplaySlot> GetSpec(uint8_t index) const;
    [[nodiscard]] std::vector<SpecDisplaySlot> GetSpecs() const;

    [[nodiscard]] uint8_t GetActiveSpec() const;
    void SetActiveSpec(uint8_t index);

    [[nodiscard]] bool IsUnlocked() const;
    void SetUnlocked(bool unlocked);

    [[nodiscard]] static uint32_t GetUnlockCost();

    [[nodiscard]] static uint8_t GetMaxSpecs();

    [[nodiscard]] std::string GetSpecName(uint8_t index) const;

    [[nodiscard]] bool CanSwitch() const;
    void SetInCombat(bool inCombat);
    void SetDead(bool dead);

    void Reset();

private:
    SpecDisplaySlot specs_[2] = {};
    bool has_spec_[2] = {false, false};
    uint8_t active_spec_ = 0;
    bool unlocked_ = false;
    bool in_combat_ = false;
    bool dead_ = false;
};

}
