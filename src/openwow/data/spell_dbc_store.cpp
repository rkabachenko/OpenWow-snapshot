
#include "openwow/data/spell_dbc_store.h"

#include <algorithm>
#include <cctype>

namespace openwow::data {

static std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool SpellDBCStore::AddEntry(SpellDBCEntry entry) {
    if (entry.id == 0) return false;
    auto [_, ok] = entries_.emplace(entry.id, std::move(entry));
    return ok;
}

std::optional<SpellDBCEntry> SpellDBCStore::GetEntry(uint32_t spellId) const {
    auto it = entries_.find(spellId);
    if (it == entries_.end()) return std::nullopt;
    return it->second;
}

std::string SpellDBCStore::GetName(uint32_t spellId) const {
    auto it = entries_.find(spellId);
    return it != entries_.end() ? it->second.name : "";
}

std::string SpellDBCStore::GetDescription(uint32_t spellId) const {
    auto it = entries_.find(spellId);
    return it != entries_.end() ? it->second.description : "";
}

std::string SpellDBCStore::GetIconPath(uint32_t spellId) const {
    auto it = entries_.find(spellId);
    return it != entries_.end() ? it->second.iconPath : "";
}

uint32_t SpellDBCStore::GetManaCost(uint32_t spellId) const {
    auto it = entries_.find(spellId);
    return it != entries_.end() ? it->second.manaCost : 0;
}

float SpellDBCStore::GetCastTime(uint32_t spellId) const {
    auto it = entries_.find(spellId);
    if (it == entries_.end()) return 0.0f;
    return ResolveCastTime(it->second.castingTimeIndex);
}

std::pair<float, float> SpellDBCStore::GetRange(uint32_t spellId) const {
    auto it = entries_.find(spellId);
    if (it == entries_.end()) return {0.0f, 0.0f};
    return ResolveRange(it->second.rangeIndex);
}

std::string SpellDBCStore::GetSchoolName(uint32_t school) {

    if (school & 0x01) return "Physical";
    if (school & 0x02) return "Holy";
    if (school & 0x04) return "Fire";
    if (school & 0x08) return "Nature";
    if (school & 0x10) return "Frost";
    if (school & 0x20) return "Shadow";
    if (school & 0x40) return "Arcane";
    return "Unknown";
}

std::vector<uint32_t> SpellDBCStore::SearchByName(const std::string& query) const {
    std::vector<uint32_t> results;
    const auto lq = ToLower(query);
    for (auto& [id, entry] : entries_) {
        if (ToLower(entry.name).find(lq) != std::string::npos) {
            results.push_back(id);
        }
    }
    std::sort(results.begin(), results.end());
    return results;
}

size_t SpellDBCStore::GetEntryCount() const { return entries_.size(); }

void SpellDBCStore::RegisterDefaults() {
    {
        SpellDBCEntry e{};
        e.id   = 133;
        e.name = "Fireball";
        e.rank = "Rank 1";
        e.description = "Hurls a fiery ball that causes Fire damage.";
        e.school = 0x04;
        e.castingTimeIndex = 4;
        e.rangeIndex = 4;
        e.manaCost = 30;
        e.iconPath = "Interface\\Icons\\Spell_Fire_FlameBolt";
        e.powerType = 0;
        e.effectId[0] = 2;
        e.effectBasePoints[0] = 14.0f;
        AddEntry(std::move(e));
    }
    {
        SpellDBCEntry e{};
        e.id   = 116;
        e.name = "Frostbolt";
        e.rank = "Rank 1";
        e.description = "Launches a bolt of frost at the enemy, causing Frost damage and slowing movement.";
        e.school = 0x10;
        e.castingTimeIndex = 3;
        e.rangeIndex = 4;
        e.manaCost = 25;
        e.iconPath = "Interface\\Icons\\Spell_Frost_FrostBolt02";
        e.powerType = 0;
        e.effectId[0] = 2;
        e.effectBasePoints[0] = 18.0f;
        AddEntry(std::move(e));
    }
    {
        SpellDBCEntry e{};
        e.id   = 585;
        e.name = "Smite";
        e.rank = "Rank 1";
        e.description = "Smite an enemy for Holy damage.";
        e.school = 0x02;
        e.castingTimeIndex = 2;
        e.rangeIndex = 5;
        e.manaCost = 20;
        e.iconPath = "Interface\\Icons\\Spell_Holy_Smite";
        e.powerType = 0;
        e.effectId[0] = 2;
        e.effectBasePoints[0] = 13.0f;
        AddEntry(std::move(e));
    }
    {
        SpellDBCEntry e{};
        e.id   = 172;
        e.name = "Corruption";
        e.rank = "Rank 1";
        e.description = "Corrupts the target, causing Shadow damage over 18 sec.";
        e.school = 0x20;
        e.castingTimeIndex = 2;
        e.rangeIndex = 5;
        e.manaCost = 35;
        e.iconPath = "Interface\\Icons\\Spell_Shadow_AbominationExplosion";
        e.powerType = 0;
        e.effectId[0] = 6;
        e.effectAura[0] = 3;
        e.effectBasePoints[0] = 40.0f;
        e.effectAmplitude[0] = 3.0f;
        AddEntry(std::move(e));
    }
    {
        SpellDBCEntry e{};
        e.id   = 75;
        e.name = "Auto Shot";
        e.description = "Automatically shoots the target.";
        e.school = 0x01;
        e.castingTimeIndex = 0;
        e.rangeIndex = 6;
        e.manaCost = 0;
        e.iconPath = "Interface\\Icons\\Ability_Whirlwind";
        e.powerType = 0;
        AddEntry(std::move(e));
    }
}

void SpellDBCStore::Clear() {
    entries_.clear();
}

float SpellDBCStore::ResolveCastTime(uint32_t index) {

    switch (index) {
        case 0:  return 0.0f;
        case 1:  return 1.0f;
        case 2:  return 2.5f;
        case 3:  return 3.0f;
        case 4:  return 3.5f;
        case 5:  return 1.5f;
        case 6:  return 2.0f;
        case 7:  return 4.0f;
        case 8:  return 5.0f;
        case 9:  return 10.0f;
        default: return 0.0f;
    }
}

std::pair<float, float> SpellDBCStore::ResolveRange(uint32_t index) {

    switch (index) {
        case 0:  return {0.0f, 0.0f};
        case 1:  return {0.0f, 5.0f};
        case 2:  return {0.0f, 10.0f};
        case 3:  return {0.0f, 20.0f};
        case 4:  return {0.0f, 35.0f};
        case 5:  return {0.0f, 30.0f};
        case 6:  return {5.0f, 35.0f};
        case 7:  return {0.0f, 40.0f};
        case 8:  return {0.0f, 45.0f};
        case 9:  return {0.0f, 100.0f};
        default: return {0.0f, 0.0f};
    }
}

}
