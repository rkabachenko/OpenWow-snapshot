
#include "openwow/game/totem_bar.h"

#include <algorithm>

namespace openwow::game {

static uint32_t SlotIndex(TotemElement e) {
    return static_cast<uint32_t>(e);
}

void TotemBar::SetTotem(TotemElement element, uint32_t spellId,
                        const std::string& name, float duration,
                        ObjectGuid guid) {
    auto& slot       = slots_[SlotIndex(element)];
    slot.element     = element;
    slot.spellId     = spellId;
    slot.name        = name;
    slot.duration    = duration;
    slot.remaining   = duration;
    slot.hasTotem    = true;
    slot.guid        = guid;
}

void TotemBar::DestroyTotem(TotemElement element) {
    auto& slot     = slots_[SlotIndex(element)];
    slot           = TotemSlotInfo{};
    slot.element   = element;
}

std::optional<TotemSlotInfo> TotemBar::GetTotem(TotemElement element) const {
    const auto& slot = slots_[SlotIndex(element)];
    if (!slot.hasTotem) return std::nullopt;
    return slot;
}

bool TotemBar::HasTotem(TotemElement element) const {
    return slots_[SlotIndex(element)].hasTotem;
}

std::vector<TotemSlotInfo> TotemBar::GetAllTotems() const {
    std::vector<TotemSlotInfo> result;
    for (const auto& s : slots_) {
        if (s.hasTotem) result.push_back(s);
    }
    return result;
}

uint32_t TotemBar::GetActiveTotemCount() const {
    uint32_t count = 0;
    for (const auto& s : slots_) {
        if (s.hasTotem) ++count;
    }
    return count;
}

float TotemBar::GetRemainingTime(TotemElement element) const {
    const auto& slot = slots_[SlotIndex(element)];
    return slot.hasTotem ? slot.remaining : 0.0f;
}

float TotemBar::GetProgress(TotemElement element) const {
    const auto& slot = slots_[SlotIndex(element)];
    if (!slot.hasTotem || slot.duration <= 0.0f) return 1.0f;
    float elapsed = slot.duration - slot.remaining;
    return std::clamp(elapsed / slot.duration, 0.0f, 1.0f);
}

void TotemBar::Update(float dt) {
    for (auto& slot : slots_) {
        if (!slot.hasTotem) continue;
        slot.remaining -= dt;
        if (slot.remaining <= 0.0f) {
            auto elem = slot.element;
            slot = TotemSlotInfo{};
            slot.element = elem;
        }
    }
}

void TotemBar::DestroyAllTotems() {
    for (uint32_t i = 0; i < kTotemElementCount; ++i) {
        auto elem = static_cast<TotemElement>(i);
        slots_[i] = TotemSlotInfo{};
        slots_[i].element = elem;
    }
}

void TotemBar::AddSet(const TotemSetEntry& entry) {

    for (auto& s : sets_) {
        if (s.setId == entry.setId) {
            s = entry;
            return;
        }
    }
    sets_.push_back(entry);
}

std::optional<TotemSetEntry> TotemBar::GetSet(uint32_t setId) const {
    for (const auto& s : sets_) {
        if (s.setId == setId) return s;
    }
    return std::nullopt;
}

std::vector<TotemSetEntry> TotemBar::GetSets() const {
    return sets_;
}

void TotemBar::SaveSet(uint32_t setId, const std::string& name) {
    TotemSetEntry entry;
    entry.setId = setId;
    entry.name  = name;
    for (uint32_t i = 0; i < kTotemElementCount; ++i) {
        entry.spells[i] = slots_[i].hasTotem ? slots_[i].spellId : 0;
    }
    AddSet(entry);
}

uint32_t TotemBar::GetSetCount() const {
    return static_cast<uint32_t>(sets_.size());
}

std::string TotemBar::GetElementName(TotemElement element) {
    switch (element) {
        case TotemElement::Fire:  return "Fire";
        case TotemElement::Earth: return "Earth";
        case TotemElement::Water: return "Water";
        case TotemElement::Air:   return "Air";
    }
    return "Unknown";
}

uint32_t TotemBar::GetElementColor(TotemElement element) {

    switch (element) {
        case TotemElement::Fire:  return 0xFFFF4500;
        case TotemElement::Earth: return 0xFF8B4513;
        case TotemElement::Water: return 0xFF1E90FF;
        case TotemElement::Air:   return 0xFF87CEEB;
    }
    return 0xFFFFFFFF;
}

void TotemBar::Reset() {
    DestroyAllTotems();
    sets_.clear();
}

}
