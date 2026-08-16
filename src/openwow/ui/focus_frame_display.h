#pragma once

#include "openwow/game/object_guid.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::ui {

struct FocusFrameDisplayData {
    openwow::game::ObjectGuid guid;
    std::string name;
    std::uint8_t level    = 0;
    std::uint8_t classId  = 0;

    std::int32_t health    = 0;
    std::int32_t healthMax = 0;
    std::int32_t power     = 0;
    std::int32_t powerMax  = 0;
    std::uint8_t powerType = 0;

    bool isAlive           = true;
    std::uint8_t reaction  = 2;

    std::vector<std::uint32_t> auraIds;

    std::string castingSpellName;
    float castProgress     = 0.0f;
    bool isCasting         = false;
};

class FocusFrameDisplay {
public:

    void SetFocusData(const FocusFrameDisplayData& data);
    void ClearFocus();
    [[nodiscard]] std::optional<FocusFrameDisplayData> GetFocusData() const;
    [[nodiscard]] bool HasFocus() const;
    [[nodiscard]] openwow::game::ObjectGuid GetFocusGuid() const;

    void UpdateHealth(std::int32_t current, std::int32_t max);
    void UpdatePower(std::int32_t current, std::int32_t max,
                     std::uint8_t type);

    void UpdateCast(const std::string& spellName, float progress);
    void ClearCast();

    void AddAura(std::uint32_t auraId);
    void RemoveAura(std::uint32_t auraId);
    [[nodiscard]] std::size_t GetAuraCount() const;

    [[nodiscard]] float GetHealthPercent() const;
    [[nodiscard]] float GetPowerPercent() const;

    void Reset();

private:
    std::optional<FocusFrameDisplayData> data_;
};

}
