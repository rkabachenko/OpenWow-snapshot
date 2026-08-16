
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

enum class StanceFormType : uint8_t {
    None               = 0,
    Cat                = 1,
    Tree               = 2,
    Travel             = 3,
    Aquatic            = 4,
    Bear               = 5,
    Ambient            = 6,
    Ghoul              = 7,
    DireBear           = 8,
    StealthShadow      = 9,
    CreatureBear       = 10,
    CreatureCat        = 11,
    GhostWolf          = 12,
    BattleStance       = 13,
    DefensiveStance    = 14,
    BerserkerStance    = 15,
    Moonkin            = 16,
    SpiritOfRedemption = 17,
    Flight             = 18,
    Stealth            = 19,
    Shadow             = 20,
    FlightEpic         = 21,
    Metamorphosis      = 22,
    Undead             = 23,
};

struct StanceBarSlotInfo {
    uint8_t        slotIndex         = 0;
    StanceFormType formType          = StanceFormType::None;
    uint32_t       spellId           = 0;
    std::string    name;
    uint32_t       iconId            = 0;
    bool           isActive          = false;
    bool           isUsable          = true;
    float          cooldownRemaining = 0.0f;
};

class StanceBarDisplay {
 public:
    static constexpr uint8_t kMaxSlots = 7;

    void SetSlots(const std::vector<StanceBarSlotInfo>& slots);
    [[nodiscard]] uint8_t GetSlotCount() const;
    [[nodiscard]] std::optional<StanceBarSlotInfo> GetSlot(uint8_t index) const;

    [[nodiscard]] StanceFormType GetActiveForm() const;
    void ActivateSlot(uint8_t index);
    void Deactivate();
    [[nodiscard]] std::optional<uint8_t> GetActiveSlotIndex() const;

    void SetSlotCooldown(uint8_t index, float remainingSec);
    void SetSlotUsable(uint8_t index, bool usable);

    [[nodiscard]] bool IsVisible() const;

    void UpdateCooldowns(float dt);

    [[nodiscard]] static std::string GetFormLabel(StanceFormType form);

    [[nodiscard]] std::optional<uint8_t> FindSlotByFormType(StanceFormType form) const;

    [[nodiscard]] std::optional<uint8_t> FindSlotBySpellId(uint32_t spellId) const;

    [[nodiscard]] bool CanSwitchTo(uint8_t index) const;

    [[nodiscard]] bool HasActiveForm() const;
    void Clear();

 private:
    std::vector<StanceBarSlotInfo> slots_;
};

}
