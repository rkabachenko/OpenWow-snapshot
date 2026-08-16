
#include "openwow/game/encounter_journal_data.h"

#include <algorithm>
#include <cctype>

namespace openwow::game {

namespace {

std::string ToLower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

}

void EncounterJournalData::AddInstance(EncounterInstanceInfo info) {

    for (auto& inst : instances_) {
        if (inst.instanceId == info.instanceId) {
            inst = std::move(info);
            return;
        }
    }
    instances_.push_back(std::move(info));
}

std::optional<EncounterInstanceInfo> EncounterJournalData::GetInstance(
    std::uint32_t instanceId) const {
    for (const auto& inst : instances_) {
        if (inst.instanceId == instanceId) return inst;
    }
    return std::nullopt;
}

std::vector<EncounterInstanceInfo> EncounterJournalData::GetAllInstances()
    const {
    return instances_;
}

std::vector<EncounterInstanceInfo> EncounterJournalData::GetDungeons() const {
    std::vector<EncounterInstanceInfo> result;
    for (const auto& inst : instances_) {
        if (inst.type == "Dungeon") result.push_back(inst);
    }
    return result;
}

std::vector<EncounterInstanceInfo> EncounterJournalData::GetRaids() const {
    std::vector<EncounterInstanceInfo> result;
    for (const auto& inst : instances_) {
        if (inst.type == "Raid") result.push_back(inst);
    }
    return result;
}

std::optional<EncounterBossInfo> EncounterJournalData::GetBoss(
    std::uint32_t bossId) const {
    for (const auto& inst : instances_) {
        for (const auto& boss : inst.bosses) {
            if (boss.bossId == bossId) return boss;
        }
    }
    return std::nullopt;
}

std::vector<EncounterAbilityInfo> EncounterJournalData::GetBossAbilities(
    std::uint32_t bossId) const {
    for (const auto& inst : instances_) {
        for (const auto& boss : inst.bosses) {
            if (boss.bossId == bossId) return boss.abilities;
        }
    }
    return {};
}

std::vector<EncounterInstanceInfo>
EncounterJournalData::GetInstancesForExpansion(std::uint8_t expansion) const {
    std::vector<EncounterInstanceInfo> result;
    for (const auto& inst : instances_) {
        if (inst.expansion == expansion) result.push_back(inst);
    }
    return result;
}

std::vector<EncounterBossInfo> EncounterJournalData::SearchBoss(
    const std::string& query) const {
    std::vector<EncounterBossInfo> result;
    std::string lowerQuery = ToLower(query);
    for (const auto& inst : instances_) {
        for (const auto& boss : inst.bosses) {
            if (ToLower(boss.name).find(lowerQuery) != std::string::npos) {
                result.push_back(boss);
            }
        }
    }
    return result;
}

std::uint32_t EncounterJournalData::GetBossCount() const {
    std::uint32_t count = 0;
    for (const auto& inst : instances_) {
        count += static_cast<std::uint32_t>(inst.bosses.size());
    }
    return count;
}

std::uint32_t EncounterJournalData::GetInstanceCount() const {
    return static_cast<std::uint32_t>(instances_.size());
}

void EncounterJournalData::Clear() {
    instances_.clear();
}

void EncounterJournalData::Open() {
    open_ = true;
}

void EncounterJournalData::Close() {
    open_ = false;
}

bool EncounterJournalData::IsOpen() const {
    return open_;
}

}
