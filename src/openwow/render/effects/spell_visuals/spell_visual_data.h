#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::render {

enum class SpellVisualType : std::uint8_t {
    Cast = 0,
    Channel,
    Impact,
    Missile,
    Persistent,
    Area,
    State,
};

enum class VisualAttachSlot : std::uint8_t {
    None = 0,
    HandRight,
    HandLeft,
    Chest,
    Head,
    Overhead,
    Back,
    Shield,
    SpellHand,
    Feet,
    Ground,
    Origin,
};

struct VisualColorMod {
    float r{1.0f};
    float g{1.0f};
    float b{1.0f};
    float a{1.0f};

    [[nodiscard]] std::uint32_t ToARGB() const;
    static VisualColorMod FromARGB(std::uint32_t argb);
    [[nodiscard]] VisualColorMod Multiply(const VisualColorMod& other) const;
    [[nodiscard]] VisualColorMod Lerp(const VisualColorMod& to, float t) const;
};

struct SpellVisualKitEntry {
    std::uint32_t  kitId      = 0;
    SpellVisualType visualType = SpellVisualType::Cast;
    std::string    modelPath;
    std::uint32_t  animId     = 0;
    float          scale      = 1.0f;
    float          duration   = 1.0f;
    bool           hasTrail   = false;
    std::uint32_t  trailColor = 0xFFFFFFFF;
    std::uint32_t  attachSlot = 0;
    VisualAttachSlot attachPoint = VisualAttachSlot::None;
    VisualColorMod colorMod;
};

class SpellVisualDataStore {
public:
    SpellVisualDataStore() = default;

    void AddVisualKit(SpellVisualKitEntry entry);

    bool RemoveVisualKit(std::uint32_t kitId);

    [[nodiscard]] std::optional<SpellVisualKitEntry> GetVisualKit(std::uint32_t kitId) const;

    [[nodiscard]] std::vector<SpellVisualKitEntry> GetVisualsByType(SpellVisualType type) const;

    [[nodiscard]] std::optional<SpellVisualKitEntry> GetVisualForSpell(std::uint32_t spellId) const;

    void MapSpellToKit(std::uint32_t spellId, std::uint32_t kitId);

    bool UnmapSpell(std::uint32_t spellId);

    [[nodiscard]] std::uint32_t GetKitCount() const;

    [[nodiscard]] std::uint32_t GetTotalMappings() const;

    [[nodiscard]] static std::string GetTypeName(SpellVisualType type);

    [[nodiscard]] bool HasVisualForSpell(std::uint32_t spellId) const;

    [[nodiscard]] static std::string GetAttachmentPointName(VisualAttachSlot point);

    [[nodiscard]] static VisualAttachSlot ResolveAttachmentPoint(std::uint32_t slot);

    [[nodiscard]] static SpellVisualType ClassifyEffectType(
        bool isArea, bool isMissile, bool isChannel);

    void ApplyColorMod(std::uint32_t kitId, const VisualColorMod& mod);

    void AddVisualKitsBatch(const std::vector<SpellVisualKitEntry>& entries);

    [[nodiscard]] std::vector<std::uint32_t> GetAllKitIds() const;

    [[nodiscard]] static bool ValidateKit(const SpellVisualKitEntry& kit);

    void Clear();

private:
    std::unordered_map<std::uint32_t, SpellVisualKitEntry> kits_;
    std::unordered_map<std::uint32_t, std::uint32_t>       spellToKit_;
};

}
