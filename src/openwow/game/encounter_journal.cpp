
#include "openwow/game/encounter_journal.h"

#include <algorithm>

namespace openwow::game {

void EncounterJournal::AddInstance(EncounterInstanceEntry entry) {
    instances_.push_back(std::move(entry));
}

std::optional<EncounterInstanceEntry> EncounterJournal::GetInstance(
    std::uint32_t instanceId) const {
    for (const auto& inst : instances_) {
        if (inst.instanceId == instanceId) return inst;
    }
    return std::nullopt;
}

std::vector<EncounterInstanceEntry> EncounterJournal::GetAllInstances() const {
    return instances_;
}

std::vector<EncounterInstanceEntry> EncounterJournal::GetDungeons() const {
    std::vector<EncounterInstanceEntry> result;
    for (const auto& inst : instances_) {
        if (!inst.isRaid) result.push_back(inst);
    }
    return result;
}

std::vector<EncounterInstanceEntry> EncounterJournal::GetRaids() const {
    std::vector<EncounterInstanceEntry> result;
    for (const auto& inst : instances_) {
        if (inst.isRaid) result.push_back(inst);
    }
    return result;
}

void EncounterJournal::AddBoss(EncounterBossEntry entry) {
    bosses_.push_back(std::move(entry));
}

std::optional<EncounterBossEntry> EncounterJournal::GetBoss(
    std::uint32_t bossId) const {
    for (const auto& b : bosses_) {
        if (b.bossId == bossId) return b;
    }
    return std::nullopt;
}

std::vector<EncounterBossEntry> EncounterJournal::GetBossesForInstance(
    std::uint32_t instanceId) const {
    std::vector<EncounterBossEntry> result;
    for (const auto& b : bosses_) {
        if (b.instanceId == instanceId) result.push_back(b);
    }
    return result;
}

void EncounterJournal::AddAbility(std::uint32_t bossId,
                                  EncounterAbilityEntry entry) {
    abilities_[bossId].push_back(std::move(entry));
}

std::vector<EncounterAbilityEntry> EncounterJournal::GetAbilities(
    std::uint32_t bossId) const {
    auto it = abilities_.find(bossId);
    if (it == abilities_.end()) return {};
    return it->second;
}

std::vector<EncounterBossEntry> EncounterJournal::SearchBosses(
    const std::string& query) const {
    if (query.empty()) return bosses_;

    std::string lowerQuery = query;
    std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    std::vector<EncounterBossEntry> result;
    for (const auto& b : bosses_) {
        std::string lowerName = b.name;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (lowerName.find(lowerQuery) != std::string::npos) {
            result.push_back(b);
        }
    }
    return result;
}

std::uint32_t EncounterJournal::GetTotalBossCount() const {
    return static_cast<std::uint32_t>(bosses_.size());
}

std::uint32_t EncounterJournal::GetTotalInstanceCount() const {
    return static_cast<std::uint32_t>(instances_.size());
}

bool EncounterJournal::IsOpen() const { return open_; }
void EncounterJournal::Open() { open_ = true; }
void EncounterJournal::Close() { open_ = false; }

void EncounterJournal::Reset() {
    instances_.clear();
    bosses_.clear();
    abilities_.clear();
    open_ = false;
}

}
