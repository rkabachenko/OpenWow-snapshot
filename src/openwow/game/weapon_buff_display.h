#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

enum class WeaponBuffSlotType : uint8_t {
    MainHandBuff = 0,
    OffHandBuff  = 1,
    RangedBuff   = 2,
};

struct WeaponBuffDisplayInfo {
    WeaponBuffSlotType slot          = WeaponBuffSlotType::MainHandBuff;
    uint32_t           enchantId     = 0;
    std::string        name;
    uint32_t           iconId        = 0;
    float              remainingDuration = 0.0f;
    float              totalDuration     = 0.0f;
    uint32_t           charges       = 0;
    std::string        tooltip;
};

class WeaponBuffDisplay {
public:
    void SetBuff(WeaponBuffDisplayInfo info);
    void RemoveBuff(WeaponBuffSlotType slot);
    void ClearAll();

    [[nodiscard]] std::optional<WeaponBuffDisplayInfo> GetBuff(WeaponBuffSlotType slot) const;
    [[nodiscard]] bool     HasBuff(WeaponBuffSlotType slot) const;
    [[nodiscard]] bool     HasAnyBuff() const;
    [[nodiscard]] uint32_t GetBuffCount() const;
    [[nodiscard]] std::vector<WeaponBuffDisplayInfo> GetActiveBuffs() const;

    void Update(float deltaTime);
    [[nodiscard]] float GetDurationPercent(WeaponBuffSlotType slot) const;

    [[nodiscard]] bool IsExpiring(WeaponBuffSlotType slot) const;

    [[nodiscard]] static std::string GetSlotName(WeaponBuffSlotType slot);

private:
    std::optional<WeaponBuffDisplayInfo> mainHand_;
    std::optional<WeaponBuffDisplayInfo> offHand_;
    std::optional<WeaponBuffDisplayInfo> ranged_;

    [[nodiscard]] std::optional<WeaponBuffDisplayInfo> const& SlotRef(WeaponBuffSlotType s) const;
    std::optional<WeaponBuffDisplayInfo>& SlotRef(WeaponBuffSlotType s);
};

}
