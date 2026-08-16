
#include "openwow/game/spell_flyout_panel.h"

#include <algorithm>

namespace openwow::game {

void SpellFlyoutPanel::OpenFlyout(const SpellFlyoutPanelInfo& info) {
    flyoutId_  = info.flyoutId;
    name_      = info.name;
    iconId_    = info.iconId;
    direction_ = info.direction;

    slots_.clear();
    const auto count = std::min(static_cast<size_t>(kMaxSlots), info.slots.size());
    slots_.assign(info.slots.begin(), info.slots.begin() + static_cast<ptrdiff_t>(count));

    selectedSpellId_.reset();
    open_ = true;
}

void SpellFlyoutPanel::CloseFlyout() {
    open_ = false;
}

bool SpellFlyoutPanel::IsOpen() const {
    return open_;
}

uint32_t SpellFlyoutPanel::GetFlyoutId() const {
    return flyoutId_;
}

const std::vector<SpellFlyoutSlot>& SpellFlyoutPanel::GetSlots() const {
    return slots_;
}

uint8_t SpellFlyoutPanel::GetSlotCount() const {
    return static_cast<uint8_t>(slots_.size());
}

SpellFlyoutDirection SpellFlyoutPanel::GetDirection() const {
    return direction_;
}

void SpellFlyoutPanel::SelectSlot(uint8_t index) {
    if (open_ && index < slots_.size()) {
        selectedSpellId_ = slots_[index].spellId;
        open_ = false;
    }
}

std::optional<uint32_t> SpellFlyoutPanel::GetSelectedSpellId() const {
    return selectedSpellId_;
}

void SpellFlyoutPanel::SetSlotUsable(uint8_t index, bool usable) {
    if (index < slots_.size()) {
        slots_[index].isUsable = usable;
    }
}

bool SpellFlyoutPanel::HasKnownSpells() const {
    return std::any_of(slots_.begin(), slots_.end(),
                       [](const SpellFlyoutSlot& s) { return s.isKnown; });
}

const std::string& SpellFlyoutPanel::GetName() const {
    return name_;
}

uint32_t SpellFlyoutPanel::GetIconId() const {
    return iconId_;
}

std::vector<SpellFlyoutSlot> SpellFlyoutPanel::FilterKnownSlots() const {
    std::vector<SpellFlyoutSlot> result;
    result.reserve(slots_.size());
    for (const auto& s : slots_) {
        if (s.isKnown) result.push_back(s);
    }
    return result;
}

std::vector<SpellFlyoutSlot> SpellFlyoutPanel::FilterUsableSlots() const {
    std::vector<SpellFlyoutSlot> result;
    result.reserve(slots_.size());
    for (const auto& s : slots_) {
        if (s.isUsable) result.push_back(s);
    }
    return result;
}

void SpellFlyoutPanel::CycleForward() {
    if (slots_.empty() || !open_) return;
    if (!selectedIndex_) {
        selectedIndex_ = 0;
    } else {
        selectedIndex_ = static_cast<uint8_t>(
            (*selectedIndex_ + 1) % static_cast<uint8_t>(slots_.size()));
    }
    selectedSpellId_ = slots_[*selectedIndex_].spellId;
}

void SpellFlyoutPanel::CycleBackward() {
    if (slots_.empty() || !open_) return;
    if (!selectedIndex_) {
        selectedIndex_ = static_cast<uint8_t>(slots_.size() - 1);
    } else if (*selectedIndex_ == 0) {
        selectedIndex_ = static_cast<uint8_t>(slots_.size() - 1);
    } else {
        selectedIndex_ = static_cast<uint8_t>(*selectedIndex_ - 1);
    }
    selectedSpellId_ = slots_[*selectedIndex_].spellId;
}

std::optional<uint8_t> SpellFlyoutPanel::GetSelectedIndex() const {
    return selectedIndex_;
}

void SpellFlyoutPanel::ResetSelection() {
    selectedIndex_.reset();
    selectedSpellId_.reset();
}

std::optional<SpellFlyoutSlot> SpellFlyoutPanel::GetSlotBySpellId(
    uint32_t spellId) const {
    for (const auto& s : slots_) {
        if (s.spellId == spellId) return s;
    }
    return std::nullopt;
}

void SpellFlyoutPanel::SetDirection(SpellFlyoutDirection dir) {
    direction_ = dir;
}

void SpellFlyoutPanel::MarkSlotKnown(uint8_t index, bool known) {
    if (index < slots_.size()) {
        slots_[index].isKnown = known;
    }
}

uint8_t SpellFlyoutPanel::GetUsableSlotCount() const {
    uint8_t count = 0;
    for (const auto& s : slots_) {
        if (s.isUsable) ++count;
    }
    return count;
}

uint8_t SpellFlyoutPanel::GetKnownSlotCount() const {
    uint8_t count = 0;
    for (const auto& s : slots_) {
        if (s.isKnown) ++count;
    }
    return count;
}

}
