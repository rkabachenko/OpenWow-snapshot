
#include "openwow/game/pet_talent.h"

#include <algorithm>

namespace openwow::game {

void PetTalentSystem::SetPetTree(PetTalentTree tree) {
    tree_ = tree;
}

PetTalentTree PetTalentSystem::GetPetTree() const {
    return tree_;
}

void PetTalentSystem::SetTalents(std::vector<PetTalentNode> talents) {
    talents_ = std::move(talents);
}

std::vector<PetTalentNode> PetTalentSystem::GetTalents() const {
    return talents_;
}

std::optional<PetTalentNode> PetTalentSystem::GetTalent(
    uint32_t talentId) const {
    for (const auto& node : talents_) {
        if (node.talentId == talentId) return node;
    }
    return std::nullopt;
}

uint32_t PetTalentSystem::CalcPointsSpent() const {
    uint32_t sum = 0;
    for (const auto& node : talents_) {
        sum += node.currentRank;
    }
    return sum;
}

uint32_t PetTalentSystem::GetPointsSpent() const {
    return CalcPointsSpent();
}

uint32_t PetTalentSystem::GetAvailablePoints() const {
    uint32_t spent = CalcPointsSpent();
    return (total_points_ > spent) ? total_points_ - spent : 0;
}

void PetTalentSystem::SetAvailablePoints(uint32_t points) {
    available_points_ = points;
}

uint32_t PetTalentSystem::GetTotalPoints() const {
    return total_points_;
}

void PetTalentSystem::SetTotalPoints(uint32_t points) {
    total_points_ = points;
}

bool PetTalentSystem::HasDependents(uint32_t talentId,
                                     uint32_t afterRank) const {

    const PetTalentNode* self = nullptr;
    for (const auto& node : talents_) {
        if (node.talentId == talentId) { self = &node; break; }
    }
    if (!self) return false;

    if (afterRank >= self->maxRank) return false;
    for (const auto& node : talents_) {
        if (node.requiredTalentId == talentId && node.currentRank > 0) {
            return true;
        }
    }
    return false;
}

bool PetTalentSystem::CanAddPoint(uint32_t talentId) const {
    if (GetAvailablePoints() == 0) return false;
    auto node = GetTalent(talentId);
    if (!node) return false;
    if (node->currentRank >= node->maxRank) return false;

    uint32_t spent = CalcPointsSpent();
    if (spent < node->tier * 3) return false;

    if (node->requiredTalentId != 0) {
        auto req = GetTalent(node->requiredTalentId);
        if (!req || req->currentRank < req->maxRank) return false;
    }

    return true;
}

bool PetTalentSystem::AddPoint(uint32_t talentId) {
    if (!CanAddPoint(talentId)) return false;
    for (auto& node : talents_) {
        if (node.talentId == talentId) {
            node.currentRank++;
            return true;
        }
    }
    return false;
}

bool PetTalentSystem::RemovePoint(uint32_t talentId) {
    auto opt = GetTalent(talentId);
    if (!opt || opt->currentRank == 0) return false;
    if (HasDependents(talentId, opt->currentRank - 1)) return false;

    for (const auto& other : talents_) {
        uint32_t otherRank = other.currentRank;
        if (other.talentId == talentId) otherRank -= 1;
        if (otherRank == 0 || other.tier == 0) continue;

        uint32_t pointsBelowTier = 0;
        for (const auto& t : talents_) {
            if (t.tier < other.tier) {
                uint32_t r = t.currentRank;
                if (t.talentId == talentId) r -= 1;
                pointsBelowTier += r;
            }
        }
        if (pointsBelowTier < other.tier * 3) return false;
    }

    for (auto& node : talents_) {
        if (node.talentId == talentId) {
            node.currentRank--;
            return true;
        }
    }
    return false;
}

std::string PetTalentSystem::GetTreeName(PetTalentTree tree) {
    switch (tree) {
        case PetTalentTree::Ferocity: return "Ferocity";
        case PetTalentTree::Tenacity: return "Tenacity";
        case PetTalentTree::Cunning:  return "Cunning";
    }
    return "Unknown";
}

std::string PetTalentSystem::GetTreeIcon(PetTalentTree tree) {
    switch (tree) {
        case PetTalentTree::Ferocity:
            return "Interface\\Icons\\Ability_Druid_PrimalTenacity";
        case PetTalentTree::Tenacity:
            return "Interface\\Icons\\Ability_Hunter_Pet_Bear";
        case PetTalentTree::Cunning:
            return "Interface\\Icons\\Ability_Hunter_Pet_Spider";
    }
    return "";
}

void PetTalentSystem::SetPetLevel(uint32_t level) {
    pet_level_ = level;
}

uint32_t PetTalentSystem::GetPetLevel() const {
    return pet_level_;
}

uint32_t PetTalentSystem::GetMaxPointsForLevel(uint32_t level) {
    if (level < 20) return 0;
    return (level - 20) / 4;
}

bool PetTalentSystem::HasPet() const {
    return has_pet_;
}

void PetTalentSystem::SetHasPet(bool has_pet) {
    has_pet_ = has_pet;
}

void PetTalentSystem::Reset() {
    tree_ = PetTalentTree::Ferocity;
    talents_.clear();
    total_points_ = 0;
    available_points_ = 0;
    pet_level_ = 1;
    has_pet_ = false;
}

}
