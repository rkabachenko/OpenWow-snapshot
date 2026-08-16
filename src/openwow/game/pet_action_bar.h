#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace openwow::game {

enum class PetActionType : uint8_t {
    Spell  = 0,
    Toggle = 1,
    Mode   = 2,
};

enum class PetMode : uint8_t {
    Assist    = 0,
    Defensive = 1,
    Passive   = 2,
};

enum class PetActionEnum : uint8_t {
    Attack = 0,
    Follow = 1,
    Stay   = 2,
};

struct PetActionSlot {
    uint32_t       slotIndex        = 0;
    PetActionType  actionType       = PetActionType::Spell;
    uint32_t       actionId         = 0;
    bool           isAutocast       = false;
    bool           isAutoCastEnabled = false;
    bool           isUsable         = true;
    float          cooldown         = 0.0f;
};

class PetActionBar {
 public:
    static constexpr uint32_t kSlotCount = 10;

    static constexpr uint32_t kSpellIdMask    = 0x3FFFFFFF;
    static constexpr uint32_t kAutocastBit    = 0x40000000;
    static constexpr uint32_t kAutoAllowedBit = 0x80000000;
    static constexpr uint32_t kTypeMask       = 0x3F000000;
    static constexpr int      kTypeShift      = 24;

    PetActionBar() { InitSlotIndices(); }

    void SetSlots(const std::vector<PetActionSlot>& slots);
    [[nodiscard]] std::vector<PetActionSlot> GetSlots() const;
    [[nodiscard]] std::optional<PetActionSlot> GetSlot(uint32_t index) const;
    [[nodiscard]] uint32_t GetSlotCount() const { return kSlotCount; }

    void SetAutocast(uint32_t slotIndex, bool enabled);
    [[nodiscard]] bool IsAutocast(uint32_t slotIndex) const;

    void ToggleAutocastBySpellId(uint32_t spellId, int32_t state = -1);

    void SetUsable(uint32_t slotIndex, bool usable);

    void SetCooldown(uint32_t slotIndex, float remaining);
    [[nodiscard]] float GetCooldown(uint32_t slotIndex) const;
    [[nodiscard]] bool IsOnCooldown(uint32_t slotIndex) const;

    int32_t SwapSlot(uint32_t srcIndex, uint32_t dstIndex);

    void SetMode(PetMode mode);
    [[nodiscard]] PetMode GetMode() const;

    void SetAction(PetActionEnum action);
    [[nodiscard]] PetActionEnum GetAction() const;

    [[nodiscard]] bool IsVisible() const;
    void SetVisible(bool visible);

    void Update(float dt);

    void Reset();

 private:
    void InitSlotIndices() {
        for (uint32_t i = 0; i < kSlotCount; ++i) slots_[i].slotIndex = i;
    }

    PetActionSlot  slots_[kSlotCount]{};
    PetMode        mode_    = PetMode::Defensive;
    PetActionEnum  action_  = PetActionEnum::Follow;
    bool           visible_ = false;
};

}
