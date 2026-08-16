
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::game {

enum class EncounterDifficulty : std::uint8_t {
    Normal5 = 0,
    Heroic5,
    Normal10,
    Normal25,
    Heroic10,
    Heroic25,
};

struct EncounterBossEntry {
    std::uint32_t bossId{0};
    std::string name;
    std::string description;
    std::uint32_t instanceId{0};
    std::uint32_t displayId{0};
    std::uint32_t orderIndex{0};
};

struct EncounterAbilityEntry {
    std::uint32_t abilityId{0};
    std::string name;
    std::string description;
    std::uint32_t spellId{0};
    std::string iconPath;
};

struct EncounterInstanceEntry {
    std::uint32_t instanceId{0};
    std::string name;
    std::string description;
    std::uint32_t mapId{0};
    std::uint32_t minLevel{0};
    std::uint32_t maxLevel{0};
    bool isRaid{false};
};

class EncounterJournal {
 public:

    void AddInstance(EncounterInstanceEntry entry);
    [[nodiscard]] std::optional<EncounterInstanceEntry> GetInstance(
        std::uint32_t instanceId) const;
    [[nodiscard]] std::vector<EncounterInstanceEntry> GetAllInstances() const;
    [[nodiscard]] std::vector<EncounterInstanceEntry> GetDungeons() const;
    [[nodiscard]] std::vector<EncounterInstanceEntry> GetRaids() const;

    void AddBoss(EncounterBossEntry entry);
    [[nodiscard]] std::optional<EncounterBossEntry> GetBoss(
        std::uint32_t bossId) const;
    [[nodiscard]] std::vector<EncounterBossEntry> GetBossesForInstance(
        std::uint32_t instanceId) const;

    void AddAbility(std::uint32_t bossId, EncounterAbilityEntry entry);
    [[nodiscard]] std::vector<EncounterAbilityEntry> GetAbilities(
        std::uint32_t bossId) const;

    [[nodiscard]] std::vector<EncounterBossEntry> SearchBosses(
        const std::string& query) const;

    [[nodiscard]] std::uint32_t GetTotalBossCount() const;
    [[nodiscard]] std::uint32_t GetTotalInstanceCount() const;

    [[nodiscard]] bool IsOpen() const;
    void Open();
    void Close();

    void Reset();

 private:
    std::vector<EncounterInstanceEntry> instances_;
    std::vector<EncounterBossEntry> bosses_;

    std::unordered_map<std::uint32_t, std::vector<EncounterAbilityEntry>>
        abilities_;
    bool open_{false};
};

}
