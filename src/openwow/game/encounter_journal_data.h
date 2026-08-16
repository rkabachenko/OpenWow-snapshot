
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

enum class EncounterJournalDifficulty : std::uint8_t {
    Normal = 0,
    Heroic = 1,
    Raid10 = 2,
    Raid10Heroic = 3,
    Raid25 = 4,
    Raid25Heroic = 5,
};

struct EncounterAbilityInfo {
    std::uint32_t abilityId{0};
    std::string name;
    std::string description;
    std::uint32_t iconId{0};
    bool isImportant{false};
    std::uint8_t phase{0};
};

struct EncounterBossInfo {
    std::uint32_t bossId{0};
    std::string name;
    std::string description;
    std::uint32_t mapId{0};
    std::uint32_t displayId{0};
    std::vector<EncounterAbilityInfo> abilities;
    std::string health;
};

struct EncounterInstanceInfo {
    std::uint32_t instanceId{0};
    std::string name;
    std::string type;
    std::uint8_t minLevel{0};
    std::uint8_t maxPlayers{0};
    std::uint8_t expansion{0};
    std::vector<EncounterBossInfo> bosses;
};

class EncounterJournalData {
 public:

    void AddInstance(EncounterInstanceInfo info);

    [[nodiscard]] std::optional<EncounterInstanceInfo> GetInstance(
        std::uint32_t instanceId) const;

    [[nodiscard]] std::vector<EncounterInstanceInfo> GetAllInstances() const;

    [[nodiscard]] std::vector<EncounterInstanceInfo> GetDungeons() const;

    [[nodiscard]] std::vector<EncounterInstanceInfo> GetRaids() const;

    [[nodiscard]] std::optional<EncounterBossInfo> GetBoss(
        std::uint32_t bossId) const;

    [[nodiscard]] std::vector<EncounterAbilityInfo> GetBossAbilities(
        std::uint32_t bossId) const;

    [[nodiscard]] std::vector<EncounterInstanceInfo> GetInstancesForExpansion(
        std::uint8_t expansion) const;

    [[nodiscard]] std::vector<EncounterBossInfo> SearchBoss(
        const std::string& query) const;

    [[nodiscard]] std::uint32_t GetBossCount() const;

    [[nodiscard]] std::uint32_t GetInstanceCount() const;

    void Clear();

    void Open();
    void Close();
    [[nodiscard]] bool IsOpen() const;

 private:
    std::vector<EncounterInstanceInfo> instances_;
    bool open_{false};
};

}
