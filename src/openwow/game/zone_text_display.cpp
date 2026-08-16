
#include "openwow/game/zone_text_display.h"

#include <algorithm>

namespace openwow::game {

void ZoneTextDisplay::ShowZoneText(const std::string& zoneName,
                                   float r, float g, float b) {

    auto* existing = FindEntry(ZoneTextType::ZoneName);
    if (existing) {
        *existing = ZoneTextEntry{zoneName, ZoneTextType::ZoneName,
                                  r, g, b, kDefaultDuration, 0.0f,
                                  kDefaultFadeStart};
    } else {
        entries_.push_back({zoneName, ZoneTextType::ZoneName,
                            r, g, b, kDefaultDuration, 0.0f,
                            kDefaultFadeStart});
    }
}

void ZoneTextDisplay::ShowSubzoneText(const std::string& subzone) {
    auto* existing = FindEntry(ZoneTextType::SubzoneName);
    if (existing) {
        *existing = ZoneTextEntry{subzone, ZoneTextType::SubzoneName,
                                  0.9f, 0.9f, 0.9f, kDefaultDuration, 0.0f,
                                  kDefaultFadeStart};
    } else {
        entries_.push_back({subzone, ZoneTextType::SubzoneName,
                            0.9f, 0.9f, 0.9f, kDefaultDuration, 0.0f,
                            kDefaultFadeStart});
    }
}

void ZoneTextDisplay::ShowPvPText(const std::string& text,
                                  float r, float g, float b) {
    auto* existing = FindEntry(ZoneTextType::PvPStatus);
    if (existing) {
        *existing = ZoneTextEntry{text, ZoneTextType::PvPStatus,
                                  r, g, b, kDefaultDuration, 0.0f,
                                  kDefaultFadeStart};
    } else {
        entries_.push_back({text, ZoneTextType::PvPStatus,
                            r, g, b, kDefaultDuration, 0.0f,
                            kDefaultFadeStart});
    }
}

void ZoneTextDisplay::ShowInstanceText(const std::string& name) {
    auto* existing = FindEntry(ZoneTextType::InstanceName);
    if (existing) {
        *existing = ZoneTextEntry{name, ZoneTextType::InstanceName,
                                  1.0f, 0.82f, 0.0f, kDefaultDuration, 0.0f,
                                  kDefaultFadeStart};
    } else {
        entries_.push_back({name, ZoneTextType::InstanceName,
                            1.0f, 0.82f, 0.0f, kDefaultDuration, 0.0f,
                            kDefaultFadeStart});
    }
}

std::vector<ZoneTextEntry> ZoneTextDisplay::GetActiveTexts() const {
    return entries_;
}

void ZoneTextDisplay::Update(float dt) {
    for (auto& e : entries_) {
        e.elapsed += dt;
    }

    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(),
                       [](const ZoneTextEntry& e) {
                           return e.elapsed >= e.duration;
                       }),
        entries_.end());
}

std::optional<ZoneTextEntry> ZoneTextDisplay::GetCurrentZoneText() const {
    const auto* p = FindEntry(ZoneTextType::ZoneName);
    if (p) return *p;
    return std::nullopt;
}

std::optional<ZoneTextEntry> ZoneTextDisplay::GetCurrentSubzoneText() const {
    const auto* p = FindEntry(ZoneTextType::SubzoneName);
    if (p) return *p;
    return std::nullopt;
}

bool ZoneTextDisplay::IsShowing() const {
    return !entries_.empty();
}

float ZoneTextDisplay::GetFadeAlpha(ZoneTextType type) const {
    const auto* p = FindEntry(type);
    if (!p) return 0.0f;

    if (p->elapsed >= p->duration)     return 0.0f;
    if (p->elapsed < p->fadeStart)     return 1.0f;

    const float fadeDuration = p->duration - p->fadeStart;
    if (fadeDuration <= 0.0f) return 0.0f;

    return 1.0f - (p->elapsed - p->fadeStart) / fadeDuration;
}

void ZoneTextDisplay::ClearAll() {
    entries_.clear();
}

void ZoneTextDisplay::Reset() {
    entries_.clear();
}

ZoneTextEntry* ZoneTextDisplay::FindEntry(ZoneTextType type) {
    for (auto& e : entries_) {
        if (e.type == type) return &e;
    }
    return nullptr;
}

const ZoneTextEntry* ZoneTextDisplay::FindEntry(ZoneTextType type) const {
    for (const auto& e : entries_) {
        if (e.type == type) return &e;
    }
    return nullptr;
}

}
