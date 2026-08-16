
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

enum class SpellFlyoutDirection : uint8_t {
    Up    = 0,
    Down  = 1,
    Left  = 2,
    Right = 3,
};

struct SpellFlyoutSlot {
    uint32_t    spellId  = 0;
    uint32_t    iconId   = 0;
    std::string name;
    bool        isKnown  = false;
    bool        isUsable = false;
};

struct SpellFlyoutPanelInfo {
    uint32_t                     flyoutId  = 0;
    std::string                  name;
    uint32_t                     iconId    = 0;
    std::vector<SpellFlyoutSlot> slots;
    SpellFlyoutDirection         direction = SpellFlyoutDirection::Up;
};

class SpellFlyoutPanel {
 public:
    static constexpr uint8_t kMaxSlots = 16;

    SpellFlyoutPanel() = default;

    void OpenFlyout(const SpellFlyoutPanelInfo& info);
    void CloseFlyout();
    [[nodiscard]] bool IsOpen() const;

    [[nodiscard]] uint32_t                            GetFlyoutId() const;
    [[nodiscard]] const std::vector<SpellFlyoutSlot>& GetSlots() const;
    [[nodiscard]] uint8_t                             GetSlotCount() const;
    [[nodiscard]] SpellFlyoutDirection                GetDirection() const;

    void SelectSlot(uint8_t index);
    [[nodiscard]] std::optional<uint32_t> GetSelectedSpellId() const;

    void SetSlotUsable(uint8_t index, bool usable);

    [[nodiscard]] bool HasKnownSpells() const;

    [[nodiscard]] const std::string& GetName() const;
    [[nodiscard]] uint32_t GetIconId() const;
    [[nodiscard]] std::vector<SpellFlyoutSlot> FilterKnownSlots() const;
    [[nodiscard]] std::vector<SpellFlyoutSlot> FilterUsableSlots() const;
    void CycleForward();
    void CycleBackward();
    [[nodiscard]] std::optional<uint8_t> GetSelectedIndex() const;
    void ResetSelection();
    [[nodiscard]] std::optional<SpellFlyoutSlot> GetSlotBySpellId(uint32_t spellId) const;
    void SetDirection(SpellFlyoutDirection dir);
    void MarkSlotKnown(uint8_t index, bool known);
    [[nodiscard]] uint8_t GetUsableSlotCount() const;
    [[nodiscard]] uint8_t GetKnownSlotCount() const;

 private:
    bool                         open_      = false;
    uint32_t                     flyoutId_  = 0;
    std::string                  name_;
    uint32_t                     iconId_    = 0;
    std::vector<SpellFlyoutSlot> slots_;
    SpellFlyoutDirection         direction_ = SpellFlyoutDirection::Up;
    std::optional<uint32_t>      selectedSpellId_;
    std::optional<uint8_t>       selectedIndex_;
};

}
