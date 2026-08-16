#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace openwow::game {

enum class CollectionItemType : uint8_t {
    Mount     = 0,
    Companion = 1,
};

enum class CollectionMountCategory : uint8_t {
    Ground       = 0,
    Flying       = 1,
    AquaticMount = 2,
};

struct CollectionDisplayEntry {
    uint32_t spellId = 0;
    std::string name;
    std::string icon;
    CollectionItemType type = CollectionItemType::Mount;
    CollectionMountCategory mountCategory = CollectionMountCategory::Ground;
    bool isUsable = true;
    bool isFavorite = false;
    uint32_t sourceId = 0;
    std::string sourceName;
};

class CollectionDisplay {
 public:
    void AddEntry(const CollectionDisplayEntry& entry);
    bool RemoveEntry(uint32_t spellId);
    [[nodiscard]] std::optional<CollectionDisplayEntry> GetEntry(uint32_t spellId) const;

    [[nodiscard]] std::vector<const CollectionDisplayEntry*> GetMounts() const;
    [[nodiscard]] std::vector<const CollectionDisplayEntry*> GetCompanions() const;
    [[nodiscard]] std::vector<const CollectionDisplayEntry*> GetUsableMounts() const;
    [[nodiscard]] std::vector<const CollectionDisplayEntry*> GetFavorites() const;

    bool SetFavorite(uint32_t spellId, bool fav);

    [[nodiscard]] size_t GetMountCount() const;
    [[nodiscard]] size_t GetCompanionCount() const;
    [[nodiscard]] size_t GetTotalCount() const;

    [[nodiscard]] std::optional<uint32_t> SummonRandom(CollectionItemType type) const;

    [[nodiscard]] std::vector<const CollectionDisplayEntry*> SearchByName(
        const std::string& query) const;
    [[nodiscard]] std::vector<const CollectionDisplayEntry*> FilterByCategory(
        CollectionMountCategory category) const;

    void SetActiveMountSpell(uint32_t spellId);
    [[nodiscard]] std::optional<uint32_t> GetActiveMountSpell() const;
    void SetActiveCompanionSpell(uint32_t spellId);
    [[nodiscard]] std::optional<uint32_t> GetActiveCompanionSpell() const;

    void Clear();

 private:
    [[nodiscard]] CollectionDisplayEntry* FindEntry(uint32_t spellId);
    [[nodiscard]] const CollectionDisplayEntry* FindEntry(uint32_t spellId) const;

    std::vector<CollectionDisplayEntry> entries_;
    std::optional<uint32_t> activeMountSpell_;
    std::optional<uint32_t> activeCompanionSpell_;
};

}
