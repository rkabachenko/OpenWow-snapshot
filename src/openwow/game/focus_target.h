
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "openwow/game/object_guid.h"

namespace openwow::game {

class FocusTargetSystem {
public:

    void SetFocus(ObjectGuid guid);
    [[nodiscard]] ObjectGuid GetFocus() const;
    [[nodiscard]] bool HasFocus() const;
    void ClearFocus();

    void SetFocusName(const std::string& name);
    [[nodiscard]] const std::string& GetFocusName() const;

    void SetFocusHealth(std::uint32_t current, std::uint32_t max);
    [[nodiscard]] float GetFocusHealthPercent() const;

    void SetFocusMana(std::uint32_t current, std::uint32_t max);
    [[nodiscard]] float GetFocusManaPercent() const;

    void SetFocusLevel(std::uint32_t level);
    [[nodiscard]] std::uint32_t GetFocusLevel() const;

    void SetFocusCasting(std::uint32_t spellId, const std::string& name,
                         float progress);
    [[nodiscard]] std::uint32_t GetFocusCastSpellId() const;
    [[nodiscard]] bool IsFocusCasting() const;

    void SetFocusAuras(const std::vector<std::uint32_t>& spellIds);
    [[nodiscard]] const std::vector<std::uint32_t>& GetFocusAuras() const;
    [[nodiscard]] bool HasFocusAura(std::uint32_t spellId) const;

    [[nodiscard]] bool IsFocusFriendly() const;
    void SetFocusFriendly(bool friendly);

    void SetFocusReaction(std::uint32_t reaction);
    [[nodiscard]] std::uint32_t GetFocusReaction() const;

    void Reset();

private:
    ObjectGuid                   guid_;
    std::string                  name_;
    std::uint32_t                healthCurrent_ = 0;
    std::uint32_t                healthMax_     = 0;
    std::uint32_t                manaCurrent_   = 0;
    std::uint32_t                manaMax_       = 0;
    std::uint32_t                level_         = 0;

    std::uint32_t                castSpellId_   = 0;
    std::string                  castName_;
    float                        castProgress_  = 0.0f;

    std::vector<std::uint32_t>   auras_;

    std::uint32_t                reaction_      = 0;
};

}
