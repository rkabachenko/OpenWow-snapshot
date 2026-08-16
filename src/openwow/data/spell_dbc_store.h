#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace openwow::data {

struct SpellDBCEntry {
    uint32_t id = 0;
    std::string name;
    std::string rank;
    std::string description;
    std::string tooltip;

    uint32_t school    = 0;
    uint32_t category  = 0;
    uint32_t dispelType = 0;
    uint32_t mechanic  = 0;
    uint32_t attributes[8] = {};

    uint32_t castingTimeIndex = 0;
    uint32_t durationIndex    = 0;
    uint32_t rangeIndex       = 0;
    float    speed            = 0.0f;

    uint32_t iconId       = 0;
    std::string iconPath;
    uint32_t activeIconId = 0;

    uint32_t manaCost    = 0;
    uint32_t manaCostPct = 0;

    uint32_t reagent[8]      = {};
    uint32_t reagentCount[8] = {};

    uint32_t effectId[3]                = {};
    float    effectBasePoints[3]        = {};
    uint32_t effectMechanic[3]          = {};
    uint32_t effectImplicitTargetA[3]   = {};
    uint32_t effectImplicitTargetB[3]   = {};
    uint32_t effectMiscValue[3]         = {};
    uint32_t effectAura[3]              = {};
    float    effectAmplitude[3]         = {};

    uint32_t powerType = 0;
};

class SpellDBCStore {
public:
    SpellDBCStore() = default;

    bool AddEntry(SpellDBCEntry entry);

    [[nodiscard]] std::optional<SpellDBCEntry> GetEntry(uint32_t spellId) const;
    [[nodiscard]] std::string  GetName(uint32_t spellId) const;
    [[nodiscard]] std::string  GetDescription(uint32_t spellId) const;
    [[nodiscard]] std::string  GetIconPath(uint32_t spellId) const;
    [[nodiscard]] uint32_t     GetManaCost(uint32_t spellId) const;

    [[nodiscard]] float GetCastTime(uint32_t spellId) const;

    [[nodiscard]] std::pair<float, float> GetRange(uint32_t spellId) const;

    [[nodiscard]] static std::string GetSchoolName(uint32_t school);

    [[nodiscard]] std::vector<uint32_t> SearchByName(const std::string& query) const;

    [[nodiscard]] size_t GetEntryCount() const;

    void RegisterDefaults();

    void Clear();

private:
    std::unordered_map<uint32_t, SpellDBCEntry> entries_;

    static float ResolveCastTime(uint32_t index);

    static std::pair<float, float> ResolveRange(uint32_t index);
};

}
