#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace openwow::game {

enum class UnitFramePowerType : std::uint8_t {
    Mana       = 0,
    Rage       = 1,
    Focus      = 2,
    Energy     = 3,
    Happiness  = 4,
    Runic      = 5,
    RunicPower = 6,
};

struct UnitFramePowerColor {
    float r{0.0f};
    float g{0.0f};
    float b{0.0f};
};

struct UnitFrameAuraIcon {
    std::uint32_t spellId{0};
    std::uint32_t iconId{0};
    std::uint8_t  stacks{0};
    bool          isDebuff{false};
    float         timeRemaining{0.0f};
};

class PlayerUnitFrameData {
public:
    PlayerUnitFrameData() = default;

    void SetName(const std::string& name);
    [[nodiscard]] const std::string& GetName() const;

    void SetLevel(std::uint8_t level);
    [[nodiscard]] std::uint8_t GetLevel() const;

    void SetClass(std::uint8_t classId);
    [[nodiscard]] std::uint8_t GetClass() const;

    void SetHealth(std::uint32_t current, std::uint32_t max);
    [[nodiscard]] float GetHealthPercent() const;

    void SetPower(UnitFramePowerType type, std::uint32_t current, std::uint32_t max);
    [[nodiscard]] float GetPowerPercent() const;
    [[nodiscard]] UnitFramePowerType GetPowerType() const;
    [[nodiscard]] UnitFramePowerColor GetPowerColor() const;

    void SetPortraitDisplayId(std::uint32_t displayId);
    [[nodiscard]] std::uint32_t GetPortraitDisplayId() const;

    void SetCombatState(bool combat);
    [[nodiscard]] bool IsCombatState() const;

    void SetResting(bool resting);
    [[nodiscard]] bool IsResting() const;

    void SetAFK(bool afk);
    [[nodiscard]] bool IsAFK() const;

    void SetDND(bool dnd);
    [[nodiscard]] bool IsDND() const;

    void SetGroupRole(const std::string& role);
    [[nodiscard]] const std::string& GetGroupRole() const;

    void SetAuras(const std::vector<UnitFrameAuraIcon>& auras);
    [[nodiscard]] const std::vector<UnitFrameAuraIcon>& GetAuras() const;
    [[nodiscard]] std::uint32_t GetBuffCount() const;
    [[nodiscard]] std::uint32_t GetDebuffCount() const;
    [[nodiscard]] bool HasAura(std::uint32_t spellId) const;

    [[nodiscard]] static UnitFramePowerColor GetDefaultPowerColor(UnitFramePowerType type);

private:
    std::string name_;
    std::uint8_t level_{1};
    std::uint8_t classId_{0};

    std::uint32_t healthCurrent_{0};
    std::uint32_t healthMax_{0};

    UnitFramePowerType powerType_{UnitFramePowerType::Mana};
    std::uint32_t powerCurrent_{0};
    std::uint32_t powerMax_{0};

    std::uint32_t portraitDisplayId_{0};

    bool inCombat_{false};
    bool resting_{false};
    bool afk_{false};
    bool dnd_{false};

    std::string groupRole_{"NONE"};

    std::vector<UnitFrameAuraIcon> auras_;
};

}
