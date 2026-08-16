#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace openwow::data {

struct CreatureDisplayEntry {
    uint32_t id                    = 0;
    uint32_t modelId               = 0;
    uint32_t soundId               = 0;
    uint32_t extendedDisplayId     = 0;
    float    creatureModelScale    = 1.0f;
    uint32_t creatureModelAlpha    = 255;
    uint32_t textureVariation[3]   = {};
    std::string portraitTexture;
    uint32_t sizeClass            = 0;
    uint32_t bloodLevel            = 0;
    uint32_t blood                 = 0;
    uint32_t npcSoundId            = 0;
    uint32_t particleColorId       = 0;
    uint32_t creatureGeosetData    = 0;
    uint32_t objectEffectPackageId = 0;
};

struct CreatureModelEntry {
    uint32_t id             = 0;
    std::string modelPath;
    float    modelScale     = 1.0f;
    float    boundingRadius = 0.0f;
    float    combatReach    = 0.0f;
    uint32_t flags          = 0;
};

class CreatureDisplayStore {
public:
    CreatureDisplayStore() = default;

    bool AddDisplayEntry(CreatureDisplayEntry entry);
    bool AddModelEntry(CreatureModelEntry entry);

    [[nodiscard]] std::optional<CreatureDisplayEntry> GetDisplayEntry(uint32_t displayId) const;
    [[nodiscard]] std::optional<CreatureModelEntry>   GetModelEntry(uint32_t modelId) const;

    [[nodiscard]] std::string GetModelPath(uint32_t displayId) const;
    [[nodiscard]] float       GetModelScale(uint32_t displayId) const;
    [[nodiscard]] float       GetBoundingRadius(uint32_t displayId) const;
    [[nodiscard]] float       GetCombatReach(uint32_t displayId) const;

    [[nodiscard]] size_t GetDisplayCount() const;
    [[nodiscard]] size_t GetModelCount() const;
    void Clear();

private:
    std::unordered_map<uint32_t, CreatureDisplayEntry> displays_;
    std::unordered_map<uint32_t, CreatureModelEntry>   models_;
};

}
