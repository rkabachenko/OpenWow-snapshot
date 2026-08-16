
#include "openwow/render/effects/spell_visuals/spell_visual_data.h"

#include <algorithm>
#include <cmath>

namespace openwow::render {

std::uint32_t VisualColorMod::ToARGB() const {
    auto clamp01 = [](float v) -> std::uint8_t {
        if (v <= 0.0f) return 0;
        if (v >= 1.0f) return 255;
        return static_cast<std::uint8_t>(v * 255.0f + 0.5f);
    };
    return (static_cast<std::uint32_t>(clamp01(a)) << 24) |
           (static_cast<std::uint32_t>(clamp01(r)) << 16) |
           (static_cast<std::uint32_t>(clamp01(g)) << 8)  |
            static_cast<std::uint32_t>(clamp01(b));
}

VisualColorMod VisualColorMod::FromARGB(std::uint32_t argb) {
    VisualColorMod c;
    c.a = static_cast<float>((argb >> 24) & 0xFF) / 255.0f;
    c.r = static_cast<float>((argb >> 16) & 0xFF) / 255.0f;
    c.g = static_cast<float>((argb >>  8) & 0xFF) / 255.0f;
    c.b = static_cast<float>((argb      ) & 0xFF) / 255.0f;
    return c;
}

VisualColorMod VisualColorMod::Multiply(const VisualColorMod& other) const {
    return {r * other.r, g * other.g, b * other.b, a * other.a};
}

VisualColorMod VisualColorMod::Lerp(const VisualColorMod& to, float t) const {
    t = std::clamp(t, 0.0f, 1.0f);
    return {
        r + (to.r - r) * t,
        g + (to.g - g) * t,
        b + (to.b - b) * t,
        a + (to.a - a) * t,
    };
}

void SpellVisualDataStore::AddVisualKit(SpellVisualKitEntry entry) {
    if (entry.kitId == 0) return;
    kits_[entry.kitId] = std::move(entry);
}

bool SpellVisualDataStore::RemoveVisualKit(std::uint32_t kitId) {
    return kits_.erase(kitId) > 0;
}

std::optional<SpellVisualKitEntry> SpellVisualDataStore::GetVisualKit(
    std::uint32_t kitId) const {
    auto it = kits_.find(kitId);
    if (it != kits_.end()) return it->second;
    return std::nullopt;
}

std::vector<SpellVisualKitEntry> SpellVisualDataStore::GetVisualsByType(
    SpellVisualType type) const {
    std::vector<SpellVisualKitEntry> out;
    out.reserve(16);
    for (const auto& [_, kit] : kits_) {
        if (kit.visualType == type) out.push_back(kit);
    }
    return out;
}

std::optional<SpellVisualKitEntry> SpellVisualDataStore::GetVisualForSpell(
    std::uint32_t spellId) const {
    auto mIt = spellToKit_.find(spellId);
    if (mIt == spellToKit_.end()) return std::nullopt;
    return GetVisualKit(mIt->second);
}

void SpellVisualDataStore::MapSpellToKit(std::uint32_t spellId,
                                         std::uint32_t kitId) {
    if (spellId == 0 || kitId == 0) return;
    spellToKit_[spellId] = kitId;
}

bool SpellVisualDataStore::UnmapSpell(std::uint32_t spellId) {
    return spellToKit_.erase(spellId) > 0;
}

bool SpellVisualDataStore::HasVisualForSpell(std::uint32_t spellId) const {
    return spellToKit_.find(spellId) != spellToKit_.end();
}

std::uint32_t SpellVisualDataStore::GetKitCount() const {
    return static_cast<std::uint32_t>(kits_.size());
}

std::uint32_t SpellVisualDataStore::GetTotalMappings() const {
    return static_cast<std::uint32_t>(spellToKit_.size());
}

std::string SpellVisualDataStore::GetTypeName(SpellVisualType type) {
    switch (type) {
        case SpellVisualType::Cast:       return "Cast";
        case SpellVisualType::Channel:    return "Channel";
        case SpellVisualType::Impact:     return "Impact";
        case SpellVisualType::Missile:    return "Missile";
        case SpellVisualType::Persistent: return "Persistent";
        case SpellVisualType::Area:       return "Area";
        case SpellVisualType::State:      return "State";
    }
    return "Unknown";
}

std::string SpellVisualDataStore::GetAttachmentPointName(VisualAttachSlot point) {
    switch (point) {
        case VisualAttachSlot::None:      return "None";
        case VisualAttachSlot::HandRight: return "HandRight";
        case VisualAttachSlot::HandLeft:  return "HandLeft";
        case VisualAttachSlot::Chest:     return "Chest";
        case VisualAttachSlot::Head:      return "Head";
        case VisualAttachSlot::Overhead:  return "Overhead";
        case VisualAttachSlot::Back:      return "Back";
        case VisualAttachSlot::Shield:    return "Shield";
        case VisualAttachSlot::SpellHand: return "SpellHand";
        case VisualAttachSlot::Feet:      return "Feet";
        case VisualAttachSlot::Ground:    return "Ground";
        case VisualAttachSlot::Origin:    return "Origin";
    }
    return "Unknown";
}

VisualAttachSlot SpellVisualDataStore::ResolveAttachmentPoint(std::uint32_t slot) {
    if (slot > static_cast<std::uint32_t>(VisualAttachSlot::Origin)) {
        return VisualAttachSlot::None;
    }
    return static_cast<VisualAttachSlot>(slot);
}

SpellVisualType SpellVisualDataStore::ClassifyEffectType(
    bool isArea, bool isMissile, bool isChannel) {
    if (isArea)    return SpellVisualType::Area;
    if (isMissile) return SpellVisualType::Missile;
    if (isChannel) return SpellVisualType::Channel;
    return SpellVisualType::Cast;
}

void SpellVisualDataStore::ApplyColorMod(std::uint32_t kitId,
                                         const VisualColorMod& mod) {
    auto it = kits_.find(kitId);
    if (it == kits_.end()) return;
    it->second.colorMod = it->second.colorMod.Multiply(mod);

    if (it->second.hasTrail) {
        auto trailCol = VisualColorMod::FromARGB(it->second.trailColor);
        trailCol = trailCol.Multiply(mod);
        it->second.trailColor = trailCol.ToARGB();
    }
}

void SpellVisualDataStore::AddVisualKitsBatch(
    const std::vector<SpellVisualKitEntry>& entries) {
    for (const auto& entry : entries) {
        AddVisualKit(entry);
    }
}

std::vector<std::uint32_t> SpellVisualDataStore::GetAllKitIds() const {
    std::vector<std::uint32_t> ids;
    ids.reserve(kits_.size());
    for (const auto& [id, _] : kits_) {
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

bool SpellVisualDataStore::ValidateKit(const SpellVisualKitEntry& kit) {
    if (kit.kitId == 0) return false;
    if (kit.scale <= 0.0f) return false;
    if (kit.duration <= 0.0f) return false;

    if (kit.visualType == SpellVisualType::Missile && kit.modelPath.empty()) {
        return false;
    }
    return true;
}

void SpellVisualDataStore::Clear() {
    kits_.clear();
    spellToKit_.clear();
}

}
