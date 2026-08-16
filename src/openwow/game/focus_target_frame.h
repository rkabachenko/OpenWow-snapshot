#pragma once

#include "openwow/game/player_unit_frame_data.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

struct FocusFrameData {
    std::uint64_t guid{0};
    std::string   name;
    std::uint8_t  level{1};
    std::uint8_t  classId{0};
    std::uint32_t healthCurrent{0};
    std::uint32_t healthMax{0};
    std::uint32_t powerCurrent{0};
    std::uint32_t powerMax{0};
    std::uint8_t  powerType{0};
    bool          isPlayer{false};
    std::uint8_t  reaction{0};
};

struct FocusTargetCastInfo {
    std::uint32_t spellId{0};
    std::string   name;
    float         timeRemaining{0.0f};
    float         totalTime{0.0f};
    bool          isChanneling{false};
    bool          isInterruptible{true};
};

class FocusTargetFrame {
public:
    FocusTargetFrame() = default;

    void SetFocus(const FocusFrameData& data);
    void ClearFocus();
    [[nodiscard]] bool HasFocus() const;
    [[nodiscard]] std::optional<FocusFrameData> GetFocus() const;
    [[nodiscard]] std::uint64_t GetFocusGuid() const;

    void UpdateHealth(std::uint32_t current, std::uint32_t max);
    void UpdatePower(std::uint32_t current, std::uint32_t max);
    [[nodiscard]] float GetHealthPercent() const;
    [[nodiscard]] float GetPowerPercent() const;

    void SetCastInfo(const FocusTargetCastInfo& info);
    void ClearCast();
    [[nodiscard]] std::optional<FocusTargetCastInfo> GetCastInfo() const;
    [[nodiscard]] bool IsCasting() const;

    void SetAuras(const std::vector<UnitFrameAuraIcon>& auras);
    [[nodiscard]] const std::vector<UnitFrameAuraIcon>& GetAuras() const;
    [[nodiscard]] std::vector<UnitFrameAuraIcon> GetDebuffs() const;

    [[nodiscard]] bool IsHostile() const;
    [[nodiscard]] bool IsFriendly() const;

private:
    bool                              hasFocus_{false};
    FocusFrameData                    focus_;
    std::optional<FocusTargetCastInfo> castInfo_;
    std::vector<UnitFrameAuraIcon>    auras_;
};

}
