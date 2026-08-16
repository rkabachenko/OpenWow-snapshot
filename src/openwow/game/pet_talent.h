
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

enum class PetTalentTree : uint32_t {
    Ferocity = 0,
    Tenacity = 1,
    Cunning  = 2,
};

struct PetTalentNode {
    uint32_t talentId         = 0;
    uint32_t tier             = 0;
    uint32_t column           = 0;
    uint32_t currentRank      = 0;
    uint32_t maxRank          = 0;
    uint32_t requiredTalentId = 0;
    uint32_t spellId          = 0;
};

class PetTalentSystem {
 public:
    void SetPetTree(PetTalentTree tree);
    [[nodiscard]] PetTalentTree GetPetTree() const;

    void SetTalents(std::vector<PetTalentNode> talents);
    [[nodiscard]] std::vector<PetTalentNode> GetTalents() const;
    [[nodiscard]] std::optional<PetTalentNode> GetTalent(uint32_t talentId) const;

    [[nodiscard]] uint32_t GetPointsSpent() const;
    [[nodiscard]] uint32_t GetAvailablePoints() const;
    void SetAvailablePoints(uint32_t points);
    [[nodiscard]] uint32_t GetTotalPoints() const;
    void SetTotalPoints(uint32_t points);

    bool AddPoint(uint32_t talentId);
    bool RemovePoint(uint32_t talentId);
    [[nodiscard]] bool CanAddPoint(uint32_t talentId) const;

    [[nodiscard]] static std::string GetTreeName(PetTalentTree tree);
    [[nodiscard]] static std::string GetTreeIcon(PetTalentTree tree);

    void SetPetLevel(uint32_t level);
    [[nodiscard]] uint32_t GetPetLevel() const;
    [[nodiscard]] static uint32_t GetMaxPointsForLevel(uint32_t level);

    [[nodiscard]] bool HasPet() const;
    void SetHasPet(bool has_pet);

    void Reset();

 private:
    [[nodiscard]] uint32_t CalcPointsSpent() const;
    [[nodiscard]] bool HasDependents(uint32_t talentId, uint32_t afterRank) const;

    PetTalentTree              tree_        = PetTalentTree::Ferocity;
    std::vector<PetTalentNode> talents_;
    uint32_t                   total_points_     = 0;
    uint32_t                   available_points_ = 0;
    uint32_t                   pet_level_        = 1;
    bool                       has_pet_          = false;
};

}
